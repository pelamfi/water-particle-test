#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"

#include "SDL2/SDL.h"
#include "stdint.h"
#include "stdlib.h"

#pragma clang diagnostic pop

struct particle
{
    uint16_t x; // particleCoordFBits fractional bits
    uint16_t y; 
    int16_t xVel; // particleVelFBits fractional bits
    int16_t yVel;
};

typedef uint8_t DensityBufferType;

static int const particleCalculationFBits = 16; // calculations done at this precision
static int const particlePosFBits = 4; // particle coordinate fractional bits
static int const particleVelFBits = 8; // particle velocity fractional bits
static int const screenWidth = 640;
static int const screenHeight = 480;
static int const densityBufferWidthExp2 = 10;
static int const densityBufferWidth = 1 << densityBufferWidthExp2;
static int const particlePosXMask = (1 << (densityBufferWidthExp2 + particlePosFBits)) - 1;
static int const densityBufferHeightExp2 = 9;
static int const densityBufferHeight = 1 << densityBufferHeightExp2;
static int const particlePosYMask = (1 << (densityBufferHeightExp2 + particlePosFBits)) - 1;
static int const particleCount = 10000;
static int const particleBufferSize = particleCount * sizeof(particle);
static int const densityBufferYMargins = 8; // kernel is 3 so this should be plenty
static int const densityBufferSize = densityBufferWidth * (densityBufferHeight + densityBufferYMargins) * sizeof(DensityBufferType);

static DensityBufferType* densityBuffer = 0;
static particle* particleBuffer = 0;

static SDL_Event event;
static SDL_Window* window = 0;
static SDL_Surface* screen = 0;
static bool exitRequested = false;

static int bpp = 0;

static int frameCounter = 0;

static void allocateBuffers() {
    densityBuffer = (DensityBufferType*)malloc(densityBufferSize);
    memset(densityBuffer, 0, densityBufferSize);
    densityBuffer += densityBufferYMargins / 2; // margin to reduce need for explicit bounds checking

    particleBuffer = (particle*)malloc(particleBufferSize);
    memset(particleBuffer, 0, particleBufferSize);
}

static DensityBufferType* densityAddr(int x, int y) {
    return &densityBuffer[y * densityBufferWidth + x];
}

static void densityBlock(int x, int y, int w, int h, DensityBufferType const& v) {
    for(int yp = y; yp < y + h; yp++) {
        for(int xp = x; xp < x + w; xp++) {
            *densityAddr(xp, yp) = v;
        }
    }
}

static void drawInitialDensityMap() {
    DensityBufferType const densityHard = 250;
    densityBlock(0, screenHeight - 10, screenWidth, 10, densityHard); // bottom
    densityBlock(0, 0, 10, screenHeight, densityHard); // left
    densityBlock(screenWidth - 10, 0, 10, screenHeight, densityHard); // right
    densityBlock(screenWidth / 2 - 30, screenHeight - 70, 60, 60, densityHard); // center
}

static void addParticleDensity(int x, int y) {
    // http://dev.theomader.com/gaussian-kernel-calculator/
    // sigma 0.5 size 3
    /* (0.024879,	0.107973,	0.024879,
        0.107973,	0.468592,	0.107973,
        0.024879,	0.107973,	0.024879)

        scala> List(0.024879, 0.107973, 0.024879, 0.107973, 0.468592, 0.107973, 0.024879, 0.107973, 0.024879)
        .map(_ * 64).map(Math.round)

        List(2, 7, 2, 7, 30, 7, 2, 7, 2)
        */
    DensityBufferType *p = densityAddr((x >> particlePosFBits) - 1, (y >> particlePosFBits) - 1);
    int const skip = densityBufferWidth - 2;
    *(p++) += 2; *(p++) += 7; *p += 2; p+=skip;
    *(p++) += 7; *(p++) += 30; *p += 7; p+=skip;
    *(p++) += 2; *(p++) += 7; *p += 2;
}

