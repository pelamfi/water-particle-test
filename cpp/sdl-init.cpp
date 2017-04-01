#include "SDL2/SDL.h" // include SDL header

extern "C" SDL_Window * sdl_init() {
  
  SDL_Init(SDL_INIT_VIDEO);
  
  return SDL_CreateWindow("SDL2 Test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
}

extern "C" double run() {
  SDL_GetWindowSurface(sdl_init());
  return 3.0;
}
