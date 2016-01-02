#include "scene.h"
#include "game.h"
#include "assets.h"
#include "coords.h"

#ifdef _DEBUG
extern bool debug_pause;
extern int b1_tilespace_x, b1_tilespace_y, l1_tilespace_x, l1_tilespace_y, r1_tilespace_x, r1_tilespace_y;
extern int b2_tilespace_x, b2_tilespace_y, l2_tilespace_x, l2_tilespace_y, r2_tilespace_x, r2_tilespace_y;
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
    Map* map;
} TestScene;


void scene_test_initialize(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    // Testing physics!!!!
    data->gravity = 1.15f; // In pixels per frame per frame
    data->terminal_velocity = 16.3f;

    BENCH_START(loading_crattle)
    data->guy.textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->guy.textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->guy.textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    BENCH_END(loading_crattle)

    default_character(&data->guy);
    data->guy.position.simd = _mm_set1_ps(45.0f + 32.0f * 5.0f);
    data->guy.position.x[Y] = 140.0f;
    data->guy.position.x[2] = 0.0f;
    data->guy.position.x[3] = 0.0f;
    data->animation_frame = 0;
    data->flip = SDL_FLIP_NONE;
    data->flip = false;
    data->dy = 0; // pixels per second
    data->jump_acceleration = 20.0f;

    BENCH_START(loading_tiles)
    // TODO NEXT PLZ ASSET CACHE!
    // data->map = &MAP_TEST2;
    data->map = malloc(2048);
    load_map(ASSET_MAPS_TEST2_CM, data->map);
    for (int i = 0; i < data->map->number_of_tilemaps; i++) {
        Tilemap* tilemap = &data->map->tilemaps[i];
        if (tilemap->tex == NULL)
            tilemap->tex = load_texture(game->renderer, tilemap->tex_asset);
    }
    BENCH_END(loading_tiles)

    // TODO oh god testing audio
    BENCH_START(loading_sound)
    data->wave = open_and_play_music(&game->audio);
    data->test_sound = decode_ogg(ASSET_SOUNDS_JUMP_OGG);
    BENCH_END(loading_sound)
}

void scene_test_update(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;
    // Test movement (actually workin' on this now)
    s->guy.dy -= s->gravity; // times 1 frame
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
    if (s->guy.grounded) {
        if (s->guy.jumped)
            s->guy.jumped = false;
        if (just_pressed(&game->controls, C_UP)) {
            s->guy.grounded = false;
            s->guy.jumped = true;
            s->guy.dy = s->jump_acceleration;

            // Test jump sound effect
            if (just_pressed(&game->controls, C_UP)) {
                s->test_sound.samples_pos = 0;
                game->audio.oneshot_waves[0] = &s->test_sound;
            }
        }
    }
    else if (just_released(&game->controls, C_UP) && s->guy.jumped) {
        const float jump_cancel_dy = 10.0f;
        if (s->guy.dy > jump_cancel_dy)
            s->guy.dy = jump_cancel_dy;
        s->guy.jumped = false;
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
    if (s->guy.dy < -s->terminal_velocity)
        s->guy.dy = -s->terminal_velocity;

    // Actually move the dude (ground_speed is just x for now)
    __m128 movement = {s->guy.ground_speed + s->guy.slide_speed, s->guy.dy, 0.0f, 0.0f};
    s->guy.position.simd = _mm_add_ps(s->guy.position.simd, movement);

    // Temporary ground @ 0px
    if (s->guy.position.x[1] < 0) {
        s->guy.position.x[1] = 0;
        s->guy.dy = 0;
    }

    // COLLISION!
    /* THIS is 2 passes done kinda crudely. It doesn't help me fix this fucking bug so I'm not using it.
    vec4 half_displacement;
    half_displacement.simd = _mm_div_ps(_mm_sub_ps(s->guy.position.simd, s->guy.old_position.simd), _mm_set1_ps(2.0f));
    s->guy.position.simd = _mm_add_ps(s->guy.old_position.simd, half_displacement.simd);
    collide_character(&s->guy, &s->map->tile_collision);
    s->guy.position.simd = _mm_add_ps(s->guy.position.simd, half_displacement.simd);
    collide_character(&s->guy, &s->map->tile_collision);
    */
    collide_character(&s->guy, &s->map->tile_collision);
    slide_character(s->gravity, &s->guy);

    // Animate here so that animation freezes along with freeze frame
    if (s->animate) {
        if (game->frame_count % 5 == 0)
            s->animation_frame += 1;
        if (s->animation_frame >= 9)
            s->animation_frame = 1;
    }
    else s->animation_frame = 0;

    // Follow player with camera
    // game->camera.simd = _mm_mul_ps(s->guy.position.simd, _mm_set_ps(0, 0, 1, 1));
    // Actually, move the camera lol
    const float cam_speed = s->guy.ground_speed_max * 0.8f;
    if (game->controls.this_frame[C_W])
        game->camera.x[Y] += cam_speed;
    if (game->controls.this_frame[C_S])
        game->camera.x[Y] -= cam_speed;
    if (game->controls.this_frame[C_D])
        game->camera.x[X] += cam_speed;
    if (game->controls.this_frame[C_A])
        game->camera.x[X] -= cam_speed;

    // This should happen after all entities are done interacting (riiight at the end of the frame)
    s->guy.old_position = s->guy.position;

    // Swap to offset viewer on F1 press
    if (just_pressed(&game->controls, C_F1))
        switch_scene(game, SCENE_OFFSET_VIEWER);
}

void scene_test_render(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;
    /* == Keeping this around in case I want it ==
#ifdef _DEBUG
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
    */

    // Draw WHOLE MAP
    draw_map(game, s->map);

    // DRAW GUY
    SDL_Rect src = { s->animation_frame * 90, 0, 90, 90 };
    SDL_Rect dest = {
        s->guy.position.x[X] - s->guy.center_x - game->camera.x[X],
        game->window_height - s->guy.position.x[Y] - s->guy.center_y + game->camera.x[Y],

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

        // MIDDLE
        SDL_SetRenderDrawColor(game->renderer, 0, 255, 255, 255);
        offset.x = dest.x + s->guy.middle_sensors.x[S1X];
        offset.y = dest.y + s->guy.center_y - s->guy.middle_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + s->guy.middle_sensors.x[S2X];
        offset.y = dest.y + s->guy.center_y - s->guy.middle_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);


        SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
    }
#endif
}

void scene_test_cleanup(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    for (int i = 0; i < data->map->number_of_tilemaps; i++) {
        Tilemap* tilemap = &data->map->tilemaps[i];
        if (tilemap->tex != NULL) {
            SDL_DestroyTexture(tilemap->tex);
            tilemap->tex = NULL;
        }
    }
    SDL_DestroyTexture(data->guy.textures[0]);
    SDL_DestroyTexture(data->guy.textures[1]);
    SDL_DestroyTexture(data->guy.textures[2]);
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL; // This is from open_and_play_music, which sucks and should be removed asap.
    free(data->wave->samples);
    free(data->wave);
    free(data->test_sound.samples); // This one is local to this function so only the samples are malloced.
}
