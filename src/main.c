#include <stdio.h>
#include "SDL.h"

int main(int argc, char** argv) {
    SDL_Window* window;

    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));

    window = SDL_CreateWindow(
        "Niiiice",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_OPENGL
    );

    SDL_Delay(1000);

    SDL_DestroyWindow(window);

    SDL_Quit();
    return 0;
}
