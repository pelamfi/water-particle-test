#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"

#include "SDL2/SDL.h" // include SDL header
#include "stdint.h"
#include "stdlib.h"

#pragma clang diagnostic pop

//using namespace std;

struct particle
{
    uint16_t x; // 4 fractional bits
    uint16_t y; // 4 fractional bits
    uint16_t xVel; // 8 fractional bits
    uint16_t yVel; // 8 fractional bits
};

typedef uint8_t DensityBufferType;


static int const screenWidth = 640;
static int const screenHeight = 480;
static int const densityBufferWidth = screenWidth;
static int const densityBufferHeight = screenHeight;
static int const particleCount = 10000;
static int const particleBufferSize = particleCount * sizeof(particle);
static int const densityBufferSize = densityBufferWidth * densityBufferHeight * sizeof(DensityBufferType);
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
    densityBlock(0, densityBufferHeight - 10, densityBufferWidth, 10, densityHard); // bottom
    densityBlock(0, 0, 10, densityBufferHeight, densityHard); // left
    densityBlock(densityBufferWidth - 10, 0, 10, densityBufferHeight, densityHard); // right
    densityBlock(densityBufferWidth / 2 - 30, densityBufferHeight - 70, 60, 60, densityHard); // center
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

static void renderFrame() {
    for (int y = 0; y < screenHeight; y++)
    {
        for (int x = 0; x < screenWidth; x++)
        {
            *pixelAddr(x, y, 0) = (uint8_t)0;
            *pixelAddr(x, y, 1) = (uint8_t)0;
            *pixelAddr(x, y, 2) = *densityAddr(x, y);
        }
    }
}

static void renderLoop() {
    while (!exitRequested)
    {
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
    sdlInit();

    renderLoop();

    sdlCleanup();
    return EXIT_SUCCESS;
}
