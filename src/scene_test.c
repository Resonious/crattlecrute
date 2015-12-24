#include "scene.h"
#include "game.h"
#include "tilemap.h"

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
    CollisionMap map_collision;
} TestScene;


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
    data->test_tilemap.tiles = TEST_TILEMAP_COMPRESSED;
    data->test_tilemap.width  = 50;
    data->test_tilemap.height = 25;

    SDL_Surface* tiles_image = load_image(ASSET_TERRAIN_TESTGROUND2_PNG);
    data->test_tilemap.tiles_per_row = tiles_image->w / 32;
    data->test_tilemap.tex = SDL_CreateTextureFromSurface(game->renderer, tiles_image);
    free_image(tiles_image);

    data->map_collision.tiles = TEST_TILEMAP;
    data->map_collision.width  = 50;
    data->map_collision.height = 25;
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
            s->map_collision.height, s->map_collision.width,
            s->map_collision.height, s->map_collision.width
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
        TileCollision l_collision_1 = process_sensor(&s->guy, &s->map_collision, &t, LEFT_SENSOR, SENSOR_1, X);
        TileCollision l_collision_2 = process_sensor(&s->guy, &s->map_collision, &t, LEFT_SENSOR, SENSOR_2, X);
        bool left_hit = l_collision_1.hit || l_collision_2.hit;

        // == RIGHT SENSORS ==
        sense_tile(&guy_new_x_position, &tilemap_dim, &s->guy.right_sensors, &t);
        TileCollision r_collision_1 = process_sensor(&s->guy, &s->map_collision, &t, RIGHT_SENSOR, SENSOR_1, X);
        TileCollision r_collision_2 = process_sensor(&s->guy, &s->map_collision, &t, RIGHT_SENSOR, SENSOR_2, X);
        bool right_hit = r_collision_1.hit || r_collision_2.hit;

        // == TOP SENSORS ==
        sense_tile(&guy_new_y_position, &tilemap_dim, &s->guy.top_sensors, &t);
        TileCollision t_collision_1 = process_sensor(&s->guy, &s->map_collision, &t, TOP_SENSOR, SENSOR_1, Y);
        TileCollision t_collision_2 = process_sensor(&s->guy, &s->map_collision, &t, TOP_SENSOR, SENSOR_2, Y);
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
        TileCollision b_collision_1 = process_bottom_sensor(&s->guy, &s->map_collision, &t, SENSOR_1);
        TileCollision b_collision_2 = process_bottom_sensor(&s->guy, &s->map_collision, &t, SENSOR_2);

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

#ifdef _DEBUG
static void draw_debug_borders(Game* game, SDL_Rect* dest, int i, int j,
    int l1_tilespace_x, int l1_tilespace_y, int l2_tilespace_x, int l2_tilespace_y,
    int r1_tilespace_x, int r1_tilespace_y, int r2_tilespace_x, int r2_tilespace_y,
    int b1_tilespace_x, int b1_tilespace_y, int b2_tilespace_x, int b2_tilespace_y
) {
    // DEBUG: highlight tiles that sensors cover
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
    {
        int i = 0;
        vec4i dest;
        dest.simd = _mm_set_epi32(32, 32, 0, 0);
        const int width_in_pixels = s->test_tilemap.width * 32;
        const int height_in_pixels = s->test_tilemap.height * 32;
        int* tile_data = s->test_tilemap.tiles;

        while (dest.x[Y] < height_in_pixels) {
            int tile_count = tile_data[i];

            if (tile_count > 0) {
                // Repetition
                i += 1;

                TileIndex repeated_index = tile_from_int(tile_data[i]);
                SDL_Rect src = tile_src_rect(&repeated_index, &s->test_tilemap);

                // Draw the tile `tile_count` times
                for (int j = 0; j < tile_count; j++) {
                    draw_tile(game, &s->test_tilemap, &repeated_index, &src, &dest.rect);
#ifdef _DEBUG
                    if (debug_pause) draw_debug_borders(
                        game, &dest.rect, dest.x[X] / 32, dest.x[Y] / 32,
                        l1_tilespace_x, l1_tilespace_y, l2_tilespace_x, l2_tilespace_y,
                        r1_tilespace_x, r1_tilespace_y, r2_tilespace_x, r2_tilespace_y,
                        b1_tilespace_x, b1_tilespace_y, b2_tilespace_x, b2_tilespace_y
                    );
#endif
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
                    SDL_Rect src = tile_src_rect(&tile_index, &s->test_tilemap);
                    draw_tile(game, &s->test_tilemap, &tile_index, &src, &dest.rect);
#ifdef _DEBUG
                    if (debug_pause) draw_debug_borders(
                        game, &dest.rect, dest.x[X] / 32, dest.x[Y] / 32,
                        l1_tilespace_x, l1_tilespace_y, l2_tilespace_x, l2_tilespace_y,
                        r1_tilespace_x, r1_tilespace_y, r2_tilespace_x, r2_tilespace_y,
                        b1_tilespace_x, b1_tilespace_y, b2_tilespace_x, b2_tilespace_y
                    );
#endif
                    increment_tilespace(&dest, width_in_pixels, 32);
                }
            }// if (tile_count > 0)
        }// while (dest.y < height)
    }// block for render

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
