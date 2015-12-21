#ifndef TILEMAP_H
#define TILEMAP_H

#include "SDL.h"
#include "types.h"
#include "assets.h"

#define S1X 0
#define S1Y 1
#define S2X 2
#define S2Y 3
#define SENSOR_1 0
#define SENSOR_2 2
#define X 0
#define Y 1
#define BOTTOM_SENSOR 0
#define TOP_SENSOR 1
#define RIGHT_SENSOR 2
#define LEFT_SENSOR 3

#define TILE_AT(tilemap, tilespace, sensor) \
    tilemap.tiles[tilespace.x[sensor + 1] * tilemap.width + tilespace.x[sensor]]

#define TOP2DOWN 0
#define BOTTOM2UP 1
#define RIGHT2LEFT 2
#define LEFT2RIGHT 3
#define TILE_HEIGHT_FOR_SENSOR(heights, tile_index, sensor_dir) \
  ((int*)(&heights[tile_index.index]) + (sensor_dir * 32))

typedef struct {
    // Texture atlas to grab tiles from
    SDL_Texture* tex;
    // Amount of tiles per row in the texture atlas
    int tiles_per_row;
    // Array of tile indices - should be width * height long
    int* tiles;
    int width, height;
} Tilemap;

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


#endif // TILEMAP_H
