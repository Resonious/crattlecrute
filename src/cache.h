#ifndef CACH_H
#define CACH_H
// Implementation for this file resides in assets.c :(

#include "assets.h"

#define ASSET_NOT_LOADED 2147483647

typedef struct {
    int id;
    void(*free)(void*);
    union {
        SDL_Texture* texture;
        AudioWave* sound;
        Map* map;
        void* data;
    };
} CachedAsset;

typedef struct {
    CachedAsset assets[NUMBER_OF_ASSETS];
} AssetCache;

struct Game;
void free_cached_asset(struct Game* game, int asset);
SDL_Texture* cached_texture(struct Game* game, int asset);
AudioWave* cached_sound(struct Game* game, int asset);
Map* cached_map(struct Game* game, int asset);

void cached_texture_dimensions(struct Game* game, int asset, /*out*/TextureDimensions* dims);

#endif // CACH_H