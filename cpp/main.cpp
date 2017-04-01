#include "SDL2/SDL.h" // include SDL header
#include "stdint.h"
#include "stdlib.h"

using namespace std;

struct particle
{
    uint16_t x; // 4 fractional bits
    uint16_t y; // 4 fractional bits
    uint16_t xVel; // 8 fractional bits
    uint16_t yVel; // 8 fractional bits
};

int const particleCount = 10000;

// http://wiki.libsdl.org/SDL_GetWindowSurface?highlight=%28%5CbCategoryVideo%5Cb%29%7C%28CategoryEnum%29%7C%28CategoryStruct%29
// http://stackoverflow.com/questions/20579658/pixel-drawing-in-sdl2-0#
int main(int argc, char* argv[])
{
    unsigned char* densityBuffer = (unsigned char*)malloc(640 * 480);
    particle* particleBuffer = (particle*)malloc(particleCount * sizeof(particle));
    SDL_Event event;    
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("SDL2 Test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);

    // instead of creating a renderer, we can draw directly to the screen
    SDL_Surface* screen = SDL_GetWindowSurface(window);

    int bpp = screen->format->BytesPerPixel;

    int loop = 0;

    while (1)
    {

        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);

        for (int x = 0; x < 640; x++)
        {
            for (int y = 0; y < 480; y++)
            {
                for (int c = 0; c < bpp; c++)
                {
                    Uint8* p = (Uint8*)screen->pixels + y * screen->pitch + x * bpp + c;
                    *p = (((loop * c) + x) * (y + (loop * (c ^ 2)))) & 0xff;
                }
            }
        }

        if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);

        // this works just like SDL_Flip() in SDL 1.2
        SDL_UpdateWindowSurface(window);

        if (SDL_PollEvent(&event) && event.type == SDL_QUIT)
        {
            break;
        }

        loop++;
    }

    // SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
