#include "scene.h"

typedef struct {
    float gravity;
    float terminal_velocity;
    Character guy;
    SDL_Texture* tiles;
    AudioWave* wave;
    AudioWave test_sound;
    int animation_frame;
    SDL_RendererFlip flip;
    bool animate;
    float dy;
    float jump_acceleration;
    Tilemap test_tilemap;
} TestScene;

static const int inverted_test_tilemap[] = {
    9,4,4,9,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,9,
    9,0,0,0,3,4,4,4,9,11,10,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,1,13,3,9,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,1,2,3,9,-1,-1,-1,-1,-1,12,11,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,1,2,0,0,0,0,11,10,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,1,2,11,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,1,2,11,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,6,
    6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,6,
};

typedef struct {
    vec4i tilespace;
    vec4i tilepos;
    vec4i position_within_tile;
    vec4i indices_are_valid;
} SensedTile;

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

void scene_test_initialize(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    // Testing physics!!!!
    data->gravity = 1.15f; // In pixels per frame per frame
    data->terminal_velocity = 14.3f;

    data->test_tilemap.tiles  = inverted_test_tilemap;
    data->test_tilemap.width  = 20;
    data->test_tilemap.height = 15;

    BENCH_START(loading_crattle)
    data->guy.textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->guy.textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->guy.textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    BENCH_END(loading_crattle)

    default_character(&data->guy);
    data->guy.position.simd = _mm_set1_ps(10.0f);
    data->guy.position.x[1] = 500.0f;
    data->animation_frame = 0;
    data->flip = SDL_FLIP_NONE;
    data->flip = false;
    data->dy = 0; // pixels per second
    data->jump_acceleration = 20.0f;

    BENCH_START(loading_tiles)
    data->tiles = load_texture(game->renderer, ASSET_TERRAIN_TESTGROUND_PNG);
    BENCH_END(loading_tiles)

    // TODO oh god testing audio
    BENCH_START(loading_sound)
    // data->wave = open_and_play_music(&game->audio);
    data->test_sound = decode_ogg(ASSET_SOUNDS_EXPLOSION_OGG);
    BENCH_END(loading_sound)
}

