/*
 * Copyright © Peter Lamberg 2017 (github-projects@pelam.fi)
 *
 * This file is part of water-particle-test.
 *
 * water-particle-test is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version
 *
 * water-particle-test is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with water-particle-test.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"

#include "SDL2/SDL.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
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
// https://sestevenson.wordpress.com/2009/08/19/rounding-in-fixed-point-number-conversions/
static int const particleVelCalculationRounding = 1 << (particleVelCalculationLeftShift - 1);
static int const particlePosCalculationLeftShift = particleCalculationFBits - particlePosFBits;
static int const particlePosCalculationRounding = 1 << (particlePosCalculationLeftShift - 1);
static int const particlePosRounding = 1 << (particlePosFBits - 1);
static int const particleGravity = 6; // particleVelFBits fractional bits included
static int const particleFriction = 250; // particleVelFBits fractional bits included
// shift left, because the density difference is added to a variable with particleCalculationFBits fractional bits
static int const gradientShift = 6;
static int const stepsPerSecond = 120;
static int const msPerStep = 1000 / stepsPerSecond;

static int const screenWidth = 640;
static int const screenHeight = 480;
static int const densityBufferWidthExp2 = 10;
static int const densityBufferWidth = 1 << densityBufferWidthExp2;
static int const particlePosXMask = (1 << (densityBufferWidthExp2 + particlePosFBits)) - 1;
static int const densityBufferHeightExp2 = 9;
static int const densityBufferHeight = 1 << densityBufferHeightExp2;
static int const particlePosYMask = (1 << (densityBufferHeightExp2 + particlePosFBits)) - 1;
static int const particleCount = 20000;
static int const particleBufferSize = particleCount * sizeof(particle);
// kernel is 3px, and derivates are calculated across 5px so this should be plenty
static int const densityBufferYMargins = 20;
static int const densityBufferSize = densityBufferWidth * (densityBufferHeight + densityBufferYMargins) * sizeof(DensityBufferType);

static int const borderThickness = 15;
static int const blockWidth = 300;
static int const blockHeight = 90;
static int const blockXMargin = 15;
static int const blockYMargin = 10;
static int const blockYPos = screenHeight - blockHeight - borderThickness - blockYMargin;
static int const blockXBasePos = borderThickness + blockXMargin;
static int const blockRange = screenWidth - borderThickness * 2 - blockWidth - blockXMargin * 2;
static int const blockStopMs = 3500;
static int const blockStopSteps = blockStopMs / msPerStep;

static DensityBufferType const densityHard = 300;

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
static int accumulatedStepMs = 0;

static void allocateBuffers()
{
    densityBuffer = (DensityBufferType*)malloc(densityBufferSize);
    memset(densityBuffer, 0, densityBufferSize);

    // This allows writing particle kernels outside of the buffer without needing bounds checking.
    // Unsigned integers and the particlePosYMask ensure that the particle coords are kept in check.
    densityBuffer +=
        densityBufferWidth * densityBufferYMargins / 2; // margin to reduce need for explicit bounds checking

    particleBuffer = (particle*)malloc(particleBufferSize);
    memset(particleBuffer, 0, particleBufferSize);
}

static DensityBufferType* densityAddr(int x, int y)
{
    return densityBuffer + y * densityBufferWidth + x;
}

static void densityBlock(int x, int y, int w, int h, int value)
{
    for (int yp = y; yp < y + h; yp++)
    {
        for (int xp = x; xp < x + w; xp++)
        {
            *densityAddr(xp, yp) += value;
        }
    }
}

static void drawInitialDensityMap()
{
    densityBlock(borderThickness, screenHeight - borderThickness, screenWidth - borderThickness * 2, borderThickness, densityHard); // bottom
    densityBlock(borderThickness, screenHeight - borderThickness + 2, screenWidth - borderThickness * 2,
                 borderThickness - 2, densityHard); // bottom 2
    densityBlock(borderThickness, screenHeight - borderThickness + 4, screenWidth - borderThickness * 2,
                 borderThickness - 4, densityHard); // bottom 2
    densityBlock(0, 0, borderThickness, screenHeight, densityHard); // left
    densityBlock(0, 0, borderThickness - 2, screenHeight, densityHard); // left
    densityBlock(0, 0, borderThickness - 4, screenHeight, densityHard); // left
    densityBlock(screenWidth - borderThickness, 0, borderThickness, screenHeight, densityHard); // right
    densityBlock(screenWidth - borderThickness + 2, 0, borderThickness - 2, screenHeight, densityHard); // right
    densityBlock(screenWidth - borderThickness + 4, 0, borderThickness - 4, screenHeight, densityHard); // right
    densityBlock(blockXBasePos, blockYPos, blockWidth, blockHeight, densityHard); // The moving block initial state
}

static void moveBlock()
{
    int t = stepCounter / 2;
    if (stepCounter % 2 == 1)
    {
        return;
    }

    int rangeStops = blockRange + blockStopSteps * 2;
    int pos = rangeStops - abs(t % (rangeStops * 2) - rangeStops) - blockStopSteps;
    if (pos < blockRange && pos >= 0)
    {
        int dir = t % (rangeStops * 2) > rangeStops ? -1 : 1;
        // remove slice from left / dir < 0: add slice to left
        densityBlock(blockXBasePos + pos, blockYPos, 1, blockHeight, densityHard * dir * -1);
        // add slice to right / dir < 0: remove slice from right
        densityBlock(blockXBasePos + pos + blockWidth, blockYPos, 1, blockHeight, densityHard * dir);
    }
}

// Parameter is pointer to the top left corner of the 3x3 density kernel
static void addParticleDensity(DensityBufferType* p)
{
    // http://dev.theomader.com/gaussian-kernel-calculator/
    // sigma 0.5 size 3
    /* (0.024879,	0.107973,	0.024879,
        0.107973,	0.468592,	0.107973,
        0.024879,	0.107973,	0.024879)

        scala> List(0.024879, 0.107973, 0.024879, 0.107973, 0.468592, 0.107973, 0.024879, 0.107973, 0.024879)
        .map(_ * 64).map(Math.round)

        List(2, 7, 2, 7, 30, 7, 2, 7, 2)
        */
    *(p++) += 2;
    *(p++) += 7;
    *p += 2;
    p += densityBufferWidth - 2;
    *(p++) += 7;
    *(p++) += 30;
    *p += 7;
    p += densityBufferWidth - 2;
    *(p++) += 2;
    *(p++) += 7;
    *p += 2;
}

