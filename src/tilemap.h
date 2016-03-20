#ifndef TILEMAP_H
#define TILEMAP_H

#ifdef __FreeBSD__
#include "SDL2/SDL.h"
#else
#include "SDL.h"
#endif
#include "types.h"
#include "mob.h"
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

void set_collision_sensors(struct GenericBody* body, float width, float height, float y_offset);
TileIndex tile_from_int(int raw_tile_index);
TileCollision process_side_sensor(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor_dir, const int sensor);
TileCollision process_top_sensor(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor);
TileCollision process_bottom_sensor(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor);
TileCollision process_bottom_sensor_one_tile_down(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor);
void sense_tile(vec4* guy_pos_f, vec4i* tilemap_dim, vec4i* sensors, /*out*/SensedTile* result);
void draw_tilemap(struct Game* game, Tilemap* tilemap);
    /* NOTE HERE IS THE 2-PASS COLLISION THAT I DON'T USE
    vec4 half_displacement;
    half_displacement.simd = _mm_div_ps(_mm_sub_ps(s->guy.position.simd, s->guy.old_position.simd), _mm_set1_ps(2.0f));
    s->guy.position.simd = _mm_add_ps(s->guy.old_position.simd, half_displacement.simd);
    collide_character(&s->guy, &s->map->tile_collision);
    s->guy.position.simd = _mm_add_ps(s->guy.position.simd, half_displacement.simd);
    collide_character(&s->guy, &s->map->tile_collision);
    */
void collide_character(struct Character* guy, CollisionMap* tile_collision);
void slide_character(float gravity, struct Character* guy);

typedef struct ParallaxBackground {
    int x, y;
    int width, height;
    int bg_asset;
    float parallax_factor;
    int flags;
    int frame_height, frame_width;
    int frame_count;
    int frame;
} ParallaxBackground;

#define BG_WRAP_X (1 << 0)
#define BG_WRAP_Y (1 << 1)

#define DOOR_VISIBLE  (1 << 0)
#define DOOR_INVERT_Y (1 << 1)

typedef struct Door {
    int x, y;
    int dest_x, dest_y, dest_area;
    SDL_Color orb_color;
    // NOTE gotta be careful with using flags for the sake of netplay.
    byte flags;
    // NOTE gotta be careful with using callback for the sake of netplay.
    Callback callback;
} Door;

typedef struct MobSpawnRate {
    int mob_type_id;
    int percentage;
} MobSpawnRate;

typedef struct MobSpawnZone {
    int countdown_until_next_spawn_attempt;

    int x, y, width, height;
    int number_of_spawns;
    MobSpawnRate* spawns;
} MobSpawnZone;

#define MAP_STATE_MAX_SMALL_MOBS 20
#define MAP_STATE_MAX_MEDIUM_MOBS 20
#define MAP_STATE_MAX_LARGE_MOBS 2
typedef struct MapState {
    SmallMob  small_mobs[MAP_STATE_MAX_SMALL_MOBS];
    MediumMob medium_mobs[MAP_STATE_MAX_MEDIUM_MOBS];
    LargeMob  large_mobs[MAP_STATE_MAX_LARGE_MOBS];
} MapState;
#define MAP_STATE_MAX_MOBS (MAP_STATE_MAX_SMALL_MOBS + MAP_STATE_MAX_MEDIUM_MOBS + MAP_STATE_MAX_LARGE_MOBS)

typedef struct Map {
  // TODO TODO ADD GRAVITY AND DRAG - REMOVE FROM WORLDSCENE
    SDL_mutex* locked;
    struct Game* game;

    int area_id;
    int asset_id;
    CollisionMap tile_collision;

    int number_of_tilemaps;
    Tilemap* tilemaps;

    int number_of_backgrounds;
    int width, height;
    ParallaxBackground* backgrounds;

    int number_of_doors;
    Door* doors;

    int number_of_spawn_zones;
    MobSpawnZone* spawn_zones;

    MapState* state;
} Map;

typedef struct CmFileHeader {
    char magic[3];
    Uint32 tiles_wide;
    Uint32 tiles_high;
    Uint8 tilemap_count;
    Uint8 background_count;
    Uint8 door_count;
    Uint8 spawn_zone_count;
    Uint16 total_spawn_rate_count;
} CmFileHeader;

SDL_Rect src_rect_frame(int n, int image_width, int image_height, int frame_height, int frame_width);
void increment_src_rect(SDL_Rect* src, int n, int image_width, int image_height);

void world_render_copy(
    struct Game* game,
    SDL_Texture* tex, SDL_Rect* src,
    vec2* pos, int width, int height,
    vec2* center
);

void world_render_copy_ex(
    struct Game* game,
    SDL_Texture* tex, SDL_Rect* src,
    vec2* pos, int width, int height,
    float angle, vec2* center, SDL_RendererFlip flip
);

CmFileHeader read_cm_file_header(const int asset);
void load_map(const int asset, /*out*/Map* map);
void draw_map(struct Game* game, Map* map);
void draw_parallax_background(struct Game* game, struct Map* map, struct ParallaxBackground* background);
void draw_door(struct Game* game, struct Door* door);
void update_map(
    Map* map, struct Game* game,
    void* data,
    void(*after_mob_update)(void*, Map*, struct Game*, MobCommon*)
);
MobCommon* spawn_mob(Map* map, struct Game* game, int mob_type_id, vec2 pos);
void despawn_mob(Map* map, struct Game* game, MobCommon* mob);
int mob_id(Map* map, MobCommon* mob);
MobCommon* mob_from_id(Map* map, int id);
int index_from_mob_id(int id);

void clear_map_state(Map* map);
void write_map_state(Map* map, byte* buffer, int* pos);
void read_map_state(Map* map, byte* buffer, int* pos);

void collide_generic_body(struct GenericBody* body, CollisionMap* tile_collision);

#endif // TILEMAP_H