static void subParticleDensity(int x, int y) {
    DensityBufferType *p = densityAddr((x >> particlePosFBits) - 1, (y >> particlePosFBits) - 1);
    int const skip = densityBufferWidth - 2;
    *(p++) -= 2; *(p++) -= 7; *p -= 2; p+=skip;
    *(p++) -= 7; *(p++) -= 30; *p -= 7; p+=skip;
    *(p++) -= 2; *(p++) -= 7; *p -= 2;
}

static void setupInitialParticles() {
    for(int i = 0; i < particleCount; i++) {
        particleBuffer[i].x = rand() % ((screenWidth - 20) << particlePosFBits) + (10 << particlePosFBits);
        particleBuffer[i].y = rand() % ((screenHeight / 4 - 20) << particlePosFBits) + ((screenHeight / 4 * 2 + 10) << particlePosFBits);
        particleBuffer[i].xVel = (rand() % (2 << particleVelFBits)) - (1 << particleVelFBits);
        particleBuffer[i].yVel = (rand() % (2 << particleVelFBits)) - (1 << particleVelFBits);
        addParticleDensity(particleBuffer[i].x, particleBuffer[i].y);
    }
}

static void sdlInit() {
    // http://wiki.libsdl.org/SDL_GetWindowSurface?highlight=%28%5CbCategoryVideo%5Cb%29%7C%28CategoryEnum%29%7C%28CategoryStruct%29
    // http://stackoverflow.com/questions/20579658/pixel-drawing-in-sdl2-0#
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("SDL2 Test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
    // instead of creating a renderer, we can draw directly to the screen
    screen = SDL_GetWindowSurface(window);
    bpp = screen->format->BytesPerPixel;
}

static void sdlCleanup() {
    SDL_DestroyWindow(window);
    SDL_Quit();
}

static void sdlHandleEvents() {
    while(SDL_PollEvent(&event)) {
        switch(event.type) {
            case SDL_QUIT:
            exitRequested = 1;
            break;
        }
    }
}

static uint8_t* pixelAddr(int x, int y, int channel) {
    uint8_t* p = (uint8_t*)screen->pixels + y * screen->pitch + x * bpp + channel;
    return p;
}

static void renderFrameOld() {
    for (int x = 0; x < screenWidth; x++)
    {
        for (int y = 0; y < screenHeight; y++)
        {
            for (int c = 0; c < bpp; c++)
            {
                *pixelAddr(x, y, c) = (((frameCounter * c) + x) * (y + (frameCounter * (c ^ 2)))) & 0xff;
            }
        }
    }
}

static void updateParticleDim(uint16_t& posVar, int16_t& velVar) {
    int pos = posVar << (particleCalculationFBits - particlePosFBits);
    int vel = velVar << (particleCalculationFBits - particleVelFBits);
    int result = pos + vel;
    posVar = result >> (particleCalculationFBits - particlePosFBits);
}

static void updateSimulation() {
    for (int particleIndex = 0; particleIndex < particleCount; particleIndex++) {
        particle* p = &particleBuffer[particleIndex];
        subParticleDensity(p->x, p->y);
        updateParticleDim(p->x, p->xVel);
        updateParticleDim(p->y, p->yVel);
        p->x &= particlePosXMask;
        p->y &= particlePosYMask;
        addParticleDensity(p->x, p->y);
    }
}


static void renderFrame() {
    for (int y = 0; y < screenHeight; y++)
    {
        for (int x = 0; x < screenWidth; x++)
        {
            *pixelAddr(x, y, 0) = *densityAddr(x, y);
            *pixelAddr(x, y, 1) = *densityAddr(x, y) * 2 & 0xff;
            *pixelAddr(x, y, 2) = *densityAddr(x, y) * 4 & 0xff;
        }
    }
}

static void renderLoop() {
    while (!exitRequested)
    {
        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);

        updateSimulation();
        renderFrame();
        
        if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);

        // this works just like SDL_Flip() in SDL 1.2
        SDL_UpdateWindowSurface(window);
        frameCounter++;

        sdlHandleEvents();
    }
}

int main(int argc, char* argv[])
{
    allocateBuffers();
    drawInitialDensityMap();
    setupInitialParticles();
    sdlInit();

    renderLoop();

    sdlCleanup();
    return EXIT_SUCCESS;
}
