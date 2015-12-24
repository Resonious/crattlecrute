#include "tilemap.h"
#ifdef _DEBUG
extern bool debug_pause;
#endif

TileIndex tile_from_int(int raw_tile_index) {
    TileIndex result;

    if (raw_tile_index == -1) {
        result.flags = NOT_A_TILE;
        result.index = -1;
        return result;
    }

    // ENDIAN dependent?
    result.index = raw_tile_index & 0x00FFFFFF;
    result.flags = (raw_tile_index & 0xFF000000) >> 24;
    return result;
}

TileIndex tile_at(CollisionMap* tilemap, vec4i* tilespace, const int sensor) {
    int raw_tile_index = tilemap->tiles[tilespace->x[sensor + 1] * tilemap->width + tilespace->x[sensor]];
    return tile_from_int(raw_tile_index);
}

int* tile_height_for_sensor(TileHeights* all_heights, TileIndex* tile_index, const int sensor_dir) {
  // ((int*)(&heights[tile_index.index]) + (sensor_dir * 32))
    // Assumes LEFT and RIGHT sensors are higher numbers than TOP_SENSOR
    int offset_to_use = sensor_dir;
    if (sensor_dir > TOP_SENSOR) {
        if (tile_index->flags & TILE_FLIP_X) {
            // At this point, it's either left or right
            offset_to_use = sensor_dir == LEFT_SENSOR ? RIGHT_SENSOR : LEFT_SENSOR;
        }
    }
    return (int*)(&all_heights[tile_index->index]) + (offset_to_use * 32);
}

void sense_tile(vec4* guy_pos_f, vec4i* tilemap_dim, vec4i* sensors, /*out*/SensedTile* result) {
    // Player position twice (x,y,x,y)
    vec4i guy_pos;
    guy_pos.simd = _mm_shuffle_epi32(_mm_cvtps_epi32(guy_pos_f->simd), _MM_SHUFFLE(1, 0, 1, 0));

    // absolute sensor position
    vec4i sense; // = guy_pos + guy.left_sensor
    sense.simd = _mm_add_epi32(guy_pos.simd, sensors->simd);
    // x/y index into tilemap // = (int)((float)sense / 32.0f)
    result->tilespace.simd = _mm_cvtps_epi32(_mm_div_ps(_mm_cvtepi32_ps(sense.simd), _mm_set_ps1(32.0f)));
    // bottom left corner of the tile // = tilespace * 32
    result->tilepos.simd = _mm_mul_epi32_x4(result->tilespace.simd, _mm_set1_epi32(32));
    // Position within the tile (valid or otherwise)
    result->position_within_tile.simd = _mm_sub_epi32(sense.simd, result->tilepos.simd);
    // Check the tilespaces to see if they're valid
    result->indices_are_valid.simd = _mm_and_si128(
        _mm_cmpgt_epi32(result->tilespace.simd, _mm_set1_epi32(-1)),
        _mm_cmplt_epi32(result->tilespace.simd, tilemap_dim->simd)
    );
}

TileCollision process_bottom_sensor_one_tile_down(Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor) {
    TileCollision result;
    result.hit = false;
    result.new_position = guy->position.x[Y];

    int new_tilespace_y = t->tilespace.x[sensor+Y] - 1;
    // Make sure "one tile down" is in fact a valid tile index..
    if (new_tilespace_y >= 0 && new_tilespace_y < tilemap->height) {
        t->tilespace.x[sensor + Y] = new_tilespace_y;
        t->tilepos.x[sensor + Y] -= 32;

        int* heights = NULL;
        TileIndex tile_index = tile_at(tilemap, &t->tilespace, sensor);
        if (!(tile_index.flags & NOT_A_TILE)) {
            heights = COLLISION_TERRAIN_TESTGROUND2[tile_index.index].top2down;
            int x_within_tile = t->position_within_tile.x[sensor+X];
            int height = heights[(tile_index.flags & TILE_FLIP_X) ? 32 - x_within_tile : x_within_tile];
            if (height < 0)
                return result;

            // Just assume that we want to be placed here (this function should only be called when grounded)
            result.hit = true;
            result.new_position = t->tilepos.x[sensor+Y] + height - guy->bottom_sensors.x[sensor+Y];
        }
    }

    return result;
}

