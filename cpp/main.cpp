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

typedef uint16_t DensityBufferType;

static int const particleCalculationFBits = 16; // calculations done at this precision
static int const particlePosFBits = 4; // particle coordinate fractional bits
static int const particleVelFBits = 8; // particle velocity fractional bits
static int const particleFrictionFBits = 8;
static int const particleFrictionRounding = 1 << (particleFrictionFBits - 1);
static int const particleVelCalculationLeftShift = particleCalculationFBits - particleVelFBits;
static int const particleVelCalculationRounding = 1 << (particleVelCalculationLeftShift - 1); // https://sestevenson.wordpress.com/2009/08/19/rounding-in-fixed-point-number-conversions/
static int const particlePosCalculationLeftShift = particleCalculationFBits - particlePosFBits;
static int const particlePosCalculationRounding = 1 << (particlePosCalculationLeftShift - 1);
static int const particlePosRounding = 1 << (particlePosFBits - 1);
static int const particleGravity = 6; // particleVelFBits fractional bits included
static int const particleFriction = 250; // particleVelFBits fractional bits included
static int const gradientShift = 4; // shift right, because the density difference is added to a variable with particleCalculationFBits fractional bits
static int const stepsPerSecond = 60;
static int const msPerStep = 1000 / stepsPerSecond;

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

static int stepCounter = 0;
static int skippedStepCounter = 0;

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
    DensityBufferType const densityHard = 500;
    densityBlock(0, screenHeight - 10, screenWidth, 10, densityHard); // bottom
    densityBlock(0, 0, 10, screenHeight, densityHard); // left
    densityBlock(screenWidth - 10, 0, 10, screenHeight, densityHard); // right
    densityBlock(screenWidth / 2 - 60, screenHeight - 130, 120, 120, densityHard); // center
}

// Parameter is pointer to the top left corner of the 3x3 density kernel
static void addParticleDensity(DensityBufferType *p) {
    // http://dev.theomader.com/gaussian-kernel-calculator/
    // sigma 0.5 size 3
    /* (0.024879,	0.107973,	0.024879,
        0.107973,	0.468592,	0.107973,
        0.024879,	0.107973,	0.024879)

        scala> List(0.024879, 0.107973, 0.024879, 0.107973, 0.468592, 0.107973, 0.024879, 0.107973, 0.024879)
        .map(_ * 64).map(Math.round)

        List(2, 7, 2, 7, 30, 7, 2, 7, 2)
        */
    *(p++) += 2; *(p++) += 7; *p += 2; p += densityBufferWidth - 2;
    *(p++) += 7; *(p++) += 30; *p += 7; p += densityBufferWidth - 2;
    *(p++) += 2; *(p++) += 7; *p += 2;
}

static void subParticleDensity(DensityBufferType *p) {
    *(p++) -= 2; *(p++) -= 7; *p -= 2; p += densityBufferWidth - 2;
    *(p++) -= 7; *(p++) -= 30; *p -= 7; p += densityBufferWidth - 2;
    *(p++) -= 2; *(p++) -= 7; *p -= 2;
}

static DensityBufferType * densityKernelTopLeftAddr(int x, int y) {
    DensityBufferType *p = densityAddr(((x + particlePosRounding) >> particlePosFBits) - 1, ((y  + particlePosRounding) >> particlePosFBits) - 1);
    return p;
}

static void setupInitialParticles() {
    for(int i = 0; i < particleCount; i++) {
        particleBuffer[i].x = rand() % ((screenWidth - 20) << particlePosFBits) + (10 << particlePosFBits);
        particleBuffer[i].y = rand() % ((screenHeight / 4 - 20) << particlePosFBits) + ((screenHeight / 4 * 2 + 10) << particlePosFBits);
        int const velRange = 1 << (particleVelFBits + 1);
        particleBuffer[i].xVel = (rand() % velRange) - (velRange / 2);
        particleBuffer[i].yVel = (rand() % velRange) - (velRange / 2);
        addParticleDensity(densityKernelTopLeftAddr(particleBuffer[i].x, particleBuffer[i].y));
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

static void updateParticleDim(uint16_t& posVar, int16_t& velVar, DensityBufferType s1, DensityBufferType s2) {
    int vel = velVar << particleVelCalculationLeftShift;
    double diff = s2 - s1;
    double sinus = diff / sqrt(1 + diff * diff) * 256;
    int sinusInt = sinus;
    vel -= sinusInt * 50;
    // diff = s2 - s1
    // cos(theta) * f 
    // cos(theta) * h
    // sin(theta) * h = diff
    // h = sqrt(1^2 + diff^2)
    // sin(theta) = diff / sqrt(1^2 + diff^2)
    // sin(theta) = diff / sqrt(1 + diff^2)
    vel *= particleFriction;
    vel += particleFrictionRounding;
    vel >>= particleFrictionFBits;
    int updatedVel = (vel + particleVelCalculationRounding) >> particleVelCalculationLeftShift;

    int pos = posVar << particlePosCalculationLeftShift;
    pos += vel;
    int updatedPos = (pos + particlePosCalculationRounding) >> particlePosCalculationLeftShift;

    velVar = updatedVel;
    posVar = updatedPos;
}

static void updateSimulation() {
    for (int particleIndex = 0; particleIndex < particleCount; particleIndex++) {
        particle* p = &particleBuffer[particleIndex];

        DensityBufferType *dp = densityKernelTopLeftAddr(p->x, p->y);
        subParticleDensity(dp);

        p->yVel += particleGravity;
        updateParticleDim(p->x, p->xVel, *(dp + densityBufferWidth), *(dp + densityBufferWidth + 2));
        updateParticleDim(p->y, p->yVel, *(dp + 1), *(dp + 1 + densityBufferWidth * 2));
        p->x &= particlePosXMask;
        p->y &= particlePosYMask;
        addParticleDensity(densityKernelTopLeftAddr(p->x, p->y));
    }
}


static void renderFrame() {
    for (int y = 0; y < screenHeight; y++)
    {
        for (int x = 0; x < screenWidth; x++)
        {
            DensityBufferType density = *densityAddr(x, y);
            *pixelAddr(x, y, 0) = (density << 4) & 0xff;
            *pixelAddr(x, y, 1) = (density >> 2) & 0xff;
            *pixelAddr(x, y, 2) = (density >> 8) & 0xff;
        }
    }
}

static int computeStepsBehindTarget() {
    int targetStepCounter = SDL_GetTicks() / msPerStep;
    int stepsBehind = targetStepCounter - (stepCounter + skippedStepCounter);
    if (stepsBehind > 10) {
        skippedStepCounter += stepsBehind - 1;
        printf("%d steps skipped\n", skippedStepCounter);
        stepsBehind = 1;
    }
    return stepsBehind;
}

static void renderLoop() {
    while (!exitRequested)
    {

        while (computeStepsBehindTarget() > 0) {
            updateSimulation();
            stepCounter++;
        }

        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
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
