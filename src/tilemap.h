#ifndef TILEMAP_H
#define TILEMAP_H

#include "SDL.h"
#include "game.h"
#define TILE_HEIGHT_FOR_SENSOR(heights, tile_index, sensor_dir) \
  ((int*)(&heights[tile_index.index]) + (sensor_dir * 32))

typedef struct {
    // Texture atlas to grab tiles from
    SDL_Texture* tex;
    // Amount of tiles per row in the texture atlas
    int tiles_per_row;
    // Array of compressed tile indices
    int* tiles;
    int width, height;
} Tilemap;

typedef struct {
    // Array of tile indices width * height long
    int* tiles;
    int width, height;
} CollisionMap;

// ENDIAN dependent?
#define TILE_FLIP_X ((byte)(1 << 7))
#define TILE_FLIP_Y ((byte)(1 << 6))
#define NOT_A_TILE  ((byte)(1 << 0))

typedef struct {
    byte flags;
    int index;
} TileIndex;

TileIndex tile_at(Tilemap* tilemap, vec4i* tilespace, const int sensor);
int* tile_height_for_sensor(TileHeights* all_heights, TileIndex* tile_index, const int sensor_dir);

typedef struct {
    vec4i tilespace;
    vec4i tilepos;
    vec4i position_within_tile;
    vec4i indices_are_valid;
} SensedTile;

typedef struct {
    bool hit;
    float new_position;
} TileCollision;

TileIndex tile_from_int(int raw_tile_index);
TileCollision process_sensor(Character* guy, Tilemap* tilemap, SensedTile* t, const int sensor_dir, const int sensor, const int dim);
TileCollision process_bottom_sensor(Character* guy, Tilemap* tilemap, SensedTile* t, const int sensor);
TileCollision process_bottom_sensor_one_tile_down(Character* guy, Tilemap* tilemap, SensedTile* t, const int sensor);
void sense_tile(vec4* guy_pos_f, vec4i* tilemap_dim, vec4i* sensors, /*out*/SensedTile* result);
void draw_tilemap(Game* game, Tilemap* tilemap);

#endif // TILEMAP_H