TileCollision process_bottom_sensor(Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor) {
    TileCollision result;
    result.hit = false;
    result.new_position = guy->position.x[Y];

    if (t->indices_are_valid.x[sensor+X] && t->indices_are_valid.x[sensor+Y]) {
        TileIndex tile_index = tile_at(tilemap, &t->tilespace, sensor);
        // This would mean we're in the air!
        if (tile_index.flags & NOT_A_TILE) {
            if (guy->grounded)
                return process_bottom_sensor_one_tile_down(guy, tilemap, t, sensor);
            else
                return result;
        }

        int* heights = COLLISION_TERRAIN_TESTGROUND2[tile_index.index].top2down;
        int x_within_tile = t->position_within_tile.x[sensor+X];
        bool tile_x_flipped = tile_index.flags & TILE_FLIP_X;

        int height = heights[tile_x_flipped ? 32 - x_within_tile : x_within_tile];

        // This would also mean we're in the air
        if (height == -1 && guy->grounded)
            return process_bottom_sensor_one_tile_down(guy, tilemap, t, sensor);
        // Try next tile up
        else if (height == 32) {
            int new_tilespace_y = t->tilespace.x[sensor+Y] + 1;
            // Make sure "one tile up" is in fact a valid tile index..
            if (new_tilespace_y >= 0 && new_tilespace_y < tilemap->height) {
                t->tilespace.x[sensor+Y] = new_tilespace_y;
                t->tilepos.x[sensor+Y] += 32;

                tile_index = tile_at(tilemap, &t->tilespace, sensor);
                if (!(tile_index.flags & NOT_A_TILE)) {
                    heights = COLLISION_TERRAIN_TESTGROUND2[tile_index.index].top2down;

                    int new_height = heights[tile_x_flipped ? 32 - x_within_tile : x_within_tile];
                    if (new_height >= 0)
                        height = new_height;
                    else
                        t->tilepos.x[sensor+Y] -= 32;
                }
                else
                    t->tilepos.x[sensor+Y] -= 32;
            }
        }

        if (height >= 0) {
            int y_placement = t->tilepos.x[sensor+Y] + height - guy->bottom_sensors.x[sensor+Y];
            if (y_placement > guy->position.x[Y] || guy->grounded) {
                result.hit = true;
                result.new_position = (float)y_placement;
            }
        }
    }

    return result;
}

TileCollision dont_call_me(CollisionMap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    // This should never happen!!!
#ifdef _DEBUG
    SDL_assert(false);
#endif
}
TileCollision left_sensor_placement(CollisionMap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + height + 1 - guy->left_sensors.x[sensor+X]);
    result.hit = result.new_position > guy->position.x[X];
    return result;
}
TileCollision right_sensor_placement(CollisionMap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + 32 - height - guy->right_sensors.x[sensor+X]);
    result.hit = result.new_position < guy->position.x[X];
    return result;
}
TileCollision top_sensor_placement(CollisionMap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+Y] + height - 1 - guy->top_sensors.x[sensor+Y]);
    result.hit = result.new_position < guy->position.x[Y];
    return result;
}

// NOTE the functions in here should line up with BOTTOM_SENSOR, TOP_SENSOR, RIGHT_SENSOR, and LEFT_SENSOR.
const static TileCollision(*placement_functions[])(CollisionMap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) = {
    dont_call_me, // Bottom sensors are special case
    top_sensor_placement,
    right_sensor_placement,
    left_sensor_placement
};

TileCollision process_sensor(Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor_dir, const int sensor, const int dim) {
    if (t->indices_are_valid.x[sensor + X] && t->indices_are_valid.x[sensor + Y]) {

        TileIndex tile_index = tile_at(tilemap, &t->tilespace, sensor);

        if (!(tile_index.flags & NOT_A_TILE)) {
            int* heights = tile_height_for_sensor(COLLISION_TERRAIN_TESTGROUND2, &tile_index, sensor_dir);
#ifdef _DEBUG
            // Make sure tile_height_for_sensor is working correctly.
            if (sensor_dir == LEFT_SENSOR)
                if (tile_index.flags & TILE_FLIP_X)
                    SDL_assert(heights == COLLISION_TERRAIN_TESTGROUND2[tile_index.index].right2left);
                else
                    SDL_assert(heights == COLLISION_TERRAIN_TESTGROUND2[tile_index.index].left2right);
            else if (sensor_dir == RIGHT_SENSOR)
                if (tile_index.flags & TILE_FLIP_X)
                    SDL_assert(heights == COLLISION_TERRAIN_TESTGROUND2[tile_index.index].left2right);
                else
                    SDL_assert(heights == COLLISION_TERRAIN_TESTGROUND2[tile_index.index].right2left);
            else if (sensor_dir == TOP_SENSOR)
                SDL_assert(heights == COLLISION_TERRAIN_TESTGROUND2[tile_index.index].bottom2up);
            else
                SDL_assert(false);

            // Sanity
            if      (dim == X) SDL_assert(!dim == Y);
            else if (dim == Y) SDL_assert(!dim == X);
#endif
            int within_tile = t->position_within_tile.x[sensor + !dim];
            int height;
            if (sensor_dir == TOP_SENSOR)
                height = heights[(tile_index.flags & TILE_FLIP_X) ? 32 - within_tile : within_tile];
            else
                height = heights[within_tile];

            if (height >= 0) {
                return placement_functions[sensor_dir](tilemap, t, guy, height, sensor);
            }
        }
    }

    TileCollision result;
    result.hit = false;
    result.new_position = guy->position.x[dim];

    return result;
}


