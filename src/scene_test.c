#include "scene.h"
#include "game.h"
#include "character.h"
#include "assets.h"
#include "coords.h"
#include <stdlib.h>

#ifdef _DEBUG
extern bool debug_pause;
extern int b1_tilespace_x, b1_tilespace_y, l1_tilespace_x, l1_tilespace_y, r1_tilespace_x, r1_tilespace_y;
extern int b2_tilespace_x, b2_tilespace_y, l2_tilespace_x, l2_tilespace_y, r2_tilespace_x, r2_tilespace_y;
#endif

// 5 seconds
#define RECORDED_FRAME_COUNT 60 * 5

typedef struct {
    Controls dummy_controls;
    float gravity;
    float drag;
    Character guy;
    Character guy2;
    AudioWave* music;
    AudioWave* test_sound;
    Map* map;
    int recording_frame;
    int playback_frame;
    bool recorded_controls[RECORDED_FRAME_COUNT][NUM_CONTROLS];
} TestScene;

void scene_test_initialize(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    // Testing physics!!!!
    data->gravity = 1.15f; // In pixels per frame per frame
    data->drag = 0.025f; // Again p/s^2

    SDL_memset(&data->dummy_controls, 0, sizeof(Controls));

    data->recording_frame = -1;
    data->playback_frame = -1;

    BENCH_START(loading_crattle1)
    data->guy.textures[0] = cached_texture(game, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->guy.textures[1] = cached_texture(game, ASSET_CRATTLECRUTE_BODY_PNG);
    data->guy.textures[2] = cached_texture(game, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);

    default_character(&data->guy);
    data->guy.position.x[X] = 150.0f;
    data->guy.position.x[Y] = 170.0f;
    data->guy.position.x[2] = 0.0f;
    data->guy.position.x[3] = 0.0f;

    // ================= TODO TODO TODO =============
    BENCH_END(loading_crattle1);

    BENCH_START(loading_crattle2)
    data->guy2.textures[0] = cached_texture(game, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->guy2.textures[1] = cached_texture(game, ASSET_CRATTLECRUTE_BODY_PNG);
    data->guy2.textures[2] = cached_texture(game, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    default_character(&data->guy2);
    data->guy2.position.x[X] = 250.0f;
    data->guy2.position.x[Y] = 170.0f;
    data->guy2.position.x[2] = 0.0f;
    data->guy2.position.x[3] = 0.0f;
    BENCH_END(loading_crattle2)

    BENCH_START(loading_tiles)
    data->map = cached_map(game, ASSET_MAPS_TEST3_CM);
    BENCH_END(loading_tiles);

    BENCH_START(loading_sound);
    data->music = cached_sound(game, ASSET_MUSIC_ARENA_OGG);
    game->audio.looped_waves[0] = data->music;

    data->test_sound = cached_sound(game, ASSET_SOUNDS_JUMP_OGG);
    data->guy.jump_sound = data->test_sound;
    data->guy2.jump_sound = data->test_sound;
    BENCH_END(loading_sound);
}

#define PERCENT_CHANCE(percent) (rand() < RAND_MAX / (100 / percent))

void scene_test_update(void* vs, Game* game) {
    TestScene* s = (TestScene*)vs;

    controls_pre_update(&s->dummy_controls);
    // Stupid AI
    /*
    if (game->frame_count % 10 == 0) {
        bool* c = s->dummy_controls.this_frame;
        if (PERCENT_CHANCE(50)) {
            int dir = PERCENT_CHANCE(65) ? C_RIGHT : C_LEFT;
            int other_dir = dir == C_RIGHT ? C_LEFT : C_RIGHT;

            c[dir] = !c[dir];
            if (c[dir] && c[other_dir]) {
                c[PERCENT_CHANCE(50) ? dir : other_dir] = false;
            }
        }

        if (s->guy2.left_hit && c[C_LEFT]) {
            c[C_LEFT] = false;
            c[C_RIGHT] = true;
        }
        else if (s->guy2.right_hit && c[C_RIGHT]) {
            c[C_RIGHT] = false;
            c[C_LEFT] = true;
        }

        c[C_UP] = s->guy2.jumped || PERCENT_CHANCE(20);
    }
    */
    // Have guy2 playback recorded controls
    if (s->playback_frame >= 0) {
        if (s->playback_frame >= RECORDED_FRAME_COUNT)
            s->playback_frame = -1;
        else {
            SDL_memcpy(&s->dummy_controls.this_frame, s->recorded_controls[s->playback_frame], sizeof(s->dummy_controls.this_frame));
            s->playback_frame += 1;
        }
    }

    // Update characters
    apply_character_physics(game, &s->guy, &game->controls, s->gravity, s->drag);
    collide_character(&s->guy, &s->map->tile_collision);
    slide_character(s->gravity, &s->guy);
    update_character_animation(&s->guy);

    apply_character_physics(game, &s->guy2, &s->dummy_controls, s->gravity, s->drag);
    collide_character(&s->guy2, &s->map->tile_collision);
    slide_character(s->gravity, &s->guy2);
    update_character_animation(&s->guy2);

    // Follow player with camera
    game->camera_target.x[X] = s->guy.position.x[X] - game->window_width / 2.0f;
    if (s->guy.grounded) {
        game->camera_target.x[Y] = s->guy.position.x[Y] - game->window_height * 0.35f;
        game->follow_cam_y = false;
    }
    else {
        if (s->guy.position.x[Y] - game->camera_target.x[Y] < 1.5f)
            game->follow_cam_y = true;
        if (game->follow_cam_y)
            game->camera_target.x[Y] = s->guy.position.x[Y] - game->window_height * 0.5f;
    }
    // move cam position towards cam target
    game->camera.simd = _mm_add_ps(game->camera.simd, _mm_mul_ps(_mm_sub_ps(game->camera_target.simd, game->camera.simd), _mm_set_ps(0, 0, 0.1f, 0.1f)));
    // Snap to cam target after awhile to stop a certain amount of jerkiness.
    const float cam_alpha = 0.01f;
    vec4 cam_dist_from_target;
    cam_dist_from_target.simd = _mm_sub_ps(game->camera_target.simd, game->camera.simd);
    if (fabsf(cam_dist_from_target.x[X]) < cam_alpha)
        game->camera.x[X] = game->camera_target.x[X];
    if (fabsf(cam_dist_from_target.x[Y]) < cam_alpha)
        game->camera.x[Y] = game->camera_target.x[Y];

    // Record controls!
    if (just_pressed(&game->controls, C_W)) {
        s->recording_frame = -1;
        s->playback_frame  = -1;
    }
    if (s->recording_frame >= 0) {
        if (s->recording_frame >= RECORDED_FRAME_COUNT) {
            // Set last frame to all zeros so the dummy stops (might not wanna do this for networking)
            SDL_memset(s->recorded_controls[s->recording_frame - 1], 0, sizeof(game->controls));
            // -60 so that we can display a message for 60 frames
            // indicating that we are finished recording
            s->recording_frame = -60;
        }
        else {
            SDL_memcpy(s->recorded_controls[s->recording_frame], &game->controls, sizeof(game->controls));
            s->recording_frame += 1;
        }
    }
    else if (s->recording_frame < -1) {
        s->recording_frame += 1;
    }
    else if (s->playback_frame == -1) {
        if (just_pressed(&game->controls, C_A))
            s->recording_frame = 0;
        else if (just_pressed(&game->controls, C_S))
            s->playback_frame = 0;
        else if (just_pressed(&game->controls, C_D))
            // TODO teleportation should be considered a special case if we decide to use multi-pass collision.
            s->guy.position.simd = s->guy2.position.simd;
    }

    // This should happen after all entities are done interacting (riiight at the end of the frame)
    character_post_update(&s->guy);
    character_post_update(&s->guy2);

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

    // Draw guys
    {
        draw_character(game, &s->guy);

        Uint8 r, g, b;
        SDL_GetTextureColorMod(s->guy2.textures[1], &r, &g, &b);
        SDL_SetTextureColorMod(s->guy2.textures[1], 255, 255, 1);
        draw_character(game, &s->guy2);
        SDL_SetTextureColorMod(s->guy2.textures[1], r, g, b);
    }

    // Recording indicator
    char* text = NULL;
    int num = 0;
    if (s->recording_frame >= 0) {
        set_text_color(game, 255, 0, 0);
        text = "Recording (%i)";
        num = RECORDED_FRAME_COUNT - s->recording_frame;
    }
    else if (s->recording_frame < -1) {
        set_text_color(game, 50, 255, 50);
        text = "Done!";
    }
    else if (s->playback_frame >= 0) {
        set_text_color(game, 100, 0, 255);
        text = "Playback (%i)";
        num = RECORDED_FRAME_COUNT - s->playback_frame;
    }
    if (text)
        draw_text_f(game, game->window_width / 2 - 128, game->window_height - 35, text, num);
}

void scene_test_cleanup(void* vdata, Game* game) {
    TestScene* data = (TestScene*)vdata;
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL;
}
