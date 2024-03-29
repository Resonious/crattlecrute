#include "tilemap.h"
#include "character.h"
#include "game.h"
#include "assets.h"
#include "coords.h"
#include "mob.h"
#ifdef _DEBUG
extern bool debug_pause;
#endif
#include <math.h>

void set_collision_sensors(struct GenericBody* body, float width, float height, float y_offset) {
    float hw = width  / 2.0f;
    float hh = height / 2.0f;
    body->top_sensors.x[S1X] = -hw;
    body->top_sensors.x[S1Y] = hh + y_offset;
    body->top_sensors.x[S2X] = hw;
    body->top_sensors.x[S2Y] = hh + y_offset;

    body->bottom_sensors.x[S1X] = -hw;
    body->bottom_sensors.x[S1Y] = -hh + y_offset;
    body->bottom_sensors.x[S2X] = hw;
    body->bottom_sensors.x[S2Y] = -hh + y_offset;

    body->left_sensors.x[S1X] = -hw - 1;
    body->left_sensors.x[S1Y] = hh + y_offset;
    body->left_sensors.x[S2X] = -hw - 1;
    body->left_sensors.x[S2Y] = -hh + y_offset;

    body->right_sensors.x[S1X] = hw + 1;
    body->right_sensors.x[S1Y] = hh + y_offset;
    body->right_sensors.x[S2X] = hw + 1;
    body->right_sensors.x[S2Y] = -hh + y_offset;
}

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

void world_render_copy_ex(
    struct Game* game,
    SDL_Texture* tex, SDL_Rect* src,
    vec2* pos, int width, int height,
    float angle, vec2* center, SDL_RendererFlip flip
) {
    SDL_Rect dest = {
        pos->x - center->x - game->camera.x[X],
        game->window_height - (pos->y - game->camera.x[Y]) - height + center->y,

        width, height
    };
    SDL_Point center_point = { (int)center->x, height - (int)center->y };
    SDL_RenderCopyEx(game->renderer, tex, src, &dest, 360 - angle, &center_point, flip);
}

void world_render_copy(
    struct Game* game,
    SDL_Texture* tex, SDL_Rect* src,
    vec2* pos, int width, int height,
    vec2* center
) {
    vec2 backup_center = { 0, 0 };
    if (center == NULL) {
        center = &backup_center;
    }

    SDL_Rect dest = {
        pos->x - center->x - game->camera.x[X],
        game->window_height - (pos->y - game->camera.x[Y]) - height + center->y,

        width, height
    };
    SDL_Point center_point = { (int)center->x, height - (int)center->y };
    SDL_RenderCopy(game->renderer, tex, src, &dest);
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

TileCollision process_bottom_sensor_one_tile_down(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor) {
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
            heights = tilemap->heights[tile_index.index].top2down;
            int x_within_tile = t->position_within_tile.x[sensor+X];
            int height = heights[(tile_index.flags & TILE_FLIP_X) ? 31 - x_within_tile : x_within_tile];
            if (height < 0)
                return result;

            // Just assume that we want to be placed here (this function should only be called when grounded)
            result.hit = true;
            result.new_position = (float)(t->tilepos.x[sensor+Y] + height - guy->bottom_sensors.x[sensor+Y]);
            // Don't bother if it's > 16 pixels high of a slope...
            if (abs(result.new_position - guy->position.x[Y]) > 16)
                result.hit = false;
        }
    }

    return result;
}

TileCollision process_bottom_sensor(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor) {
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

        // This would also mean we're in the air ("we" being the sensor; not neccesarily the character)
        if (height == -1 && guy->grounded)
            return process_bottom_sensor_one_tile_down(guy, tilemap, t, sensor);

        // Try next tile up
        if (height >= 31) {
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
                    if (new_height >= 0) {
                        height = new_height;

                        // Checking 2 tiles up for bottom sensor is absurd.
                        if (height >= 31) {
                            result.hit = true;
                            result.new_position = guy->position.x[Y];
                            return result;
                        }
                    }
                    else
                        t->tilepos.x[sensor+Y] -= 32;
                }
                else
                    t->tilepos.x[sensor+Y] -= 32;
            }
        }

        if (height >= 0) {
            int y_placement = t->tilepos.x[sensor+Y] + height - guy->bottom_sensors.x[sensor+Y];
            if ((y_placement >= guy->position.x[Y] && y_placement <= guy->old_position.x[Y]) || guy->grounded) {
                result.hit = true;
                result.new_position = (float)y_placement;
            }
        }
    }

    return result;
}

TileCollision dont_call_me(CollisionMap* tilemap, SensedTile* t, GenericBody* guy, int height, const int sensor) {
    // This should never happen!!!
#ifdef _DEBUG
    SDL_assert(false);
#endif
    TileCollision r = {false, 0};
    return r;
}
TileCollision left_sensor_placement(CollisionMap* tilemap, SensedTile* t, GenericBody* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + height + 1 - guy->left_sensors.x[sensor+X]);
    result.hit = result.new_position >= guy->position.x[X] && (result.new_position <= guy->old_position.x[X]);
    return result;
}
TileCollision right_sensor_placement(CollisionMap* tilemap, SensedTile* t, GenericBody* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + 31 - height - guy->right_sensors.x[sensor+X]);
    result.hit = result.new_position <= guy->position.x[X] && (result.new_position >= guy->old_position.x[X]);
    return result;
}
TileCollision top_sensor_placement(CollisionMap* tilemap, SensedTile* t, GenericBody* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+Y] + height - 1 - guy->top_sensors.x[sensor+Y]);
    result.hit = result.new_position < guy->position.x[Y];// && result.new_position >= guy->old_position.x[Y];
    return result;
}

