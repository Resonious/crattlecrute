#include "tilemap.h"
#include "assets.h"
#include "coords.h"
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
            // TODO TODO RIGHT HERE NOOB!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
            heights = tilemap->heights[tile_index.index].top2down;
            int x_within_tile = t->position_within_tile.x[sensor+X];
            int height = heights[(tile_index.flags & TILE_FLIP_X) ? 31 - x_within_tile : x_within_tile];
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

        int* heights = tilemap->heights[tile_index.index].top2down;
        int x_within_tile = t->position_within_tile.x[sensor+X];
        bool tile_x_flipped = tile_index.flags & TILE_FLIP_X;

        int height = heights[tile_x_flipped ? 31 - x_within_tile : x_within_tile];

        // This would also mean we're in the air
        if (height == -1 && guy->grounded)
            return process_bottom_sensor_one_tile_down(guy, tilemap, t, sensor);
        // Try next tile up
        else if (height == 31) {
            int new_tilespace_y = t->tilespace.x[sensor+Y] + 1;
            // Make sure "one tile up" is in fact a valid tile index..
            if (new_tilespace_y >= 0 && new_tilespace_y < tilemap->height) {
                t->tilespace.x[sensor+Y] = new_tilespace_y;
                t->tilepos.x[sensor+Y] += 32;

                tile_index = tile_at(tilemap, &t->tilespace, sensor);
                if (!(tile_index.flags & NOT_A_TILE)) {
                    heights = tilemap->heights[tile_index.index].top2down;

                    tile_x_flipped = tile_index.flags & TILE_FLIP_X;
                    int new_height = heights[tile_x_flipped ? 31 - x_within_tile : x_within_tile];
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
    TileCollision r;
    return r;
}
TileCollision left_sensor_placement(CollisionMap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + height + 1 - guy->left_sensors.x[sensor+X]);
    result.hit = result.new_position > guy->position.x[X];
    return result;
}
TileCollision right_sensor_placement(CollisionMap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + 32 - height - 1 - guy->right_sensors.x[sensor+X]);
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

TileCollision process_top_sensor(Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor) {
    if (t->indices_are_valid.x[sensor + X] && t->indices_are_valid.x[sensor + Y]) {

        TileIndex tile_index = tile_at(tilemap, &t->tilespace, sensor);

        if (!(tile_index.flags & NOT_A_TILE)) {
            int* heights = tile_height_for_sensor(tilemap->heights, &tile_index, TOP_SENSOR);
#ifdef _DEBUG
            SDL_assert(heights == tilemap->heights[tile_index.index].bottom2up);
#endif
            int x_within_tile = t->position_within_tile.x[sensor + X];
            SDL_assert(x_within_tile < 32);
            int height;
            height = heights[(tile_index.flags & TILE_FLIP_X) ? 31 - x_within_tile : x_within_tile];

            // Move over one tile if the tile we hit's height is the bottom
            if (height == 0) {
                int new_tilespace_y = t->tilespace.x[sensor + Y];
                new_tilespace_y -= 1;

                // Don't overflow tilemap
                if (new_tilespace_y >= 0) {
                    t->tilespace.x[sensor + Y] = new_tilespace_y;
                    t->tilepos.x[sensor + Y] -= 32;

                    tile_index = tile_at(tilemap, &t->tilespace, sensor);
                    // Don't bump to next tile if next tile is not a tile
                    if (!(tile_index.flags & NOT_A_TILE)) {
                        heights = tile_height_for_sensor(tilemap->heights, &tile_index, TOP_SENSOR);

                        int new_height;
                        new_height = heights[(tile_index.flags & TILE_FLIP_X) ? 31 - x_within_tile : x_within_tile];

                        if (new_height >= 0)
                            height = new_height;
                        else
                            t->tilepos.x[sensor + Y] += 32;
                    }
                    else
                        t->tilepos.x[sensor + Y] += 32;
                }
            }

            if (height >= 0) {
                return placement_functions[TOP_SENSOR](tilemap, t, guy, height, sensor);
            }
        }
    }

    TileCollision result;
    result.hit = false;
    result.new_position = guy->position.x[Y];

    return result;
}

TileCollision process_side_sensor(Character* guy, CollisionMap* tilemap, SensedTile* t, const int sensor_dir, const int sensor) {
    if (t->indices_are_valid.x[sensor + X] && t->indices_are_valid.x[sensor + Y]) {

        TileIndex tile_index = tile_at(tilemap, &t->tilespace, sensor);

        if (!(tile_index.flags & NOT_A_TILE)) {
            int* heights = tile_height_for_sensor(tilemap->heights, &tile_index, sensor_dir);
#ifdef _DEBUG
            SDL_assert(sensor_dir != TOP_SENSOR);
            // Make sure tile_height_for_sensor is working correctly.
            if (sensor_dir == LEFT_SENSOR)
                if (tile_index.flags & TILE_FLIP_X)
                    SDL_assert(heights == tilemap->heights[tile_index.index].right2left);
                else
                    SDL_assert(heights == tilemap->heights[tile_index.index].left2right);
            else if (sensor_dir == RIGHT_SENSOR)
                if (tile_index.flags & TILE_FLIP_X)
                    SDL_assert(heights == tilemap->heights[tile_index.index].left2right);
                else
                    SDL_assert(heights == tilemap->heights[tile_index.index].right2left);
            else
                SDL_assert(false);
#endif
            int y_within_tile = t->position_within_tile.x[sensor + Y];
            SDL_assert(y_within_tile < 32);
            int height;
            height = heights[y_within_tile];

            // Move over one tile if we're max height
            if (height == 31) {
                const int tilepos_offset_amount = sensor_dir == LEFT_SENSOR ? 32 : -32;

                int new_tilespace_dim = t->tilespace.x[sensor + X];
                new_tilespace_dim += (sensor_dir == LEFT_SENSOR ? 1 : -1);

                // Don't overflow tilemap
                if (new_tilespace_dim < tilemap->width) {
                    t->tilespace.x[sensor + X] = new_tilespace_dim;
                    t->tilepos.x[sensor + X] += tilepos_offset_amount;

                    tile_index = tile_at(tilemap, &t->tilespace, sensor);
                    // Don't bump to next tile if next tile is not a tile
                    if (!(tile_index.flags & NOT_A_TILE)) {
                        heights = tile_height_for_sensor(tilemap->heights, &tile_index, sensor_dir);

                        int new_height = heights[y_within_tile];

                        if (new_height >= 0)
                            height = new_height;
                        else
                            t->tilepos.x[sensor + X] -= tilepos_offset_amount;
                    }
                    else
                        t->tilepos.x[sensor + X] -= tilepos_offset_amount;
                }
            }

            if (height >= 0) {
                return placement_functions[sensor_dir](tilemap, t, guy, height, sensor);
            }
        }
    }

    TileCollision result;
    result.hit = false;
    result.new_position = guy->position.x[X];

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
// This also offsets by game->camera
static void draw_tile(Game* game, Tilemap* tilemap, TileIndex* tile_index, SDL_Rect* src, SDL_Rect* dest) {
    if (tile_index->flags & NOT_A_TILE) return;

    vec4i new_dest;
    SDL_memcpy(&new_dest, dest, sizeof(SDL_Rect));
    new_dest.simd = _mm_sub_epi32(new_dest.simd, _mm_cvtps_epi32(game->camera.simd));
    new_dest.rect.y = game->window_height - new_dest.rect.y - 32;

    SDL_RendererFlip flip = (tile_index->flags & TILE_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(game->renderer, tilemap->tex, src, &new_dest, 0, NULL, flip);
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
        i += 1;

        if (tile_count > 0) {
            // Repetition
            TileIndex repeated_index = tile_from_int(tile_data[i]);
            SDL_Rect src = tile_src_rect(&repeated_index, tilemap);

            // Draw the tile `tile_count` times
            for (int j = 0; j < tile_count; j++) {
                draw_tile(game, tilemap, &repeated_index, &src, &dest.rect);
                increment_tilespace(&dest, width_in_pixels, 32);
                // NOTE this could be optimized by not repeatedly calling increment_tilespace
                // on repeated -1's.
            }

            i += 1;
        }
        else {
            // Alternation
            tile_count = -tile_count;

            for (int j = 0; j < tile_count; j++, i++) {
                TileIndex tile_index = tile_from_int(tile_data[i]);
                SDL_Rect src = tile_src_rect(&tile_index, tilemap);
                draw_tile(game, tilemap, &tile_index, &src, &dest.rect);
                increment_tilespace(&dest, width_in_pixels, 32);
            }
        }// if (tile_count > 0)
    }// while (dest.y < height)
}

void draw_map(Game* game, Map* map) {
    for (int i = 0; i < map->number_of_tilemaps; i++) {
        draw_tilemap(game, &map->tilemaps[i]);
    }
}

void collide_character(Character* guy, CollisionMap* tile_collision) {
    // === These are needed for all sensor operations ===
    // tilemap dimensions twice (w,h,w,h)
    vec4i tilemap_dim;
    tilemap_dim.simd = _mm_set_epi32(
        tile_collision->height, tile_collision->width,
        tile_collision->height, tile_collision->width
    );

    vec4 guy_new_x_position;
    guy_new_x_position.x[X] = guy->position.x[X];
    guy_new_x_position.x[Y] = guy->old_position.x[Y];
    vec4 guy_new_y_position;
    guy_new_y_position.x[X] = guy->old_position.x[X];
    guy_new_y_position.x[Y] = guy->position.x[Y];

    SensedTile t;
    bool left_hit, right_hit, top_hit;
    // NOTE don't access these if the corrosponding *_hit == false
    TileCollision l_collision_1, l_collision_2,
                  r_collision_1, r_collision_2,
                  t_collision_1, t_collision_2;
    // == LEFT SENSORS ==
    if (guy_new_x_position.x[X] < guy->old_position.x[X]) {
        sense_tile(&guy_new_x_position, &tilemap_dim, &guy->left_sensors, &t);
        l_collision_1 = process_side_sensor(guy, tile_collision, &t, LEFT_SENSOR, SENSOR_1);
        l_collision_2 = process_side_sensor(guy, tile_collision, &t, LEFT_SENSOR, SENSOR_2);
        left_hit = l_collision_1.hit || l_collision_2.hit;
    }
    else
        left_hit = false;

    // == RIGHT SENSORS ==
    if (guy_new_x_position.x[X] > guy->old_position.x[X]) {
        sense_tile(&guy_new_x_position, &tilemap_dim, &guy->right_sensors, &t);
        r_collision_1 = process_side_sensor(guy, tile_collision, &t, RIGHT_SENSOR, SENSOR_1);
        r_collision_2 = process_side_sensor(guy, tile_collision, &t, RIGHT_SENSOR, SENSOR_2);
        right_hit = r_collision_1.hit || r_collision_2.hit;
    }
    else
        right_hit = false;

    // == TOP SENSORS ==
    if (guy_new_y_position.x[Y] > guy->old_position.x[Y]) {
        sense_tile(&guy_new_y_position, &tilemap_dim, &guy->top_sensors, &t);
        t_collision_1 = process_top_sensor(guy, tile_collision, &t, SENSOR_1);
        t_collision_2 = process_top_sensor(guy, tile_collision, &t, SENSOR_2);
        top_hit = t_collision_1.hit || t_collision_2.hit;
    }
    else
        top_hit = false;

    if (left_hit)
        guy->position.x[X] = fmaxf(l_collision_1.new_position, l_collision_2.new_position);
    if (right_hit)
        guy->position.x[X] = fminf(r_collision_1.new_position, r_collision_2.new_position);
    if (top_hit) {
        guy->position.x[Y] = fminf(t_collision_1.new_position, t_collision_2.new_position);
        guy->dy = 0;
    }

    // == BOTTOM SENSORS ==
    sense_tile(&guy->position, &tilemap_dim, &guy->bottom_sensors, &t);
    TileCollision b_collision_1 = process_bottom_sensor(guy, tile_collision, &t, SENSOR_1);
    TileCollision b_collision_2 = process_bottom_sensor(guy, tile_collision, &t, SENSOR_2);

    guy->grounded = b_collision_1.hit || b_collision_2.hit;
    if (guy->grounded)
        guy->dy = 0;
    if (b_collision_1.hit && b_collision_2.hit) {
        guy->position.x[Y] = fmaxf(b_collision_1.new_position, b_collision_2.new_position);
        guy->ground_angle = atan2f(
            b_collision_2.new_position - b_collision_1.new_position,
            guy->bottom_sensors.x[S2X] - guy->bottom_sensors.x[S1X]
        ) / M_PI * 180;

        const float ground_angle_cap = 30;
        if (guy->ground_angle > ground_angle_cap)
            guy->ground_angle = ground_angle_cap;
        else if (guy->ground_angle < -ground_angle_cap)
            guy->ground_angle = -ground_angle_cap;
    }
    else if (b_collision_1.hit)
        guy->position.x[Y] = b_collision_1.new_position;
    else if (b_collision_2.hit)
        guy->position.x[Y] = b_collision_2.new_position;
    else
        guy->grounded = false;
}