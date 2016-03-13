#ifdef __APPLE__
#include <mach-o/getsect.h>
#else
#ifndef __FreeBSD__
#include <malloc.h>
#endif
#endif

#ifdef __FreeBSD__
#include "SDL2/SDL.h"
#else
#include "SDL.h"
#endif

#if defined(_WIN32) && defined(EMBEDDED_ASSETS)
#include <Windows.h>
#endif

#include "stb_image.h"

#include "assets.h"
#include "game.h"
#include <stdio.h>
#include <errno.h>

extern SDL_Window* main_window;

#ifdef EMBEDDED_ASSETS
#ifdef _WIN32 //  ========================= VISUAL STUDIO RESOURCE FILE ===================
#include "embedded_assets.h"

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

#elif __APPLE__ // ================= MACOSX CREATESECT =====================
byte* embedded_assets;

int open_assets_file() {
    unsigned long _size;
    embedded_assets = (byte*)getsectdata("assets", "assets", &_size);
    return 0;
}

AssetFile load_asset(int asset) {
    AssetFile f;
    f.size = ASSETS[asset].size;
    f.bytes = embedded_assets + ASSETS[asset].offset;

    return f;
}

#else // ============================== LINUX LD EMBEDDED ASSETS =======================
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

// only used within this file...
static SDL_Surface* makesurface(byte* image, int width, int height) {
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        image, width, height, 32, width * 4, RMASK, GMASK, BMASK, AMASK
    );
#ifdef _DEBUG
    if (surface == NULL) {
        SDL_ShowSimpleMessageBox(0, "BAD SURFACE!", SDL_GetError(), main_window);
    }
#endif
    return surface;
}

SDL_Surface* load_image(int asset) {
    // leak when using asset file........
    AssetFile image_asset = load_asset(asset);

    int width = 0, height = 0, comp = 0;
    byte* image = stbi_load_from_memory(image_asset.bytes, image_asset.size, &width, &height, &comp, 4);
    if (image == NULL)
        SDL_ShowSimpleMessageBox(0, "YO!", "Couldn't load this image", main_window);

    return makesurface(image, width, height);
}

SDL_Texture* load_texture(SDL_Renderer* renderer, int asset) {
    // This'll free the image but not the texture.
    SDL_Surface* img = load_image(asset);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, img);
    free_image(img);
    return tex;
}

// ===== The following two functions show that SIMD color replace sucks balls ======
SDL_Texture* load_texture_with_color_change_no_simd(SDL_Renderer* renderer, int asset, Uint32 c_from, Uint32 c_to) {
    // This'll free the image but not the texture.
    SDL_Surface* img = load_image(asset);

    Uint32* pixels = img->pixels;

    // === BENCHMARKING ===
    Uint64 before = SDL_GetPerformanceCounter();
    // === /BENCHMARKING ===
    for (int i = 0; i < img->w * img->h; i += 1) {
        if (pixels[i] == c_from)
            pixels[i] = c_to;
    }
    // === BENCHMARKING ===
    Uint64 after = SDL_GetPerformanceCounter();
    Uint64 ticks = after - before;
    double seconds = (double)ticks / (double)SDL_GetPerformanceFrequency();
    // === /BENCHMARKING ===

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
#ifdef _WIN32 // Won't compile on linux and we don't actually use it right now
SDL_Texture* load_texture_with_color_change(SDL_Renderer* renderer, int asset, Uint32 c_from, Uint32 c_to) {
    // This'll free the image but not the texture.
    SDL_Surface* img = load_image(asset);
    Uint32* pixels = img->pixels;

    __m128i target_pixel = _mm_set1_epi32(c_from);
    __m128i replacement_pixel = _mm_set1_epi32(c_to);
    __m128i one = _mm_set1_epi32(1);

    // === BENCHMARKING ===
    Uint64 before = SDL_GetPerformanceCounter();
    // === /BENCHMARKING ===
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
    // === BENCHMARKING ===
    Uint64 after = SDL_GetPerformanceCounter();
    Uint64 ticks = after - before;
    double seconds = (double)ticks / (double)SDL_GetPerformanceFrequency();
    // === /BENCHMARKING ===

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, img);
    free_image(img);
    return tex;
}
#endif

// NOTE this assumes that the SDL_Surface.pixels is the same buffer as the
// image loaded from stbi_load_from_memory.
void free_image(SDL_Surface* image) {
    stbi_image_free(image->pixels);
    SDL_FreeSurface(image);
}

// ===== ASSET CACHE =====

void free_cached_asset(struct Game* game, int asset) {
    CachedAsset* cached_asset = &game->asset_cache.assets[asset];
    if (cached_asset->id == ASSET_NOT_LOADED) return;
    cached_asset->free(cached_asset->data);
}

SDL_Texture* cached_texture(Game* game, int asset) {
    CachedAsset* cached_asset = &game->asset_cache.assets[asset];
    if (cached_asset->id == ASSET_NOT_LOADED) {
        cached_asset->id = asset;
        cached_asset->texture = load_texture(game->renderer, asset);
        cached_asset->free = SDL_DestroyTexture;
    }
    else {
        SDL_assert(cached_asset->id == asset);
    }

    return cached_asset->texture;
}

AudioWave* cached_sound(Game* game, int asset) {
    CachedAsset* cached_asset = &game->asset_cache.assets[asset];
    if (cached_asset->id == ASSET_NOT_LOADED) {
        cached_asset->id = asset;
        cached_asset->sound = malloc(sizeof(AudioWave));
        *cached_asset->sound = decode_ogg(asset);
        cached_asset->free = free_malloced_audio_wave;
    }
    else {
        SDL_assert(cached_asset->id == asset);
    }

    return cached_asset->sound;
}