static void subParticleDensity(DensityBufferType* p)
{
    *(p++) -= 2;
    *(p++) -= 7;
    *p -= 2;
    p += densityBufferWidth - 2;
    *(p++) -= 7;
    *(p++) -= 30;
    *p -= 7;
    p += densityBufferWidth - 2;
    *(p++) -= 2;
    *(p++) -= 7;
    *p -= 2;
}

static DensityBufferType* densityKernelTopLeftAddr(int x, int y)
{
    return densityAddr(((x + particlePosRounding) >> particlePosFBits) - 1, ((y + particlePosRounding) >> particlePosFBits) - 1);
}

static void setupInitialParticles()
{
    for (int i = 0; i < particleCount; i++)
    {
        particleBuffer[i].x = rand() % ((screenWidth - borderThickness * 2) << particlePosFBits) + (borderThickness << particlePosFBits);
        particleBuffer[i].y = rand() % ((screenHeight / 4 - borderThickness * 2) << particlePosFBits) + (borderThickness << particlePosFBits);
        int const velRange = 1 << (particleVelFBits + 1);
        particleBuffer[i].xVel = (rand() % velRange) - (velRange / 2);
        particleBuffer[i].yVel = (rand() % velRange) - (velRange / 2);
        addParticleDensity(densityKernelTopLeftAddr(particleBuffer[i].x, particleBuffer[i].y));
    }
}

