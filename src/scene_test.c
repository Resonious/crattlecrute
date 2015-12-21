#include "scene.h"

#ifdef _DEBUG
extern bool debug_pause;
#endif

typedef struct {
    float gravity;
    float terminal_velocity;
    Character guy;
    AudioWave* wave;
    AudioWave test_sound;
    int animation_frame;
    SDL_RendererFlip flip;
    bool animate;
    float dy;
    float jump_acceleration;
    Tilemap test_tilemap;
} TestScene;

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

static TileCollision process_bottom_sensor_one_tile_down(TestScene* s, SensedTile* t, const int sensor) {
    TileCollision result;
    result.hit = false;
    result.new_position = s->guy.position.x[Y];

    int new_tilespace_y = t->tilespace.x[sensor+Y] - 1;
    // Make sure "one tile down" is in fact a valid tile index..
    if (new_tilespace_y >= 0 && new_tilespace_y < s->test_tilemap.height) {
        t->tilespace.x[sensor + Y] = new_tilespace_y;
        t->tilepos.x[sensor + Y] -= 32;

        int* heights = NULL;
        TileIndex tile_index = tile_at(&s->test_tilemap, &t->tilespace, sensor);
        if (!(tile_index.flags & NOT_A_TILE)) {
            heights = COLLISION_TERRAIN_TESTGROUND2[tile_index.index].top2down;
            int x_within_tile = t->position_within_tile.x[sensor+X];
            int height = heights[(tile_index.flags & TILE_FLIP_X) ? 32 - x_within_tile : x_within_tile];
            if (height < 0)
                return result;

            // Just assume that we want to be placed here (this function should only be called when grounded)
            result.hit = true;
            result.new_position = t->tilepos.x[sensor+Y] + height - s->guy.bottom_sensors.x[sensor+Y];
        }
    }

    return result;
}

static TileCollision process_bottom_sensor(TestScene* s, SensedTile* t, const int sensor) {
    TileCollision result;
    result.hit = false;
    result.new_position = s->guy.position.x[Y];

    if (t->indices_are_valid.x[sensor+X] && t->indices_are_valid.x[sensor+Y]) {
        TileIndex tile_index = tile_at(&s->test_tilemap, &t->tilespace, sensor);
        // This would mean we're in the air!
        if (tile_index.flags & NOT_A_TILE) {
            if (s->guy.grounded)
                return process_bottom_sensor_one_tile_down(s, t, sensor);
            else
                return result;
        }

        int* heights = COLLISION_TERRAIN_TESTGROUND2[tile_index.index].top2down;
        int x_within_tile = t->position_within_tile.x[sensor+X];
        bool tile_x_flipped = tile_index.flags & TILE_FLIP_X;

        int height = heights[tile_x_flipped ? 32 - x_within_tile : x_within_tile];

        // This would also mean we're in the air
        if (height == -1 && s->guy.grounded)
            return process_bottom_sensor_one_tile_down(s, t, sensor);
        // Try next tile up
        else if (height == 32) {
            int new_tilespace_y = t->tilespace.x[sensor+Y] + 1;
            // Make sure "one tile up" is in fact a valid tile index..
            if (new_tilespace_y >= 0 && new_tilespace_y < s->test_tilemap.height) {
                t->tilespace.x[sensor+Y] = new_tilespace_y;
                t->tilepos.x[sensor+Y] += 32;

                tile_index = tile_at(&s->test_tilemap, &t->tilespace, sensor);
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
            int y_placement = t->tilepos.x[sensor+Y] + height - s->guy.bottom_sensors.x[sensor+Y];
            if (y_placement > s->guy.position.x[Y] || s->guy.grounded) {
                result.hit = true;
                result.new_position = (float)y_placement;
            }
        }
    }

    return result;
}

static TileCollision dont_call_me(Tilemap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    // This should never happen!!!
#ifdef _DEBUG
    SDL_assert(false);
#endif
}
static TileCollision left_sensor_placement(Tilemap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + height + 1 - guy->left_sensors.x[sensor+X]);
    result.hit = result.new_position > guy->position.x[X];
    return result;
}
static TileCollision right_sensor_placement(Tilemap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+X] + 32 - height - guy->right_sensors.x[sensor+X]);
    result.hit = result.new_position < guy->position.x[X];
    return result;
}
static TileCollision top_sensor_placement(Tilemap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) {
    TileCollision result;
    result.new_position = (float)(t->tilepos.x[sensor+Y] + height - 1 - guy->top_sensors.x[sensor+Y]);
    result.hit = result.new_position < guy->position.x[Y];
    return result;
}

