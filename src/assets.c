#include "assets.h"
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include "SDL.h"
#include "stb_image.h"

extern SDL_Window* main_window;

#ifdef EMBEDDED_ASSETS
#ifdef _WIN32 //  ========================= VISUAL STUDIO RESOURCE FILE ===================
#include "embedded_assets.h"
#include <Windows.h>

byte* embedded_assets;

int open_assets_file() {
    HRSRC resource = FindResource(NULL, MAKEINTRESOURCE(IDR_RCDATA1), RT_RCDATA);
    HGLOBAL resource_data = LoadResource(NULL, resource);
    embedded_assets = LockResource(resource_data);
    return 0;
}

AssetFile load_asset(int asset) {
    AssetFile f;
    f.size = ASSETS[asset].size;
    f.bytes = embedded_assets + ASSETS[asset].offset;

    return f;
}

#else // ============================== *NIX LD EMBEDDED ASSETS =======================
extern char _binary_build_crattlecrute_assets_start[];
extern char _binary_build_crattlecrute_assets_end[];

int open_assets_file() { return 0; }

AssetFile load_asset(int asset) {
    AssetFile f;
    f.size = ASSETS[asset].size;
    f.bytes = _binary_build_crattlecrute_assets_start + ASSETS[asset].offset;

    return f;
}
#endif // _WIN32

#else // EMBEDDED_ASSETS =============================== NORMAL EXTERNAL ASSETS FILE ================
// NOTE!!!!!!!!!!! This leaks !!!!!!!!!!!!! I'm gonna assume embedded assets is the only way we ship.
// (embedded doesn't allocate)
FILE* assets_file = NULL;

int open_assets_file() {
    char assets_path[1024];
    sprintf(assets_path, "%scrattlecrute.assets", SDL_GetBasePath());

    assets_file = fopen(assets_path, "rb");
    if (assets_file == NULL) {
        assets_file = fopen("crattlecrute.assets", "rb");
        if (assets_file == NULL) {
            printf("No asset file!!! (%s)", assets_path);
            SDL_ShowSimpleMessageBox(0, "YO!", "No assets file!!!", main_window);
            return errno;
        }
    }
    return 0;
}

AssetFile load_asset(int asset) {
    AssetFile f;
    f.size = ASSETS[asset].size;
    f.bytes = malloc(f.size);
    fseek(assets_file, ASSETS[asset].offset, SEEK_SET);
    fread(f.bytes, 1, f.size, assets_file);

    return f;
}
#endif // EMBEDDED_ASSETS


SDL_Surface* load_image(int asset) {
    // leak when using asset file........
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

__m128i mulm128i(__m128i* a, __m128i* b)
{
    __m128i tmp1 = _mm_mul_epu32(*a,*b); /* mul 2,0*/
    __m128i tmp2 = _mm_mul_epu32( _mm_srli_si128(*a,4), _mm_srli_si128(*b,4)); /* mul 3,1 */
    return _mm_unpacklo_epi32(_mm_shuffle_epi32(tmp1, _MM_SHUFFLE (0,0,2,0)), _mm_shuffle_epi32(tmp2, _MM_SHUFFLE (0,0,2,0))); /* shuffle results to [63..0] and pack */
}

// This is for testing only it doesn't give a fuck about endianness
SDL_Texture* load_texture_with_color_change(SDL_Renderer* renderer, int asset, Uint32 c_from, Uint32 c_to) {
    // This'll free the image but not the texture.
    SDL_Surface* img = load_image(asset);
    Uint32* pixels = img->pixels;

    __m128i target_pixel = _mm_set1_epi32(c_from);
    __m128i replacement_pixel = _mm_set1_epi32(c_to);
    __m128i one = _mm_set1_epi32(1);

    for (int i = 0; i < img->w * img->h; i += 4) {
        // _mm_loadu_si128(&i);
        __m128i p4;
        p4 = _mm_loadu_si128(&pixels[i]);

        // abs() here because "true"s come out as -1, and we want to multiply by 1's instead.
        __m128i cmp_result = _mm_abs_epi32(_mm_cmpeq_epi32(p4, target_pixel));
        // Keep an inverse so we can retain any non-target pixels when re-assigning.
        __m128i inverse_result = _mm_xor_si128(cmp_result, one);
        // This is the target pixel, only in the cells that it belongs.
        __m128i filtered_target = mulm128i(&cmp_result, &replacement_pixel);
        // This is the old pixels, such that filtered_target + filtered_retention = new set of pixels.
        __m128i filtered_retention = mulm128i(&inverse_result, &p4);

        // Add the filtered results and store.
        _mm_storeu_si128(&pixels[i], _mm_add_epi32(filtered_target, filtered_retention));
    }
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
