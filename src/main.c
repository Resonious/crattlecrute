#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "SDL.h"
#include "assets.h"
#include "types.h"

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
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);

    // Load test image for now...
    AssetFile image_asset = load_asset(ASSET_CRATTLECRUTE_BODY_PNG);

    int width = 0, height = 0, comp = 0;
    stbi_uc* image = stbi_load_from_memory(image_asset.bytes, image_asset.size, &width, &height, &comp, 4);
    if (image == NULL)
        printf("NO!!: %s\n", stbi_failure_reason());
    else
        printf("OK we have %ix%i image of %i pixel components.\n", width, height, comp);

    // NOTE remember these should be cleaned up...
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        image, width, height, comp, width,
        0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
    );
    if (surface == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (surface == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);

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
        SDL_Rect dest = { 100, 100, 90, 90 };
        SDL_RenderCopy(renderer, texture, &src, &dest);

        SDL_RenderPresent(renderer);
    }

    stbi_image_free(image);
    SDL_DestroyWindow(window);

    SDL_Quit();
    return 0;
}
