#ifndef CACH_H
#define CACH_H
// Implementation for this file resides in assets.c :(

#include "assets.h"

#define ASSET_NOT_LOADED 2147483647

typedef struct PeripheralOffset {
    // Dimensions of bottom relative to frame center point.
    int x, y;
    // Angle from bottom to top in degrees as usual.
    float angle;
} PeripheralOffset;

typedef struct AnimationAtlas {
    SDL_Texture* texture;
    int width, height;

    //[number_of_frames_in_the_texture]
    // Each entry is the rect of the FIRST LAYER of the frame.
    // The rectangle must be incremented within the atlas manually
    // to retrieve the other layers.
    SDL_Rect* frames;

    //[number_of_frames_in_the_texture]
    PeripheralOffset* eye_offsets;
} AnimationAtlas;

struct Map;
typedef struct {
    int id;
    void(*free)(void*);
    union {
        SDL_Texture* texture;
        AudioWave* sound;
        struct Map* map;
        AnimationAtlas* atlas;
        void* data;
    };
} CachedAsset;

typedef struct {
    CachedAsset assets[100];
} AssetCache;

struct Game;
void free_cached_asset(struct Game* game, int asset);
SDL_Texture* cached_texture(struct Game* game, int asset);
AudioWave* cached_sound(struct Game* game, int asset);
struct Map* cached_map(struct Game* game, int asset);
AnimationAtlas* cached_atlas(struct Game* game, int asset, int sprite_width, int sprite_height, int eye_offset_layer);

void cached_texture_dimensions(struct Game* game, int asset, /*out*/TextureDimensions* dims);

#endif // CACH_H