#define READ(type, dest) \
    type dest; \
    SDL_memcpy(&dest, file.bytes + pos, sizeof(type)); \
    pos += sizeof(type)

Map* cached_map(Game* game, int asset) {
    CachedAsset* cached_asset = &game->asset_cache.assets[asset];
    if (cached_asset->id == ASSET_NOT_LOADED) {
        cached_asset->id = asset;
        CmFileHeader file_header = read_cm_file_header(asset);

        size_t bytes_needed_for_map =
            sizeof(Map) +
            (size_t)file_header.tilemap_count          * sizeof(Tilemap) +
            (size_t)file_header.background_count       * sizeof(ParallaxBackground) +
            (size_t)file_header.door_count             * sizeof(Door) +
            (size_t)file_header.total_spawn_rate_count * sizeof(MobSpawnRate) +
            (size_t)file_header.spawn_zone_count       * sizeof(MobSpawnZone) +
            sizeof(MapState);

        cached_asset->map = aligned_malloc(bytes_needed_for_map + 32); // some cruft room for 16 byte aligned map state
        cached_asset->map->locked = SDL_CreateMutex(); // TODO check for successful mutex creation
        load_map(asset, cached_asset->map);
        cached_asset->map->game = game;

        for (int i = 0; i < cached_asset->map->number_of_tilemaps; i++) {
            Tilemap* tilemap = &cached_asset->map->tilemaps[i];
            if (tilemap->tex == NULL)
                tilemap->tex = cached_texture(game, tilemap->tex_asset);
        }

        cached_asset->free = aligned_free;
    }
    else
        SDL_assert(cached_asset->id == asset);

    return cached_asset->map;
}

void cached_texture_dimensions(struct Game* game, int asset, /*out*/TextureDimensions* dims) {
    dims->tex = cached_texture(game, asset);
    int result = SDL_QueryTexture(dims->tex, NULL, NULL, &dims->width, &dims->height);
    SDL_assert(result == 0);
}

void free_cached_atlas(void* ptr) {
    AnimationAtlas* atlas = (AnimationAtlas*)ptr;
    aligned_free(atlas->frames);
    aligned_free(atlas->eye_offsets);
    aligned_free(atlas);
}

#include "character.h"

AnimationAtlas* cached_atlas(struct Game* game, int asset, int sprite_width, int sprite_height, int eye_offset_layer) {
    SDL_Surface* image = load_image(asset);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(game->renderer, image);

    CachedAsset* cached_asset = &game->asset_cache.assets[asset];
    if (cached_asset->id != ASSET_NOT_LOADED) {
        SDL_assert(cached_asset->id == asset);
        return cached_asset->atlas;
    }

    AnimationAtlas* animation = aligned_malloc(sizeof(AnimationAtlas));

    int number_of_frames = (image->w / sprite_width * image->h / sprite_height) / CHARACTER_LAYERS;

    SDL_Rect* rect = aligned_malloc(number_of_frames * CHARACTER_LAYERS * sizeof(SDL_Rect));
    PeripheralOffset* eye_offset = aligned_malloc(number_of_frames * sizeof(PeripheralOffset));

    animation->width       = image->w;
    animation->height      = image->h;
    animation->texture     = tex;
    animation->frames      = rect;
    animation->eye_offsets = eye_offset;

    for (int frame = 0; frame < number_of_frames; frame++, rect++, eye_offset++) {
        rect->x = sprite_width * frame * CHARACTER_LAYERS;
        rect->y = animation->height - sprite_height;
        rect->w = sprite_width;
        rect->h = sprite_height;

        while (rect->x >= animation->width) {
            rect->x -= animation->width;
            rect->y -= sprite_height;
        }

        SDL_assert(rect->x >= 0);
        SDL_assert(rect->x + sprite_width <= animation->width);
        SDL_assert(rect->y >= 0);
        SDL_assert(rect->y + sprite_height <= animation->height);

        SDL_Rect eye_layer = *rect;
        eye_layer.x += sprite_width * (eye_offset_layer - 1);
        while (eye_layer.x >= animation->width) {
            eye_layer.x -= animation->width;
            eye_layer.y -= sprite_height;
        }

        SDL_Point eye_bottom = { 0,0 }, eye_top = { 0,0 };

        for (int y = eye_layer.y; y < eye_layer.y + eye_layer.h; y++) {
            for (int x = eye_layer.x; x < eye_layer.x + eye_layer.w; x++) {
                int p = y * animation->width + x;
                Uint32 pixel = ((Uint32*)image->pixels)[p];

                if (pixel & AMASK) {
                    int pixel_x = x - eye_layer.x - eye_layer.w / 2;
                    int pixel_y = eye_layer.h - (y - eye_layer.y) - eye_layer.h / 2;

                    // Red indicates top
                    if (pixel == (AMASK | RMASK)) {
                        eye_top.x = pixel_x;
                        eye_top.y = pixel_y;
                    }
                    // Black indicates bottom
                    else if (pixel == (AMASK)) {
                        eye_bottom.x = pixel_x;
                        eye_bottom.y = pixel_y;
                    }
                    // No other pixels should be present
                    else {
                        SDL_assert(false);
                    }
                }
                if (eye_bottom.x != 0 && eye_top.x != 0)
                    goto FoundOffsets;
            }
        }
        continue;
        FoundOffsets:;

        eye_offset->x = eye_bottom.x;
        eye_offset->y = eye_bottom.y;
        eye_offset->angle = atan2f(eye_top.y - eye_bottom.y, eye_top.x - eye_bottom.x) / (float)M_PI * 180.0f;
    }
    free_image(image);

    cached_asset->id = asset;
    cached_asset->atlas = animation;
    cached_asset->free = free_cached_atlas;
    return animation;
}