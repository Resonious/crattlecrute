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

    open_assets_file();
    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));

    window = SDL_CreateWindow(
        "Niiiice",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_OPENGL
    );

    AssetFile image_asset = load_asset(ASSET_CRATTLECRUTE_BODY_PNG);

    int x = 0, y = 0, comp = 0;
    stbi_uc* image = stbi_load_from_memory(image_asset.bytes, image_asset.size, &x, &y, &comp, 4);
    if (image == NULL)
        printf("NO!!: %s\n", stbi_failure_reason());
    else
        printf("OK we have %ix%i image of %i pixel components.\n", x, y, comp);
    stbi_image_free(image);

    SDL_Delay(10000);

    SDL_DestroyWindow(window);

    SDL_Quit();
    return 0;
}