void scene_test_update(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;
    // Test movement (for controls' sake)
    s->dy -= s->gravity; // times 1 frame
    // Get accelerations from controls
    if (game->controls.this_frame[C_LEFT]) {
        s->guy.ground_speed -= s->guy.ground_acceleration;
        s->flip = SDL_FLIP_HORIZONTAL;
    }
    if (game->controls.this_frame[C_RIGHT]) {
        s->guy.ground_speed += s->guy.ground_acceleration;
        s->flip = SDL_FLIP_NONE;
    }
    if (just_pressed(&game->controls, C_UP)) {
        s->dy += s->jump_acceleration;
    }
    // TODO having ground_deceleration > ground_acceleration will have a weird effect here.
    if (!game->controls.this_frame[C_LEFT] && !game->controls.this_frame[C_RIGHT]) {
        if (s->guy.ground_speed > 0) {
            s->guy.ground_speed -= s->guy.ground_deceleration;
            if (s->guy.ground_speed < 0)
                s->guy.ground_speed = 0;
        }
        else if (s->guy.ground_speed < 0) {
            s->guy.ground_speed += s->guy.ground_deceleration;
            if (s->guy.ground_speed > 0)
                s->guy.ground_speed = 0;
        }
    }
    s->animate = game->controls.this_frame[C_LEFT] || game->controls.this_frame[C_RIGHT];

    // Cap speeds
    if (s->guy.ground_speed > s->guy.ground_speed_max)
        s->guy.ground_speed = s->guy.ground_speed_max;
    else if (s->guy.ground_speed < -s->guy.ground_speed_max)
        s->guy.ground_speed = -s->guy.ground_speed_max;
    if (s->dy < -s->terminal_velocity)
        s->dy = -s->terminal_velocity;

    if (game->controls.this_frame[C_SPACE])
        s->dy = 0;
    // Actually move the dude (ground_speed is just x for now)
    __m128 movement = {s->guy.ground_speed, s->dy, 0.0f, 0.0f};
    s->guy.position.simd = _mm_add_ps(s->guy.position.simd, movement);

    // Temporary ground @ 20px
    if (s->guy.position.x[1] < 20) {
        s->guy.position.x[1] = 20;
        s->dy = 0;
    }
    // Test out tile collision!!!
    {
        int sense_x, sense_y, tile_x, tile_y, tilespace_x, tilespace_y;

        // === These are needed for all sensor operations ===
        // tilemap dimensions twice (w,h,w,h)
        vec4i tilemap_dim;
        tilemap_dim.simd = _mm_set_epi32(
            s->test_tilemap.height, s->test_tilemap.width,
            s->test_tilemap.height, s->test_tilemap.width
        );

        // == BOTH LEFT SENSOR SIMD??? (TEST!!) ==
        SensedTile t;
        sense_tile(&s->guy.position, &tilemap_dim, &s->guy.left_sensors, &t);

        if (t.indices_are_valid.x[0] && t.indices_are_valid.x[1]) {
            int tile_index = s->test_tilemap.tiles[t.tilespace.x[1] * s->test_tilemap.width + t.tilespace.x[0]];
            if (tile_index >= 0) {
                TileHeights* collision_height_list = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                // TODO assumes left2right
                int* heights = collision_height_list->left2right;

                // TODO assumes left2right
                int height = heights[t.position_within_tile.x[1]];
                if (height >= 0) {
                    // TODO left2right-specific behavior:
                    int x_placement = t.tilepos.x[0] + height - s->guy.left_sensors.x[0];

                    if (x_placement > s->guy.position.x[0])
                        s->guy.position.x[0] = x_placement;
                }
            }
        }
        if (t.indices_are_valid.x[2] && t.indices_are_valid.x[3]) {
            int tile_index = s->test_tilemap.tiles[t.tilespace.x[3] * s->test_tilemap.width + t.tilespace.x[2]];
            if (tile_index >= 0) {
                TileHeights* collision_heights = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_heights->left2right;

                int height = heights[t.position_within_tile.x[3]];
                if (height >= 0) {
                    int x_placement = t.tilepos.x[2] + height - s->guy.left_sensors.x[2];

                    if (x_placement > s->guy.position.x[0])
                        s->guy.position.x[0] = x_placement;
                }
            }
        }

        // == RIGHT SENSORS ==
        sense_tile(&s->guy.position, &tilemap_dim, &s->guy.right_sensors, &t);

        if (t.indices_are_valid.x[0] && t.indices_are_valid.x[1]) {
            int tile_index = s->test_tilemap.tiles[t.tilespace.x[1] * s->test_tilemap.width + t.tilespace.x[0]];
            if (tile_index >= 0) {
                TileHeights* collision_height_list = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_height_list->right2left;

                int height = heights[t.position_within_tile.x[1]];
                if (height >= 0) {
                    int x_placement = t.tilepos.x[0] + 32 - height - s->guy.right_sensors.x[0];

                    if (x_placement < s->guy.position.x[0])
                        s->guy.position.x[0] = x_placement;
                }
            }
        }
        if (t.indices_are_valid.x[2] && t.indices_are_valid.x[3]) {
            int tile_index = s->test_tilemap.tiles[t.tilespace.x[3] * s->test_tilemap.width + t.tilespace.x[2]];
            if (tile_index >= 0) {
                TileHeights* collision_heights = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_heights->right2left;

                int height = heights[t.position_within_tile.x[3]];
                if (height >= 0) {
                    int x_placement = t.tilepos.x[2] + 32 - height - s->guy.right_sensors.x[2];

                    if (x_placement < s->guy.position.x[0])
                        s->guy.position.x[0] = x_placement;
                }
            }
        }

        // == TOP SENSOR 1 ==
        sense_tile(&s->guy.position, &tilemap_dim, &s->guy.top_sensors, &t);

        if (t.indices_are_valid.x[S1X] && t.indices_are_valid.x[S1Y]) {
            int tile_index = s->test_tilemap.tiles[t.tilespace.x[S1Y] * s->test_tilemap.width + t.tilespace.x[S1X]];
            if (tile_index >= 0) {
                TileHeights* collision_height_list = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_height_list->bottom2up;

                int height = heights[t.position_within_tile.x[S1X]];
                if (height >= 0) {
                    int y_placement = t.tilepos.x[S1Y] - height - s->guy.top_sensors.x[S1Y];

                    if (y_placement < s->guy.position.x[1]) {
                        s->guy.position.x[1] = y_placement;
                        s->dy = 0;
                    }
                }
            }
        }
        if (t.indices_are_valid.x[S2X] && t.indices_are_valid.x[S2Y]) {
            int tile_index = s->test_tilemap.tiles[t.tilespace.x[S2Y] * s->test_tilemap.width + t.tilespace.x[S2X]];
            if (tile_index >= 0) {
                TileHeights* collision_height_list = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_height_list->bottom2up;

                int height = heights[t.position_within_tile.x[S2X]];
                if (height >= 0) {
                    int y_placement = t.tilepos.x[S2Y] - height - s->guy.top_sensors.x[S2Y];

                    if (y_placement < s->guy.position.x[1]) {
                        s->guy.position.x[1] = y_placement;
                        s->dy = 0;
                    }
                }
            }
        }

        // == BOTTOM SENSOR 1 ==
        bool b1_hit = false;
        sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[0];
        sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[1];
        tilespace_x = sense_x / 32;
        tilespace_y = sense_y / 32;
    B1Go:
        // left side of tile
        tile_x = tilespace_x * 32;
        // bottom of tile
        tile_y = tilespace_y * 32;

        if (tilespace_y >= 0 && tilespace_y < 15 && tilespace_x >= 0 && tilespace_x < 20) {
            int tile_index = s->test_tilemap.tiles[tilespace_y * 20 + tilespace_x];
            if (tile_index >= 0) {
                TileHeights* collision_heights = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_heights->top2down;
                int x_position_within_tile = sense_x - tile_x;
                int height = heights[x_position_within_tile];
                if (height >= 0) {
                    int y_placement = tile_y + height - s->guy.bottom_sensors.x[1];

                    if (y_placement > s->guy.position.x[1]) {
                        s->guy.position.x[1] = y_placement;
                        s->dy = 0;
                        b1_hit = true;
                    }
                }
            }
        }
        if (b1_hit && (tilespace_y * 32) - sense_y <= 3) {
            // check tile above
            tilespace_y += 1;
            sense_y += 1;
            b1_hit = false;
            goto B1Go;
        }

        // == BOTTOM SENSOR 2 == (COPOPASTO)
        bool b2_hit = false;
        sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[2];
        sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[3];
        tilespace_x = sense_x / 32;
        tilespace_y = (game->window_height - sense_y) / 32;
    B2Go:
        // left side of tile
        tile_x = tilespace_x * 32;
        // bottom of tile
        tile_y = tilespace_y * 32;

        if (tilespace_y >= 0 && tilespace_y < 15 && tilespace_x >= 0 && tilespace_x < 20) {
            int tile_index = s->test_tilemap.tiles[tilespace_y * 20 + tilespace_x];
            if (tile_index >= 0) {
                TileHeights* collision_heights = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_heights->top2down;
                int x_position_within_tile = sense_x - tile_x;
                int height = heights[x_position_within_tile];
                if (height >= 0) {
                    int y_placement = tile_y + height - s->guy.bottom_sensors.x[3];

                    if (y_placement > s->guy.position.x[1]) {
                        s->guy.position.x[1] = y_placement;
                        s->dy = 0;
                        b2_hit = true;
                    }
                }
            }
        }
        if (b2_hit && (tilespace_y * 32) - sense_y <= 3) {
            // check tile above
            tilespace_y += 1;
            sense_y += 1;
            b2_hit = false;
            goto B2Go;
        }
    }

    // Test sound effect
    if (just_pressed(&game->controls, C_UP)) {
        s->test_sound.samples_pos = 0;
        game->audio.oneshot_waves[0] = &s->test_sound;
    }

    // Swap to offset viewer on F1 press
    if (just_pressed(&game->controls, C_F1))
        switch_scene(game, SCENE_OFFSET_VIEWER);
}