// NOTE the functions in here should line up with BOTTOM_SENSOR, TOP_SENSOR, RIGHT_SENSOR, and LEFT_SENSOR.
const static TileCollision(*placement_functions[])(CollisionMap* tilemap, SensedTile* t, GenericBody* guy, int height, const int sensor) = {
    (void*)dont_call_me, // Bottom sensors are special case
    (void*)top_sensor_placement,
    (void*)right_sensor_placement,
    (void*)left_sensor_placement
};

TileCollision process_top_sensor(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor) {
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

TileCollision process_side_sensor(GenericBody* guy, CollisionMap* tilemap, SensedTile* t, const int sensor_dir, const int sensor) {
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
            if (height >= 31) {
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
static void draw_tile(struct Game* game, Tilemap* tilemap, TileIndex* tile_index, SDL_Rect* src, SDL_Rect* dest) {
    if (tile_index->flags & NOT_A_TILE) return;

    vec4i new_dest;
    SDL_memcpy(&new_dest, dest, sizeof(SDL_Rect));
    new_dest.simd = _mm_sub_epi32(new_dest.simd, _mm_cvtps_epi32(game->camera.simd));
    new_dest.rect.y = (int)game->window_height - new_dest.rect.y - 32;

    SDL_RendererFlip flip = (tile_index->flags & TILE_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(game->renderer, tilemap->tex, src, &new_dest.rect, 0, NULL, flip);
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

void draw_door(struct Game* game, struct Door* door) {
    if (!(door->flags & DOOR_VISIBLE))
        return;
    SDL_Texture* tex = cached_texture(game, ASSET_MISC_DOOR_PNG);
    SDL_Rect src = { 0, 90, 90, 90 };
    vec2 pos = { (float)door->x, (float)door->y };
    world_render_copy(game, tex, &src, &pos, 90, 90, NULL);
    src.x += 90;
    world_render_copy(game, tex, &src, &pos, 90, 90, NULL);
    src.x = 0;
    src.y = 0;
    world_render_copy(game, tex, &src, &pos, 90, 90, NULL);

    if (door->orb_color.a != 0) {
        src.x = 90;
        Uint8 r, g, b, a;
        SDL_GetTextureColorMod(tex, &r, &g, &b);
        SDL_GetTextureAlphaMod(tex, &a);

        SDL_SetTextureColorMod(tex, door->orb_color.r, door->orb_color.g, door->orb_color.b);
        SDL_SetTextureAlphaMod(tex, door->orb_color.a);
        world_render_copy(game, tex, &src, &pos, 90, 90, NULL);
        SDL_SetTextureColorMod(tex, r, g, b);
        SDL_SetTextureAlphaMod(tex, a);
    }
}

void draw_parallax_background(struct Game* game, struct Map* map, struct ParallaxBackground* background) {
    SDL_Texture* texture = cached_texture(game, background->bg_asset);
    bool need_src = !(background->frame_width == 0 || background->frame_height == 0);
    SDL_Rect src;
    int real_width, real_height;

    if (need_src) {
        real_width = background->frame_width;
        real_height = background->frame_height;

        src.x = background->frame * background->frame_width;
        src.y = background->height - background->frame_height;
        src.w = background->frame_width;
        src.h = background->frame_height;

        while (src.x >= background->width) {
            src.x -= background->width;
            src.y += background->frame_height;
        }
    }
    else {
        real_width = background->width;
        real_height = background->height;
    }

    SDL_Rect dest = {
        background->x - game->camera.x[X] * background->parallax_factor,
        game->window_height - ((map->height - background->y) - game->camera.x[Y] * background->parallax_factor),

        real_width, real_height
    };

    SDL_Rect* src_ptr = need_src ? &src : NULL;
    bool wrap_x = background->flags & BG_WRAP_X;
    bool wrap_y = background->flags & BG_WRAP_Y;

    if (wrap_x && wrap_y) {
        while (dest.x + dest.w > 0)
            dest.x -= dest.w;
        while (dest.y + dest.h > 0)
            dest.y -= dest.h;

        int original_x = dest.x;

        while (dest.y < game->window_height) {
            dest.y += dest.h;
            while (dest.x < game->window_width) {
                dest.x += dest.w;
                SDL_RenderCopy(game->renderer, texture, src_ptr, &dest);
            }
            dest.x = original_x;
        }
    }
    else {
        SDL_RenderCopy(game->renderer, texture, src_ptr, &dest);

        // TODO make these render a whole square (not a cross)
        if (wrap_x) {
            SDL_Rect side_dest = dest;
            while (side_dest.x < game->window_width) {
                side_dest.x += side_dest.w;
                SDL_RenderCopy(game->renderer, texture, src_ptr, &side_dest);
            }
            side_dest.x = dest.x;
            while (side_dest.x + side_dest.w > game->window_width) {
                side_dest.x -= side_dest.w;
                SDL_RenderCopy(game->renderer, texture, src_ptr, &side_dest);
            }
        }
        else if (wrap_y) {
            SDL_Rect other_dest = dest;
            while (other_dest.y < game->window_height) {
                other_dest.y += other_dest.h;
                SDL_RenderCopy(game->renderer, texture, src_ptr, &other_dest);
            }
            other_dest.y = dest.y;
            while (other_dest.y + other_dest.h > game->window_height) {
                other_dest.y -= other_dest.h;
                SDL_RenderCopy(game->renderer, texture, src_ptr, &other_dest);
            }
        }
    }
}

void draw_tilemap(struct Game* game, Tilemap* tilemap) {
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
                // NOTE OPTIMIZE this could be optimized by not repeatedly calling increment_tilespace
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

SDL_Rect src_rect_frame(int n, int image_width, int image_height, int frame_height, int frame_width) {
    SDL_Rect src = {
        0, image_height - frame_height,
        frame_width, frame_height
    };
    if (n > 0)
        increment_src_rect(&src, n, image_width, image_height);
    return src;
}

void increment_src_rect(SDL_Rect* src, int n, int image_width, int image_height) {
    src->x += src->w * n;
    while (src->x >= image_width) {
        src->x -= image_width;
        src->y -= src->h;
    }
}

void draw_map(struct Game* game, Map* map) {
    wait_for_then_use_lock(map->locked);

    for (int i = 0; i < map->number_of_backgrounds; i++)
        draw_parallax_background(game, map, &map->backgrounds[i]);
    for (int i = 0; i < map->number_of_tilemaps; i++)
        draw_tilemap(game, &map->tilemaps[i]);
    for (int i = 0; i < map->number_of_doors; i++)
        draw_door(game, &map->doors[i]);

    if (game->window_width > map->width) {
        int width_diff = (game->window_width - map->width) / 2;
        SDL_Rect side_borders[2] = {
            {
                0, 0,
                width_diff, game->window_height
            },
            {
                map->width + width_diff, 0,
                width_diff, game->window_height
            },
        };

        SDL_SetRenderDrawColor(game->renderer, 0, 0, 0, 255);
        SDL_RenderFillRects(game->renderer, side_borders, 2);
    }
    if (game->window_height > map->height) {
        int height_diff = (game->window_height - map->height) / 2;
        SDL_Rect topbot_borders[2] = {
            {
                0, 0,
                game->window_width, height_diff
            },
            {
                0, game->window_height - height_diff - 1,
                game->window_width, height_diff
            },
        };

        SDL_SetRenderDrawColor(game->renderer, 0, 0, 0, 255);
        SDL_RenderFillRects(game->renderer, topbot_borders, 2);
    }

    for (int i = 0; i < MAP_STATE_MAX_SMALL_MOBS; i++) {
        MobCommon* mob = (MobCommon*)&map->state->small_mobs[i];
        if (mob->mob_type_id != -1) {
            SDL_assert(mob->mob_type_id >= 0);
            mob_registry[mob->mob_type_id].render(mob, game, map);
        }
    }
    for (int i = 0; i < MAP_STATE_MAX_MEDIUM_MOBS; i++) {
        MobCommon* mob = (MobCommon*)&map->state->medium_mobs[i];
        if (mob->mob_type_id != -1)
            mob_registry[mob->mob_type_id].render(mob, game, map);
    }
    for (int i = 0; i < MAP_STATE_MAX_LARGE_MOBS; i++) {
        MobCommon* mob = (MobCommon*)&map->state->large_mobs[i];
        if (mob->mob_type_id != -1)
            mob_registry[mob->mob_type_id].render(mob, game, map);
    }

    SDL_UnlockMutex(map->locked);
}

static void do_bottom_sensors(GenericBody* guy, CollisionMap* tile_collision, vec4i* tilemap_dim, TileCollision* b1ref, TileCollision* b2ref) {
    SensedTile t;
    // == BOTTOM SENSORS ==
    sense_tile(&guy->position, tilemap_dim, &guy->bottom_sensors, &t);
    TileCollision b_collision_1 = process_bottom_sensor(guy, tile_collision, &t, SENSOR_1);
    TileCollision b_collision_2 = process_bottom_sensor(guy, tile_collision, &t, SENSOR_2);

    guy->grounded = b_collision_1.hit || b_collision_2.hit;
    if (b_collision_1.hit && b_collision_2.hit) {
        guy->position.x[Y] = fmaxf(b_collision_1.new_position, b_collision_2.new_position);
        guy->ground_angle = atan2f(
            b_collision_2.new_position - b_collision_1.new_position,
            (float)(guy->bottom_sensors.x[S2X] - guy->bottom_sensors.x[S1X])
        ) / (float)M_PI * 180.0f;

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
    else {
        guy->grounded = false;
        const float angular_adjustment_speed = 2.2f;
        if (guy->ground_angle > 0) {
            guy->ground_angle -= angular_adjustment_speed;
            if (guy->ground_angle < 0)
                guy->ground_angle = 0.0f;
        }
        else if (guy->ground_angle < 0) {
            guy->ground_angle += angular_adjustment_speed;
            if (guy->ground_angle > 0)
                guy->ground_angle = 0.0f;
        }
    }

    if (b1ref)
        *b1ref = b_collision_1;
    if (b2ref)
        *b2ref = b_collision_2;
}

void collide_generic_body(struct GenericBody* body, CollisionMap* tile_collision) {
    // === These are needed for all sensor operations ===
    // tilemap dimensions twice (w,h,w,h)
    vec4i tilemap_dim;
    tilemap_dim.simd = _mm_set_epi32(
        tile_collision->height, tile_collision->width,
        tile_collision->height, tile_collision->width
    );

    TileCollision b_collision_1, b_collision_2;

    bool guy_was_grounded = body->grounded;
    if (guy_was_grounded)
        do_bottom_sensors(body, tile_collision, &tilemap_dim, &b_collision_1, &b_collision_2);

    vec4 guy_new_x_position;
    guy_new_x_position.x[X] = body->position.x[X];
    guy_new_x_position.x[Y] = body->grounded ? body->position.x[Y] : body->old_position.x[Y];
    vec4 guy_new_y_position;
    guy_new_y_position.x[X] = body->old_position.x[X];
    guy_new_y_position.x[Y] = body->position.x[Y];
    // vec4 displacement;
    // displacement.simd = _mm_sub_ps(body->position.simd, body->old_position.simd);

    SensedTile t;
    bool left_hit, right_hit, top_hit;
    // NOTE don't access these if the corrosponding *_hit == false
    TileCollision l_collision_1, l_collision_2,
                  r_collision_1, r_collision_2,
                  t_collision_1, t_collision_2;

    // NOTE Adding '=' to these comparisons will make the collision work for moving tilemaps,
    // but causes a bug when landing on a slope that curves up into a wall.

    // == LEFT SENSORS ==
    if (body->position.x[X] < body->old_position.x[X]) {
        sense_tile(&guy_new_x_position, &tilemap_dim, &body->left_sensors, &t);
        l_collision_1 = process_side_sensor(body, tile_collision, &t, LEFT_SENSOR, SENSOR_1);
        l_collision_2 = process_side_sensor(body, tile_collision, &t, LEFT_SENSOR, SENSOR_2);
        left_hit = l_collision_1.hit || l_collision_2.hit;
    }
    else
        left_hit = false;

    // == RIGHT SENSORS ==
    if (body->position.x[X] > body->old_position.x[X]) {
        sense_tile(&guy_new_x_position, &tilemap_dim, &body->right_sensors, &t);
        r_collision_1 = process_side_sensor(body, tile_collision, &t, RIGHT_SENSOR, SENSOR_1);
        r_collision_2 = process_side_sensor(body, tile_collision, &t, RIGHT_SENSOR, SENSOR_2);
        right_hit = r_collision_1.hit || r_collision_2.hit;
    }
    else
        right_hit = false;

    // == TOP SENSORS ==
    if (body->position.x[Y] > body->old_position.x[Y]) {
        sense_tile(&guy_new_y_position, &tilemap_dim, &body->top_sensors, &t);
        t_collision_1 = process_top_sensor(body, tile_collision, &t, SENSOR_1);
        t_collision_2 = process_top_sensor(body, tile_collision, &t, SENSOR_2);
        top_hit = t_collision_1.hit || t_collision_2.hit;
    }
    else
        top_hit = false;

    if (left_hit)
        body->position.x[X] = fmaxf(l_collision_1.new_position, l_collision_2.new_position);
    if (right_hit)
        body->position.x[X] = fminf(r_collision_1.new_position, r_collision_2.new_position);
    if (top_hit) {
        body->position.x[Y] = fminf(t_collision_1.new_position, t_collision_2.new_position);
        body->hit_ceiling = true;
    }
    else
        body->hit_ceiling = false;
    body->hit_wall = ((left_hit && l_collision_1.hit && l_collision_2.hit) || (right_hit && r_collision_1.hit && r_collision_2.hit));

    // Even if we were grounded and did one of these in the beginnin, we want to do another.
    do_bottom_sensors(body, tile_collision, &tilemap_dim, &b_collision_1, &b_collision_2);

    body->left_hit = left_hit;
    body->right_hit = right_hit;
}

void collide_character(struct Character* guy, CollisionMap* tile_collision) {
    // === These are needed for all sensor operations ===
    // tilemap dimensions twice (w,h,w,h)
    vec4i tilemap_dim;
    tilemap_dim.simd = _mm_set_epi32(
        tile_collision->height, tile_collision->width,
        tile_collision->height, tile_collision->width
    );

    GenericBody* body = (GenericBody*)guy;
    TileCollision b_collision_1, b_collision_2;

    bool guy_was_grounded = guy->grounded;
    if (guy_was_grounded)
        do_bottom_sensors(body, tile_collision, &tilemap_dim, &b_collision_1, &b_collision_2);

    vec4 guy_new_x_position;
    guy_new_x_position.x[X] = guy->position.x[X];
    guy_new_x_position.x[Y] = guy->grounded ? guy->position.x[Y] : guy->old_position.x[Y];
    vec4 guy_new_y_position;
    guy_new_y_position.x[X] = guy->old_position.x[X];
    guy_new_y_position.x[Y] = guy->position.x[Y];
    // vec4 displacement;
    // displacement.simd = _mm_sub_ps(guy->position.simd, guy->old_position.simd);

    SensedTile t, m_t;
    bool left_hit, right_hit, top_hit;
    // NOTE don't access these if the corrosponding *_hit == false
    TileCollision l_collision_1, l_collision_2, l_collision_3,
                  r_collision_1, r_collision_2, r_collision_3,
                  t_collision_1, t_collision_2;

    // == MIDDLE SENSORS ==
    sense_tile(&guy_new_x_position, &tilemap_dim, &guy->middle_sensors, &m_t);

    // NOTE Adding '=' to these comparisons will make the collision work for moving tilemaps,
    // but causes a bug when landing on a slope that curves up into a wall.

    // == LEFT SENSORS ==
    if (guy->position.x[X] < guy->old_position.x[X]) {
        sense_tile(&guy_new_x_position, &tilemap_dim, &guy->left_sensors, &t);
        l_collision_1 = process_side_sensor(body, tile_collision, &t,   LEFT_SENSOR, SENSOR_1);
        l_collision_2 = process_side_sensor(body, tile_collision, &t,   LEFT_SENSOR, SENSOR_2);
        l_collision_3 = process_side_sensor(body, tile_collision, &m_t, LEFT_SENSOR, SENSOR_1);
        left_hit = l_collision_1.hit || l_collision_2.hit || l_collision_3.hit;
    }
    else
        left_hit = false;

    // == RIGHT SENSORS ==
    if (guy->position.x[X] > guy->old_position.x[X]) {
        sense_tile(&guy_new_x_position, &tilemap_dim, &guy->right_sensors, &t);
        r_collision_1 = process_side_sensor(body, tile_collision, &t,   RIGHT_SENSOR, SENSOR_1);
        r_collision_2 = process_side_sensor(body, tile_collision, &t,   RIGHT_SENSOR, SENSOR_2);
        r_collision_3 = process_side_sensor(body, tile_collision, &m_t, RIGHT_SENSOR, SENSOR_2);
        right_hit = r_collision_1.hit || r_collision_2.hit || r_collision_3.hit;
    }
    else
        right_hit = false;

    // == TOP SENSORS ==
    if (guy->position.x[Y] > guy->old_position.x[Y]) {
        sense_tile(&guy_new_y_position, &tilemap_dim, &guy->top_sensors, &t);
        t_collision_1 = process_top_sensor(body, tile_collision, &t, SENSOR_1);
        t_collision_2 = process_top_sensor(body, tile_collision, &t, SENSOR_2);
        top_hit = t_collision_1.hit || t_collision_2.hit;
    }
    else
        top_hit = false;

    if (left_hit)
        guy->position.x[X] = fmaxf(l_collision_3.new_position, fmaxf(l_collision_1.new_position, l_collision_2.new_position));
    if (right_hit)
        guy->position.x[X] = fminf(r_collision_3.new_position, fminf(r_collision_1.new_position, r_collision_2.new_position));
    if (top_hit) {
        guy->position.x[Y] = fminf(t_collision_1.new_position, t_collision_2.new_position);
        guy->dy = 0;
    }
    if ((left_hit && l_collision_1.hit && l_collision_2.hit && l_collision_3.hit) || (right_hit && r_collision_1.hit && r_collision_2.hit && r_collision_3.hit)) {
        guy->ground_speed = 0;
        guy->slide_speed = 0;
    }

    // Even if we were grounded and did one of these in the beginnin, we want to do another.
    do_bottom_sensors(body, tile_collision, &tilemap_dim, &b_collision_1, &b_collision_2);

    if (guy->grounded)
        guy->dy = 0;

    guy->left_hit = left_hit;
    guy->right_hit = right_hit;
}

void slide_character(float gravity, struct Character* guy) {
    if (guy->grounded) {
        if (fabsf(guy->ground_angle) > 5.0f) {
            // guy->slide_speed -= gravity / tanf(guy->ground_angle * (M_PI / 180.0f));
            const float sin_ground_angle = sinf(guy->ground_angle * ((float)M_PI / 180.0f));
            float slide_accel = gravity * 0.1f * sin_ground_angle;

            // if we slide into another slope of opposite direction, accelerate the slide faster
            // towards our new target direction.
            if (SIGN_OF(slide_accel) == SIGN_OF(guy->slide_speed))
                slide_accel *= 2.0f;

            guy->slide_speed -= slide_accel;

            // Cap slide speed based on angle.
            const float slide_speed_max = (CHARA_GROUND_SPEED_MAX * guy->ground_speed_max) * fabsf(sin_ground_angle);
            if (guy->slide_speed > slide_speed_max)
                guy->slide_speed = slide_speed_max;
            else if (guy->slide_speed < -slide_speed_max)
                guy->slide_speed = -slide_speed_max;
        }
        else
            guy->slide_speed = 0;
    }
}


#define FIND_SPAWN_SLOT(size, mob_list)\
    case size:\
        for (int k = 0; k < MAP_STATE_MAX_##size##_MOBS ; k++) {\
            if (map->state->mob_list[k].mob_type_id == -1) {\
                mob_data = (MobCommon*)&map->state->mob_list[k];\
                mob_data->index = k;\
                goto spawn_it;\
            }\
        } return NULL;

MobCommon* spawn_mob(Map* map, struct Game* game, int mob_type_id, vec2 pos) {
    MobType* mob_to_spawn = &mob_registry[mob_type_id];
    SDL_assert(mob_to_spawn->id == mob_type_id);

    MobCommon* mob_data;
    switch (mob_to_spawn->size_class) {
        FIND_SPAWN_SLOT(SMALL,  small_mobs)
        FIND_SPAWN_SLOT(MEDIUM, medium_mobs)
        FIND_SPAWN_SLOT(LARGE,  large_mobs)
    default:
        printf("UNKNOWN SIZE CLASS. mob_type_id: %i, spawn class: %i", mob_to_spawn->id, mob_to_spawn->size_class);
        return NULL;
    }

spawn_it:
    mob_data->mob_type_id = mob_type_id;
    mob_to_spawn->initialize((void*)mob_data, game, map, pos);
    return mob_data;
}

void despawn_mob(Map* map, struct Game* game, MobCommon* mob) {
    int offset = 0;
    mob->mob_type_id = MOB_NONE;
}

int mob_id(Map* map, MobCommon* mob) {
    int offset = 0;
    MobType* mob_type = &mob_registry[mob->mob_type_id];
    switch (mob_type->size_class) {
    case SMALL:
        break;
    case LARGE:  offset += MAP_STATE_MAX_MEDIUM_MOBS;
    case MEDIUM: offset += MAP_STATE_MAX_SMALL_MOBS;
        break;
    default:
        SDL_assert(false);
        return -1;
    }
    return offset + mob->index;
}

int index_from_mob_id(int id) {
    if (id > MAP_STATE_MAX_SMALL_MOBS) {
        id -= MAP_STATE_MAX_SMALL_MOBS;
        if (id > MAP_STATE_MAX_MEDIUM_MOBS)
            id -= MAP_STATE_MAX_MEDIUM_MOBS;
    }
    SDL_assert(id >= 0);
    return id;
}

MobCommon* mob_from_id(Map* map, int id) {
    MobCommon* mob_list = (MobCommon*)map->state->small_mobs;
    int offset = sizeof(SmallMob);

    if (id > MAP_STATE_MAX_SMALL_MOBS) {
        id -= MAP_STATE_MAX_SMALL_MOBS;
        mob_list = (MobCommon*)map->state->medium_mobs;
        offset = sizeof(MediumMob);

        if (id > MAP_STATE_MAX_MEDIUM_MOBS) {
            id -= MAP_STATE_MAX_MEDIUM_MOBS;
            mob_list = (MobCommon*)map->state->large_mobs;
            SDL_assert(id < MAP_STATE_MAX_LARGE_MOBS);
            offset = sizeof(LargeMob);
        }
    }
    SDL_assert(id >= 0);

    return (MobCommon*)(((byte*)mob_list) + id * offset);
}

void update_map(
    Map* map, struct Game* game,
    void* data,
    void(*after_mob_update)(void*, Map*, struct Game*, MobCommon*)
) {
    wait_for_then_use_lock(map->locked);
    if (game->net_joining)
        goto UpdateMobs;
    // Spawn mobs when needed.
    for (int i = 0; i < map->number_of_spawn_zones; i++) {
        MobSpawnZone* zone = &map->spawn_zones[i];

        if (zone->countdown_until_next_spawn_attempt >= 0) {
            zone->countdown_until_next_spawn_attempt -= 1;
        }
        else {
            int random = pcg32_boundedrand(100);

            for (int j = 0; j < zone->number_of_spawns; j++) {
                MobSpawnRate* spawn = &zone->spawns[j];
                if (spawn->percentage != 0 && random <= spawn->percentage) {
                    vec2 target_pos = {
                        zone->x + pcg32_boundedrand(zone->width),
                        zone->y + pcg32_boundedrand(zone->height)
                    };
                    game->net.spawn_mob(game->current_scene_data, map, game, spawn->mob_type_id, target_pos, NULL, NULL);

                    // TODO add spawn frequency or something to maps.
                    zone->countdown_until_next_spawn_attempt = 60 * 60 * 60;
                    goto UpdateMobs;
                }
            }
        }
    }

UpdateMobs:;
#define UPDATE_MOBS(size, mob_list)\
    for (int i = 0; i < MAP_STATE_MAX_##size##_MOBS; i++) {\
        MobCommon* mob = (MobCommon*)&map->state->mob_list[i];\
        if (mob->mob_type_id != -1) {\
            MobType* reg = &mob_registry[mob->mob_type_id];\
            if (reg->update)\
                reg->update(mob, game, map);\
            \
            if (after_mob_update)\
                after_mob_update(data, map, game, mob);\
            \
            INTERACT_MOBS(SMALL, small_mobs, mob_list);\
            INTERACT_MOBS(MEDIUM, medium_mobs, mob_list);\
            INTERACT_MOBS(LARGE, large_mobs, mob_list);\
        }\
    }
#define INTERACT_MOBS(size, mob_list, from_list)\
    for (int j = 0; j < MAP_STATE_MAX_##size##_MOBS; j++) {\
        if ((MobCommon*)map->state->mob_list == (MobCommon*)map->state->from_list && j == i) continue;\
        MobCommon* other_mob = (MobCommon*)&map->state->mob_list[j];\
        if (other_mob->mob_type_id != -1 && reg->mob_interact)\
            reg->mob_interact(mob, game, map, other_mob);\
    }

    BENCH_START(update_mobs);
    UPDATE_MOBS(SMALL,  small_mobs);
    UPDATE_MOBS(MEDIUM, medium_mobs);
    UPDATE_MOBS(LARGE,  large_mobs);
    BENCH_END(update_mobs);

    SDL_UnlockMutex(map->locked);
}

#define READ(type, dest) \
    type dest; \
    SDL_memcpy(&dest, file.bytes + pos, sizeof(type)); \
    pos += sizeof(type)

CmFileHeader read_cm_file_header(const int asset) {
    CmFileHeader header;
    AssetFile file = load_asset(asset);
    header.magic[0] = file.bytes[0];
    header.magic[1] = file.bytes[1];
    header.magic[2] = file.bytes[2];
    int pos = 3;

    SDL_memcpy(&header.tiles_wide, file.bytes + pos, sizeof(Uint32));
    pos += sizeof(Uint32);
    SDL_memcpy(&header.tiles_high, file.bytes + pos, sizeof(Uint32));
    pos += sizeof(Uint32);
    SDL_memcpy(&header.tilemap_count, file.bytes + pos, sizeof(Uint8));
    pos += sizeof(Uint8);
    SDL_memcpy(&header.background_count, file.bytes + pos, sizeof(Uint8));
    pos += sizeof(Uint8);
    SDL_memcpy(&header.door_count, file.bytes + pos, sizeof(Uint8));
    pos += sizeof(Uint8);
    SDL_memcpy(&header.total_spawn_rate_count, file.bytes + pos, sizeof(Uint16));
    pos += sizeof(Uint16);
    SDL_memcpy(&header.spawn_zone_count, file.bytes + pos, sizeof(Uint8));
    pos += sizeof(Uint8);

    return header;
}

// Here we're assuming map is just some empty chunk of memory with enough size lol...
// Enough memory can be allocated ahead of time by reading the file header. (like in assets.c)
// ALSO ASSUMES THE MUTEX IS ALREADY INITIALIZED... (like in assets.c)
void load_map(const int asset, /*out*/ Map* map) {
    AssetFile file = load_asset(asset);
    SDL_assert(file.bytes[0] == 'C');
    SDL_assert(file.bytes[1] == 'M');
    SDL_assert(file.bytes[2] == '2');
    int pos = 3;

    READ(Uint32, tiles_wide);
    READ(Uint32, tiles_high);
    READ(Uint8, tilemap_count);
    READ(Uint8, background_count);
    READ(Uint8, door_count);
    READ(Uint16, total_spawn_rate_count);
    READ(Uint8, spawn_zone_count);

    // NOTE Defaults for these are defined in tilemap_compressor.rb
    READ(float, gravity);
    READ(float, drag);

    map->asset_id = asset;
    map->width  = tiles_wide * 32;
    map->height = tiles_high * 32;
    map->tile_collision.width   = (int)tiles_wide;
    map->tile_collision.height  = (int)tiles_high;
    map->tile_collision.heights = COLLISION_TERRAIN_STANDARD;
    map->number_of_tilemaps     = (int)tilemap_count;
    map->number_of_backgrounds  = (int)background_count;
    map->number_of_doors        = (int)door_count;
    map->number_of_spawn_zones  = (int)spawn_zone_count;

    map->gravity = gravity;
    map->drag    = drag;

    // We're gonna just put tilemap structs sequentially after the map struct
    map->tilemaps = (Tilemap*)(map + 1);

    for (Uint8 i = 0; i < tilemap_count; i++) {
        char* texture_asset_ident = file.bytes + pos;
        for (char* c = texture_asset_ident; *c != 0; c++)
            pos += 1;
        // This leaves us at the terminating 0
        pos += 1;

        READ(Uint16, tiles_per_row);
        READ(Uint32, data_size_in_bytes);

        map->tilemaps[i].width  = (int)tiles_wide;
        map->tilemaps[i].height = (int)tiles_high;
        map->tilemaps[i].tex_asset = asset_from_ident(texture_asset_ident);
        SDL_assert(map->tilemaps[i].tex_asset != -1);
        map->tilemaps[i].tex = NULL;
        map->tilemaps[i].tiles_per_row = (int)tiles_per_row;
        // Assuming embedded assets
        map->tilemaps[i].tiles = (int*)(file.bytes + pos);

        pos += data_size_in_bytes * sizeof(int);
    }

    READ(Uint32, collision_tiles_size);
    SDL_assert(collision_tiles_size == tiles_high * tiles_wide);
    // Again, assuming embedded assets
    map->tile_collision.tiles = (int*)(file.bytes + pos);
    pos += collision_tiles_size * sizeof(int);
    SDL_assert(file.size > pos);

    // Parallax time! (again assuming enough space has been allocated)
    map->backgrounds = (ParallaxBackground*)(map->tilemaps + tilemap_count);
    for (Uint8 i = 0; i < background_count; i++) {
        char* texture_asset_ident = file.bytes + pos;
        for (char* c = texture_asset_ident; *c != 0; c++)
            pos += 1;
        // This leaves us at the terminating 0
        pos += 1;

        READ(int, x);
        READ(int, y);
        READ(float, pfactor);
        READ(int, frame_width);
        READ(int, frame_height);
        READ(Uint32, frame_count);
        READ(Uint32, flags);

        map->backgrounds[i].x = x;
        map->backgrounds[i].y = y;
        map->backgrounds[i].parallax_factor = pfactor;
        map->backgrounds[i].frame_width     = frame_width;
        map->backgrounds[i].frame_height    = frame_height;
        map->backgrounds[i].frame_count     = frame_count;
        map->backgrounds[i].flags = flags;
        map->backgrounds[i].bg_asset = asset_from_ident(texture_asset_ident);
        SDL_assert(map->backgrounds[i].bg_asset != -1);
        map->backgrounds[i].frame = 0;

        SDL_Surface* image = load_image(map->backgrounds[i].bg_asset);
        map->backgrounds[i].width  = image->w;
        map->backgrounds[i].height = image->h;
        free_image(image);
    }
    SDL_assert(door_count != 0 ? file.size != pos : true);

    map->doors = (Door*)(map->backgrounds + background_count);
    for (int i = 0; i < door_count; i++) {
        READ(int, x);
        READ(int, y);
        READ(int, area_id);
        READ(int, dest_x);
        READ(int, dest_y);
        READ(byte, orb_r);
        READ(byte, orb_g);
        READ(byte, orb_b);
        READ(byte, orb_a);

        map->doors[i].x = x;
        map->doors[i].y = y;
        map->doors[i].dest_area = area_id;
        map->doors[i].dest_x = dest_x;
        map->doors[i].dest_y = dest_y;
        map->doors[i].orb_color.r = orb_r;
        map->doors[i].orb_color.g = orb_g;
        map->doors[i].orb_color.b = orb_b;
        map->doors[i].orb_color.a = orb_a;
        map->doors[i].flags = DOOR_VISIBLE | DOOR_INVERT_Y;
        map->doors[i].callback = NULL;
    }

    MobSpawnRate* next_spawn_rate = (MobSpawnRate*)(map->doors + door_count);
    map->spawn_zones = (MobSpawnZone*)(next_spawn_rate + total_spawn_rate_count);
    for (int i = 0; i < spawn_zone_count; i++) {
        READ(int, x);
        READ(int, y);
        READ(int, width);
        READ(int, height);
        READ(byte, spawn_rate_count);

        map->spawn_zones[i].x = x;
        map->spawn_zones[i].y = y;
        map->spawn_zones[i].width  = width;
        map->spawn_zones[i].height = height;
        map->spawn_zones[i].number_of_spawns = spawn_rate_count;
        map->spawn_zones[i].countdown_until_next_spawn_attempt = 60 * 2;

        map->spawn_zones[i].spawns = next_spawn_rate;
        next_spawn_rate += spawn_rate_count;

        for (int j = 0; j < spawn_rate_count; j++) {
            READ(int, mob_type_id);
            READ(int, percentage);

            map->spawn_zones[i].spawns[j].mob_type_id = mob_type_id;
            map->spawn_zones[i].spawns[j].percentage  = percentage;
        }
    }

    // DONE reading the file - now just initializing map state.
    SDL_assert(file.size == pos);

    byte* map_state = (byte*)(map->spawn_zones + spawn_zone_count);
    int remainder = ((size_t)map_state) % 16;
    if (remainder != 0)
        map_state += (16 - remainder);

    map->state = (MapState*)map_state;
    clear_map_state(map);
}

void clear_map_state(Map* map) {
    // TODO use despawn_mob or something so we can do some cleanup function
    EACH_MOB(map, (mob), {
        mob->mob_type_id = -1;
    });
}

void transfer_map_state(Map* map, byte rw, AbdBuffer* buf) {
    data_section(rw, buf, "mob states");
    EACH_MOB(map, (mob), {
        data_s32_a(rw, buf, &mob->mob_type_id, "mob type id");
        SDL_assert(mob->mob_type_id >= -1);

        if (mob->mob_type_id != -1) {
            data_section(rw, buf, "BEGIN MOB");
            if (rw == ABD_READ) {
                mob->index = i;
                mob_registry[mob->mob_type_id].initialize(mob, map->game, map, (vec2) { 0, 0 });
            }
            mob_registry[mob->mob_type_id].transfer(mob, map->game, map, rw, buf);
            data_section(rw, buf, "END MOB");
        }
    });
}
void write_map_state(Map* map, byte* buffer, int* pos) {
    AbdBuffer buf = buf_new(buffer, *pos);
    transfer_map_state(map, ABD_WRITE, &buf);
    *pos = buf.pos;
}
void read_map_state(Map* map, byte* buffer, int* pos) {
    AbdBuffer buf = buf_new(buffer, *pos);
    transfer_map_state(map, ABD_READ, &buf);
    *pos = buf.pos;
}

void transfer_map_to_data(Map* map, byte rw, struct DataChunk* chunk) {
    DATA_CHUNK_TO_BUF(rw, chunk, buf, sizeof(MapState) + map->number_of_spawn_zones * sizeof(MobSpawnZone));

    data_u32(rw, buf, &map->area_id);
    transfer_map_state(map, rw, buf);

    data_s32(rw, buf, &map->number_of_spawn_zones);
}