// NOTE the functions in here should line up with BOTTOM_SENSOR, TOP_SENSOR, RIGHT_SENSOR, and LEFT_SENSOR.
const static TileCollision(*placement_functions[])(Tilemap* tilemap, SensedTile* t, Character* guy, int height, const int sensor) = {
    dont_call_me, // Bottom sensors are special case
    top_sensor_placement,
    right_sensor_placement,
    left_sensor_placement
};

// Contingent on X=0 and Y=1
const static byte tile_flip_flag_for_dim[] = { TILE_FLIP_X, TILE_FLIP_Y };

static TileCollision process_sensor(TestScene* s, SensedTile* t, const int sensor_dir, const int sensor, const int dim) {
    if (t->indices_are_valid.x[sensor + X] && t->indices_are_valid.x[sensor + Y]) {

        TileIndex tile_index = tile_at(&s->test_tilemap, &t->tilespace, sensor);

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
                return placement_functions[sensor_dir](&s->test_tilemap, t, &s->guy, height, sensor);
            }
        }
    }

    TileCollision result;
    result.hit = false;
    result.new_position = s->guy.position.x[dim];

    return result;
}

void scene_test_initialize(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    // Testing physics!!!!
    data->gravity = 1.15f; // In pixels per frame per frame
    data->terminal_velocity = 14.3f;

    BENCH_START(loading_crattle)
    data->guy.textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->guy.textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->guy.textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    BENCH_END(loading_crattle)

    default_character(&data->guy);
    data->guy.position.simd = _mm_set1_ps(45.0f + 32.0f * 5.0f);
    data->guy.position.x[Y] = 120.0f;
    data->animation_frame = 0;
    data->flip = SDL_FLIP_NONE;
    data->flip = false;
    data->dy = 0; // pixels per second
    data->jump_acceleration = 20.0f;

    BENCH_START(loading_tiles)
    // data->test_tilemap.tiles  = malloc(sizeof(TEST_TILEMAP));
    data->test_tilemap.tiles = TEST_TILEMAP;
    data->test_tilemap.width  = 50;
    data->test_tilemap.height = 25;

    SDL_Surface* tiles_image = load_image(ASSET_TERRAIN_TESTGROUND2_PNG);
    data->test_tilemap.tiles_per_row = tiles_image->w / 32;
    data->test_tilemap.tex = SDL_CreateTextureFromSurface(game->renderer, tiles_image);
    free_image(tiles_image);
    BENCH_END(loading_tiles)

    // TODO oh god testing audio
    BENCH_START(loading_sound)
    data->wave = open_and_play_music(&game->audio);
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
    // JUMP
    if (just_pressed(&game->controls, C_UP)) {
        s->guy.grounded = false;
        s->dy += s->jump_acceleration;
    }
    // TEST ANGLE
    /*
    if (game->controls.this_frame[C_SPACE]) {
        s->guy.ground_angle += 2;
        if (s->guy.ground_angle >= 360)
            s->guy.ground_angle -= 360;
    }
    */
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

    // Actually move the dude (ground_speed is just x for now)
    __m128 movement = {s->guy.ground_speed, s->dy, 0.0f, 0.0f};
    s->guy.position.simd = _mm_add_ps(s->guy.position.simd, movement);

    // Temporary ground @ 0px
    if (s->guy.position.x[1] < 0) {
        s->guy.position.x[1] = 0;
        s->dy = 0;
    }

    // ==== COLLISION BEGINS! ====
    {
        // === These are needed for all sensor operations ===
        // tilemap dimensions twice (w,h,w,h)
        vec4i tilemap_dim;
        tilemap_dim.simd = _mm_set_epi32(
            s->test_tilemap.height, s->test_tilemap.width,
            s->test_tilemap.height, s->test_tilemap.width
        );

        vec4 guy_new_x_position;
        guy_new_x_position.x[X] = s->guy.position.x[X];
        guy_new_x_position.x[Y] = s->guy.old_position.x[Y];
        vec4 guy_new_y_position;
        guy_new_y_position.x[X] = s->guy.old_position.x[X];
        guy_new_y_position.x[Y] = s->guy.position.x[Y];

        // == LEFT SENSORS ==
        SensedTile t;
        sense_tile(&guy_new_x_position, &tilemap_dim, &s->guy.left_sensors, &t);
        TileCollision l_collision_1 = process_sensor(s, &t, LEFT_SENSOR, SENSOR_1, X);
        TileCollision l_collision_2 = process_sensor(s, &t, LEFT_SENSOR, SENSOR_2, X);
        bool left_hit = l_collision_1.hit || l_collision_2.hit;

        // == RIGHT SENSORS ==
        sense_tile(&guy_new_x_position, &tilemap_dim, &s->guy.right_sensors, &t);
        TileCollision r_collision_1 = process_sensor(s, &t, RIGHT_SENSOR, SENSOR_1, X);
        TileCollision r_collision_2 = process_sensor(s, &t, RIGHT_SENSOR, SENSOR_2, X);
        bool right_hit = r_collision_1.hit || r_collision_2.hit;

        // == TOP SENSORS ==
        sense_tile(&guy_new_y_position, &tilemap_dim, &s->guy.top_sensors, &t);
        TileCollision t_collision_1 = process_sensor(s, &t, TOP_SENSOR, SENSOR_1, Y);
        TileCollision t_collision_2 = process_sensor(s, &t, TOP_SENSOR, SENSOR_2, Y);
        bool top_hit = t_collision_1.hit || t_collision_2.hit;

        if (left_hit)
            s->guy.position.x[X] = fmaxf(l_collision_1.new_position, l_collision_2.new_position);
        if (right_hit)
            s->guy.position.x[X] = fminf(r_collision_1.new_position, r_collision_2.new_position);
        if (top_hit) {
            s->guy.position.x[Y] = fminf(t_collision_1.new_position, t_collision_2.new_position);
            s->dy = 0;
        }

        // == BOTTOM SENSORS ==
        sense_tile(&s->guy.position, &tilemap_dim, &s->guy.bottom_sensors, &t);
        TileCollision b_collision_1 = process_bottom_sensor(s, &t, SENSOR_1);
        TileCollision b_collision_2 = process_bottom_sensor(s, &t, SENSOR_2);

        s->guy.grounded = b_collision_1.hit || b_collision_2.hit;
        if (s->guy.grounded)
            s->dy = 0;
        if (b_collision_1.hit && b_collision_2.hit) {
            s->guy.position.x[Y] = fmaxf(b_collision_1.new_position, b_collision_2.new_position);
            s->guy.ground_angle = atan2f(
                b_collision_2.new_position - b_collision_1.new_position,
                s->guy.bottom_sensors.x[S2X] - s->guy.bottom_sensors.x[S1X]
            ) / M_PI * 180;
        }
        else if (b_collision_1.hit)
            s->guy.position.x[Y] = b_collision_1.new_position;
        else if (b_collision_2.hit)
            s->guy.position.x[Y] = b_collision_2.new_position;
        else
            s->guy.grounded = false;
    }
    // === END OF COLLISION ===

    // Animate here so that animation freezes along with freeze frame
    if (s->animate) {
        if (game->frame_count % 5 == 0)
            s->animation_frame += 1;
        if (s->animation_frame >= 9)
            s->animation_frame = 1;
    }
    else s->animation_frame = 0;

    // Test sound effect
    if (just_pressed(&game->controls, C_UP)) {
        s->test_sound.samples_pos = 0;
        game->audio.oneshot_waves[0] = &s->test_sound;
    }

    // This should happen after all entities are done interacting (riiight at the end of the frame)
    s->guy.old_position = s->guy.position;

    // Swap to offset viewer on F1 press
    if (just_pressed(&game->controls, C_F1))
        switch_scene(game, SCENE_OFFSET_VIEWER);
}

