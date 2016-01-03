#ifndef TILEMAP_H
#define TILEMAP_H

#include "SDL.h"
#include "types.h"
#define TILE_HEIGHT_FOR_SENSOR(heights, tile_index, sensor_dir) \
  ((int*)(&heights[tile_index.index]) + (sensor_dir * 32))

typedef struct {
    int top2down[32];
    int bottom2up[32];
    int right2left[32];
    int left2right[32];
} TileHeights;

typedef struct {
    // In case we need to load tex
    int tex_asset;
    // Texture atlas to grab tiles from
    SDL_Texture* tex;
    // Amount of tiles per row in the texture atlas
    int tiles_per_row;
    // dimensions
    int width, height;
    // Array of compressed tile indices
    int* tiles;
} Tilemap;

typedef struct {
    // Collision heights
    TileHeights* heights;
    // Dimensions
    int width, height;
    // Array of tile indices width * height long
    int* tiles;
} CollisionMap;

// ==== TILE INDEX FLAGS (ENDIAN dependent?) ====
#define TILE_FLIP_X ((byte)(1 << 7))
#define TILE_FLIP_Y ((byte)(1 << 6))
#define NOT_A_TILE  ((byte)(1 << 0))

typedef struct {
    byte flags;
    int index;
} TileIndex;

TileIndex tile_at(CollisionMap* tilemap, vec4i* tilespace, const int sensor);
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

struct Game;
struct Character;

TileIndex tile_from_int(int raw_tile_index);
TileCollision process_side_sensor(struct Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor_dir, const int sensor);
TileCollision process_top_sensor(struct Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor);
TileCollision process_bottom_sensor(struct Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor);
TileCollision process_bottom_sensor_one_tile_down(struct Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor);
void sense_tile(vec4* guy_pos_f, vec4i* tilemap_dim, vec4i* sensors, /*out*/SensedTile* result);
void draw_tilemap(struct Game* game, Tilemap* tilemap);
void collide_character(struct Character* guy, CollisionMap* tile_collision);
void slide_character(float gravity, struct Character* guy);

typedef struct {
    CollisionMap tile_collision;
    int number_of_tilemaps;
    Tilemap* tilemaps;
} Map;

typedef struct {
    char magic[3];
    Uint32 tiles_wide;
    Uint32 tiles_high;
    Uint8 tilemap_count;
} CmFileHeader;

CmFileHeader read_cm_file_header(const int asset);
void load_map(const int asset, /*out*/Map* map);
void draw_map(struct Game* game, Map* map);

#endif // TILEMAP_H
