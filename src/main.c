#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "SDL.h"
#include "assets.h"
#include "types.h"

SDL_Window* main_window;

int main(int argc, char** argv) {
    SDL_Window* window;
    SDL_Renderer* renderer;

    open_assets_file();
    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));

    window = SDL_CreateWindow(
        "Niiiice",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        0
    );
    main_window = window;
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);

    // Load test image for now...
    SDL_Surface* image = load_image(ASSET_CRATTLECRUTE_BODY_PNG);

    if (image == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, image);
    if (texture == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);

    free_image(image);

    // Main loop bitch
    SDL_Event event;
    bool running = true;
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            }
        }

        // Draw!!! Finally!!!
        SDL_RenderClear(renderer);

        SDL_Rect src = { 0, 0, 90, 90 };
        SDL_Rect dest = { 20, 20, 90, 90 };
        SDL_RenderCopy(renderer, texture, &src, &dest);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyWindow(window);

    SDL_Quit();
    return 0;
}