void scene_test_render(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;
#ifdef _DEBUG
    int b1_tilespace_x, b1_tilespace_y, l1_tilespace_x, l1_tilespace_y, r1_tilespace_x, r1_tilespace_y;
    int b2_tilespace_x, b2_tilespace_y, l2_tilespace_x, l2_tilespace_y, r2_tilespace_x, r2_tilespace_y;
    if (debug_pause) {
        // DEBUG: b-sensor 1 tile index
        int sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[0];
        int sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[1];
        b1_tilespace_x = sense_x / 32;
        b1_tilespace_y = sense_y / 32;
        // DEBUG: b-sensor 2 tile index
        sense_x = s->guy.position.x[0] + s->guy.bottom_sensors.x[2];
        sense_y = s->guy.position.x[1] + s->guy.bottom_sensors.x[3];
        b2_tilespace_x = sense_x / 32;
        b2_tilespace_y = sense_y / 32;
        // DEBUG: l-sensor 1 tile index
        sense_x = s->guy.position.x[0] + s->guy.left_sensors.x[0];
        sense_y = s->guy.position.x[1] + s->guy.left_sensors.x[1];
        l1_tilespace_x = sense_x / 32;
        l1_tilespace_y = sense_y / 32;
        // DEBUG: l-sensor 2 tile index
        sense_x = s->guy.position.x[0] + s->guy.left_sensors.x[2];
        sense_y = s->guy.position.x[1] + s->guy.left_sensors.x[3];
        l2_tilespace_x = sense_x / 32;
        l2_tilespace_y = sense_y / 32;
        // DEBUG: r-sensor 1 tile index
        sense_x = s->guy.position.x[0] + s->guy.right_sensors.x[0];
        sense_y = s->guy.position.x[1] + s->guy.right_sensors.x[1];
        r1_tilespace_x = sense_x / 32;
        r1_tilespace_y = sense_y / 32;
        // DEBUG: r-sensor 2 tile index
        sense_x = s->guy.position.x[0] + s->guy.right_sensors.x[2];
        sense_y = s->guy.position.x[1] + s->guy.right_sensors.x[3];
        r2_tilespace_x = sense_x / 32;
        r2_tilespace_y = sense_y / 32;
    }
#endif

    // Draw tiles!
    vec4i tilespace;
    for (int i = 0; i < s->test_tilemap.width; i++) {
        tilespace.x[X] = i;
        for (int j = 0; j < s->test_tilemap.height; j++) {
            tilespace.x[Y] = j;

            SDL_Rect tile_rect = { i * 32, game->window_height - j * 32 - 32, 32, 32 };
            TileIndex tile_index = tile_at(&s->test_tilemap, &tilespace, 0);

            if (!(tile_index.flags & NOT_A_TILE)) {
                SDL_Rect src = { tile_index.index, 0, 32, 32 };

                // OPTIMIZE out this while loop with multiplication lol?
                while (src.x >= s->test_tilemap.tiles_per_row) {
                    src.x -= s->test_tilemap.tiles_per_row;
                    src.y += 1;
                }
                src.x *= 32;
                src.y *= 32;

                SDL_RendererFlip flip = (tile_index.flags & TILE_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                SDL_RenderCopyEx(game->renderer, s->test_tilemap.tex, &src, &tile_rect, 0, NULL, flip);
            }

#ifdef _DEBUG
            if (debug_pause) {
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
                    SDL_SetRenderDrawColor(game->renderer, 255, 0, 255, 128);
                    SDL_RenderDrawRect(game->renderer, &tile_rect);
                }

                SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
            }
#endif
        }
    }

    // DRAW GUY
    SDL_Rect src = { s->animation_frame * 90, 0, 90, 90 };
    SDL_Rect dest = {
        s->guy.position.x[X] - s->guy.center_x,
        game->window_height - s->guy.position.x[Y] - s->guy.center_y,

        90, 90
    };
    // Chearfully assume that center_y is right after center_x and aligned the same as SDL_Point...
    SDL_Point* center = (SDL_Point*)&s->guy.center_x;
    for (int i = 0; i < 3; i++)
        SDL_RenderCopyEx(game->renderer, s->guy.textures[i], &src, &dest, 360 - s->guy.ground_angle, center, s->flip);

    // Draw sensors
#ifdef _DEBUG
    if (debug_pause) {
        dest.x += s->guy.center_x;
        SDL_Rect offset = { 0, 0, 1, 1 };
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor(game->renderer, &r, &b, &g, &a);

        // TOP
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 255, 255);
        offset.x = dest.x + s->guy.top_sensors.x[S1X];
        offset.y = dest.y + s->guy.center_y - s->guy.top_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + s->guy.top_sensors.x[S2X];
        offset.y = dest.y + s->guy.center_y - s->guy.top_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);

        // BOTTOM
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 255, 255);
        offset.x = dest.x + s->guy.bottom_sensors.x[S1X];
        offset.y = dest.y + s->guy.center_y - s->guy.bottom_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + s->guy.bottom_sensors.x[S2X];
        offset.y = dest.y + s->guy.center_y - s->guy.bottom_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);

        // LEFT
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 0, 255);
        offset.x = dest.x + s->guy.left_sensors.x[S1X];
        offset.y = dest.y + s->guy.center_y - s->guy.left_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + s->guy.left_sensors.x[S2X];
        offset.y = dest.y + s->guy.center_y - s->guy.left_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);

        // RIGHT
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 0, 255);
        offset.x = dest.x + s->guy.right_sensors.x[S1X];
        offset.y = dest.y + s->guy.center_y - s->guy.right_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + s->guy.right_sensors.x[S2X];
        offset.y = dest.y + s->guy.center_y - s->guy.right_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);


        SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
    }
#endif
}

void scene_test_cleanup(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    SDL_DestroyTexture(data->test_tilemap.tex);
    SDL_DestroyTexture(data->guy.textures[0]);
    SDL_DestroyTexture(data->guy.textures[1]);
    SDL_DestroyTexture(data->guy.textures[2]);
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL; // This is from open_and_play_music, which sucks and should be removed asap.
    free(data->wave->samples);
    free(data->wave);
    free(data->test_sound.samples); // This one is local to this function so only the samples are malloced.
}