static void sdlInit()
{
    // http://wiki.libsdl.org/SDL_GetWindowSurface?highlight=%28%5CbCategoryVideo%5Cb%29%7C%28CategoryEnum%29%7C%28CategoryStruct%29
    // http://stackoverflow.com/questions/20579658/pixel-drawing-in-sdl2-0#
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Peter Lamberg's (water) Particle Test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              640, 480, 0);
    // instead of creating a renderer, we can draw directly to the screen
    screen = SDL_GetWindowSurface(window);
    bpp = screen->format->BytesPerPixel;
}

static void sdlCleanup()
{
    SDL_DestroyWindow(window);
    SDL_Quit();
}

static void sdlHandleEvents()
{
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            exitRequested = 1;
            break;
        }
    }
}

static uint8_t* pixelAddr(int x, int y, int channel)
{
    return (uint8_t*)screen->pixels + y * screen->pitch + x * bpp + channel;
}

static void updateParticleDim(uint16_t& posVar, int16_t& velVar, DensityBufferType const* p, int skip)
{
    int vel = velVar << particleVelCalculationLeftShift;

    // https://en.wikipedia.org/wiki/Sobel_operator
    // http://stackoverflow.com/a/10032882/1148030
    // However I'm cheating by using only a slice of a kernel.

    int derivative = 1 * *p;
    p += skip;
    derivative += 2 * *p;
    p += skip * 2;
    derivative += -2 * *p;
    p += skip;
    derivative += -1 * *p;

    vel += derivative << gradientShift;
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

static void updateSimulation()
{
    moveBlock();

    for (int particleIndex = 0; particleIndex < particleCount; particleIndex++)
    {
        particle& p = particleBuffer[particleIndex];

        DensityBufferType* derivCorner = densityAddr(((p.x + particlePosRounding) >> particlePosFBits) - 2,
                                                     ((p.y + particlePosRounding) >> particlePosFBits) - 2);
        DensityBufferType* derivTop = derivCorner + 2;
        DensityBufferType* derivLeft = derivCorner + densityBufferWidth * 2;
        DensityBufferType* fieldCorner = derivCorner + 1 + densityBufferWidth;

        subParticleDensity(fieldCorner);

        p.yVel += particleGravity;
        updateParticleDim(p.x, p.xVel, derivLeft, 1);
        updateParticleDim(p.y, p.yVel, derivTop, densityBufferWidth);
        p.x &= particlePosXMask;
        p.y &= particlePosYMask;
        addParticleDensity(densityKernelTopLeftAddr(p.x, p.y));
    }
}


static void renderFrame()
{
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

static int computeStepsBehindTarget()
{
    int targetStepCounter = SDL_GetTicks() / msPerStep;
    int stepsBehind = targetStepCounter - (stepCounter + skippedStepCounter);
    if (stepsBehind > 10)
    {
        skippedStepCounter += stepsBehind - 1;
        printf("%d steps skipped\n", skippedStepCounter);
        stepsBehind = 1;
    }
    return stepsBehind;
}

static void renderLoop()
{
    while (!exitRequested)
    {

        while (computeStepsBehindTarget() > 0)
        {
            int start = SDL_GetTicks();
            updateSimulation();
            int end = SDL_GetTicks();
            accumulatedStepMs += end - start;
            stepCounter++;

            int logIntervalSteps = 5000 / msPerStep;
            if (stepCounter % logIntervalSteps == logIntervalSteps - 1)
            {
                printf("%d%% simulation cpu load (1 cpu)\n", 100 * accumulatedStepMs / (logIntervalSteps * msPerStep));
                accumulatedStepMs = 0;
            }
        }


        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
        renderFrame();
        if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);

        SDL_UpdateWindowSurface(window); // basically a flip
        frameCounter++;

        sdlHandleEvents();
    }
}

int SDL_main(int argc, char* argv[])
{
    allocateBuffers();
    drawInitialDensityMap();
    setupInitialParticles();
    sdlInit();

    renderLoop();

    sdlCleanup();
    return EXIT_SUCCESS;
}
