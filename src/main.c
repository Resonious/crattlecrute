#include <stdio.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "SDL.h"
#include "assets.h"
#include "errno.h"

int main(int argc, char** argv) {
    SDL_Window* window;

    printf("BEFORE INIT\n");

    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));

    window = SDL_CreateWindow(
        "Niiiice",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_OPENGL
    );

    FILE* assets_file = fopen("crattlecrute.assets", "rb");
    if (!assets_file)
      printf("NO!!!!!!!!!!!!!!! ERRNO %i\n", errno);

    unsigned char buf[1000];
    fseek(assets_file, TERRAIN_DIRT1_PNG_OFFSET, SEEK_SET);
    if (!assets_file) printf("INSANE WHAT HAPPEN\n");
    size_t size_read = fread(buf, 1, TERRAIN_DIRT1_PNG_SIZE, assets_file);
    if (size_read != TERRAIN_DIRT1_PNG_SIZE)
        printf("WHAT THE FUCK\n");

    int x = 0, y = 0, comp = 0;
    stbi_uc* image = stbi_load_from_memory(buf, TERRAIN_DIRT1_PNG_SIZE, &x, &y, &comp, 4);
    if (image == NULL)
        printf("ERROR: %s\n", stbi_failure_reason());
    else
        printf("OK we have %ix%i image of %i pixel components.\n", x, y, comp);

    SDL_Delay(10000);

    stbi_image_free(image);
    SDL_DestroyWindow(window);

    SDL_Quit();
    return 0;
}