void scene_test_render(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;
    // Draw tiles!
    // DEBUG: b-sensor 1 tile index
    int sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[0];
    int sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[1];
    int b1_tilespace_x = sense_x / 32;
    int b1_tilespace_y = sense_y / 32;
    // DEBUG: b-sensor 2 tile index
    sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[2];
    sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[3];
    int b2_tilespace_x = sense_x / 32;
    int b2_tilespace_y = sense_y / 32;
    // DEBUG: l-sensor 1 tile index
    sense_x = s->guy.position.x[0] + s->guy.left_sensors.x[0];
    sense_y = s->guy.position.x[1] + s->guy.left_sensors.x[1];
    int l1_tilespace_x = sense_x / 32;
    int l1_tilespace_y = sense_y / 32;
    // DEBUG: l-sensor 2 tile index
    sense_x = s->guy.position.x[0] + s->guy.left_sensors.x[2];
    sense_y = s->guy.position.x[1] + s->guy.left_sensors.x[3];
    int l2_tilespace_x = sense_x / 32;
    int l2_tilespace_y = sense_y / 32;
    // DEBUG: r-sensor 1 tile index
    sense_x = s->guy.position.x[0] + s->guy.right_sensors.x[0];
    sense_y = s->guy.position.x[1] + s->guy.right_sensors.x[1];
    int r1_tilespace_x = sense_x / 32;
    int r1_tilespace_y = sense_y / 32;
    // DEBUG: r-sensor 2 tile index
    sense_x = s->guy.position.x[0] + s->guy.right_sensors.x[2];
    sense_y = s->guy.position.x[1] + s->guy.right_sensors.x[3];
    int r2_tilespace_x = sense_x / 32;
    int r2_tilespace_y = sense_y / 32;

    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 15; j++) {
            SDL_Rect tile_rect = { i * 32, game->window_height - j * 32 - 32, 32, 32 };
            int tile_index = s->test_tilemap.tiles[j * 20 + i];

            if (tile_index != -1) {
                SDL_Rect src = { 0, 0, 32, 32 };
                // Hack this since we don't yet have a way to know the dimensions of the tile image
                if (tile_index >= 8) {
                    src.x = tile_index - 8;
                    src.y = 1;
                }
                else {
                    src.x = tile_index;
                    src.y = 0;
                }
                src.x *= 32;
                src.y *= 32;

                SDL_RenderCopy(game->renderer, s->tiles, &src, &tile_rect);
            }

            // DEBUG: highlight tiles that sensors cover
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
                SDL_SetRenderDrawColor(game->renderer, 0, 255, 50, 128);
                SDL_RenderDrawRect(game->renderer, &tile_rect);
            }

            SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
        }
    }

    SDL_Rect src = { s->animation_frame * 90, 0, 90, 90 };
    // The dude's center of mass is still at the corner.
    SDL_Rect dest = { s->guy.position.x[0], game->window_height - s->guy.position.x[1] - s->guy.height, 90, 90 };
    if (s->animate) {
        if (game->frame_count % 5 == 0)
            s->animation_frame += 1;
        if (s->animation_frame >= 9)
            s->animation_frame = 1;
    }
    else s->animation_frame = 0;
    for (int i = 0; i < 3; i++)
        SDL_RenderCopyEx(game->renderer, s->guy.textures[i], &src, &dest, 0, NULL, s->flip);

    // Draw sensors
    SDL_Rect offset = { 0, 0, 1, 1 };
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(game->renderer, &r, &b, &g, &a);

    // TOP
    SDL_SetRenderDrawColor(game->renderer, 0, 255, 0, 255);
    offset.x = dest.x + s->guy.top_sensors.x[0];
    offset.y = dest.y + s->guy.height - s->guy.top_sensors.x[1];
    SDL_RenderFillRect(game->renderer, &offset);
    offset.x = dest.x + s->guy.top_sensors.x[2];
    offset.y = dest.y + s->guy.height - s->guy.top_sensors.x[3];
    SDL_RenderFillRect(game->renderer, &offset);

    // BOTTOM
    SDL_SetRenderDrawColor(game->renderer, 0, 255, 0, 255);
    offset.x = dest.x + s->guy.bottom_sensors.x[0];
    offset.y = dest.y + s->guy.height - s->guy.bottom_sensors.x[1];
    SDL_RenderFillRect(game->renderer, &offset);
    offset.x = dest.x + s->guy.bottom_sensors.x[2];
    offset.y = dest.y + s->guy.height - s->guy.bottom_sensors.x[3];
    SDL_RenderFillRect(game->renderer, &offset);

    // LEFT
    SDL_SetRenderDrawColor(game->renderer, 255, 0, 0, 255);
    offset.x = dest.x + s->guy.left_sensors.x[0];
    offset.y = dest.y + s->guy.height - s->guy.left_sensors.x[1];
    SDL_RenderFillRect(game->renderer, &offset);
    offset.x = dest.x + s->guy.left_sensors.x[2];
    offset.y = dest.y + s->guy.height - s->guy.left_sensors.x[3];
    SDL_RenderFillRect(game->renderer, &offset);

    // RIGHT
    SDL_SetRenderDrawColor(game->renderer, 255, 0, 0, 255);
    offset.x = dest.x + s->guy.right_sensors.x[0];
    offset.y = dest.y + s->guy.height - s->guy.right_sensors.x[1];
    SDL_RenderFillRect(game->renderer, &offset);
    offset.x = dest.x + s->guy.right_sensors.x[2];
    offset.y = dest.y + s->guy.height - s->guy.right_sensors.x[3];
    SDL_RenderFillRect(game->renderer, &offset);


    SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
}

void scene_test_cleanup(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    SDL_DestroyTexture(data->tiles);
    SDL_DestroyTexture(data->guy.textures[0]);
    SDL_DestroyTexture(data->guy.textures[1]);
    SDL_DestroyTexture(data->guy.textures[2]);
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL; // This is from open_and_play_music, which sucks and should be removed asap.
    // free(data->wave->samples);
    // free(data->wave);
    free(data->test_sound.samples); // This one is local to this function so only the samples are malloced.
}