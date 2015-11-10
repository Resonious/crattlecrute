#include "assets.h"
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include "SDL.h"
#include "stb_image.h"

FILE* assets_file = NULL;
extern SDL_Window* main_window;

// This should be called just once at the beginning of main()
int open_assets_file() {
    char assets_path[1024];
    sprintf(assets_path, "%scrattlecrute.assets", SDL_GetBasePath());

    assets_file = fopen(assets_path, "rb");
    if (assets_file == NULL) {
        printf("No asset file!!! (%s)", assets_path);
        SDL_ShowSimpleMessageBox(0, "YO!", "No assets file!!!", main_window);
        return errno;
    }
    return 0;
}

AssetFile load_asset(int asset) {
    AssetFile f;
    f.size = ASSETS[asset].size;
    // CLEAN UP AFTER YOURSELF!
    f.bytes = malloc(f.size);
    fseek(assets_file, ASSETS[asset].offset, SEEK_SET);
    fread(f.bytes, 1, f.size, assets_file);

    return f;
}

SDL_Surface* load_image(int asset) {
    AssetFile image_asset = load_asset(asset);

    int width = 0, height = 0, comp = 0;
    byte* image = stbi_load_from_memory(image_asset.bytes, image_asset.size, &width, &height, &comp, 4);
    if (image == NULL)
        SDL_ShowSimpleMessageBox(0, "YO!", "Couldn't load this image", main_window);

    return SDL_CreateRGBSurfaceFrom(
        image, width, height, 32, width * 4,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
        0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
    );
}

SDL_Texture* load_texture(SDL_Renderer* renderer, int asset) {
    // This'll free the image but not the texture.
    SDL_Surface* img = load_image(asset);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, img);
    free_image(img);
    return tex;
}

// NOTE this assumes that the SDL_Surface.pixels is the same buffer as the
// image loaded from stbi_load_from_memory.
void free_image(SDL_Surface* image) {
    stbi_image_free(image->pixels);
    SDL_FreeSurface(image);
}