static void increment_tilespace(vec4i* tilespace, int width, int increment_by) {
    // Increment
    tilespace->x[X] += increment_by;
    if (tilespace->x[X] >= width) {
        tilespace->x[X] = 0;
        tilespace->x[Y] += increment_by;
    }
}

static SDL_Rect tile_src_rect(TileIndex* tile_index, Tilemap* map) {
    SDL_Rect src = { tile_index->index, 0, 32, 32 };
    // OPTIMIZE out this while loop with multiplication lol?
    while (src.x >= map->tiles_per_row) {
        src.x -= map->tiles_per_row;
        src.y += 1;
    }
    src.x *= 32;
    src.y *= 32;
    return src;
}

// NOTE dest.y is in GAME coordinates (0 bottom), src.y is in IMAGE coordinates (0 top)
static void draw_tile(Game* game, Tilemap* tilemap, TileIndex* tile_index, SDL_Rect* src, SDL_Rect* dest) {
    int old_dest_y = dest->y;
    dest->y = game->window_height - dest->y - 32;

    SDL_RendererFlip flip = (tile_index->flags & TILE_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(game->renderer, tilemap->tex, src, dest, 0, NULL, flip);

    dest->y = old_dest_y;
}


// ====== Keeping this around for posterity. I might want to implement it again but better. =========
// DEBUG: highlight tiles that sensors cover
/*
#ifdef _DEBUG
static void draw_debug_borders(Game* game, SDL_Rect* dest, int i, int j) {
    SDL_Rect tile_rect;
    SDL_memcpy(&tile_rect, dest, sizeof(SDL_Rect));

    tile_rect.y = game->window_height - dest->y - 32;
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(game->renderer, &r, &b, &g, &a);
    if (
        i == l1_tilespace_x && j == l1_tilespace_y
        ||
        i == l2_tilespace_x && j == l2_tilespace_y
        ) {
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 50, 128);
        SDL_RenderDrawRect(game->renderer, &tile_rect);
    }
    if (
        i == r1_tilespace_x && j == r1_tilespace_y
        ||
        i == r2_tilespace_x && j == r2_tilespace_y
        ) {
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 50, 128);
        SDL_RenderDrawRect(game->renderer, &tile_rect);
    }
    if (
        i == b1_tilespace_x && j == b1_tilespace_y
        ||
        i == b2_tilespace_x && j == b2_tilespace_y
        ) {
        // smaller rect so that we can see both
        tile_rect.x += 1;
        tile_rect.y += 1;
        tile_rect.h -= 2;
        tile_rect.w -= 2;
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 255, 128);
        SDL_RenderDrawRect(game->renderer, &tile_rect);
    }

    SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
}
#endif
*/

void draw_tilemap(Game* game, Tilemap* tilemap) {
    int i = 0;
    vec4i dest;
    dest.simd = _mm_set_epi32(32, 32, 0, 0);
    const int width_in_pixels = tilemap->width * 32;
    const int height_in_pixels = tilemap->height * 32;
    int* tile_data = tilemap->tiles;

    while (dest.x[Y] < height_in_pixels) {
        int tile_count = tile_data[i];

        if (tile_count > 0) {
            // Repetition
            i += 1;

            TileIndex repeated_index = tile_from_int(tile_data[i]);
            SDL_Rect src = tile_src_rect(&repeated_index, tilemap);

            // Draw the tile `tile_count` times
            for (int j = 0; j < tile_count; j++) {
                draw_tile(game, tilemap, &repeated_index, &src, &dest.rect);
                increment_tilespace(&dest, width_in_pixels, 32);
            }

            i += 1;
        }
        else {
            // Alternation
            tile_count = -tile_count;
            i += 1;

            for (int j = 0; j < tile_count; j++, i++) {
                TileIndex tile_index = tile_from_int(tile_data[i]);
                SDL_Rect src = tile_src_rect(&tile_index, tilemap);
                draw_tile(game, tilemap, &tile_index, &src, &dest.rect);
                increment_tilespace(&dest, width_in_pixels, 32);
            }
        }// if (tile_count > 0)
    }// while (dest.y < height)
}// block for render
