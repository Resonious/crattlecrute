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
} TestScene;

static const int test_tilemap[] = {
    6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,6,
    6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,6,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,1,2,11,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,1,2,11,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,-1,-1,1,2,0,0,0,0,11,10,-1,-1,-1,9,
    9,-1,-1,-1,-1,-1,1,2,3,9,-1,-1,-1,-1,-1,12,11,-1,-1,9,
    9,-1,-1,-1,1,13,3,9,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,0,0,0,3,4,4,4,9,11,10,-1,-1,-1,-1,-1,-1,-1,-1,9,
    9,4,4,9,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,9
};

void scene_test_initialize(TestScene* data, Game* game) {
    // Testing physics!!!!
    data->gravity = 1.15f; // In pixels per frame per frame
    data->terminal_velocity = 14.3f;

    data->guy.ground_speed = 0.0f;
    data->guy.ground_speed_max = 6.0f;
    data->guy.ground_acceleration = 1.15f;
    data->guy.ground_deceleration = 1.1f;
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

    BENCH_START(loading_crattle)
    data->guy.textures[0] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG);
    data->guy.textures[1] = load_texture(game->renderer, ASSET_CRATTLECRUTE_BODY_PNG);
    data->guy.textures[2] = load_texture(game->renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG);
    BENCH_END(loading_crattle)

    // TODO oh god testing audio
    BENCH_START(loading_sound)
    data->wave = open_and_play_music(&game->audio);
    data->test_sound = decode_ogg(ASSET_SOUNDS_EXPLOSION_OGG);
    BENCH_END(loading_sound)
}

void scene_test_update(TestScene* s, Game* game) {
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
        const int y_offset_to_center = 75;
        const int x_offset_to_center = 75 / 2;
        const int guy_x = (int)s->guy.position.x[0] + x_offset_to_center;
        const int guy_y = (int)s->guy.position.x[1] - y_offset_to_center;
        const int tile_x = guy_x / 32;
        const int tile_y = (game->window_height - guy_y) / 32;
        if (tile_y >= 0 && tile_y < 15 && tile_x >= 0 && tile_x < 20) {
            const int tile_index = test_tilemap[tile_y * 20 + tile_x];
            if (tile_index >= 0) {
                TileHeights* collision_heights = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_heights->top2down;
                int x_position_within_tile = guy_x - tile_x * 32;
                s->guy.position.x[1] = (game->window_height - tile_y * 32 - 32) + heights[x_position_within_tile] + y_offset_to_center;
                s->dy = 0;
            }
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

void scene_test_render(TestScene* s, Game* game) {
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 15; j++) {
            int tile_index = test_tilemap[j * 20 + i];
            if (tile_index == -1) continue;

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
            SDL_Rect dest = { i * 32, j * 32, 32, 32 };

            SDL_RenderCopy(game->renderer, s->tiles, &src, &dest);
        }
    }

    SDL_Rect src = { s->animation_frame * 90, 0, 90, 90 };
    // The dude's center of mass is still at the corner.
    SDL_Rect dest = { s->guy.position.x[0], game->window_height - s->guy.position.x[1], 90, 90 };
    if (s->animate) {
        if (game->frame_count % 5 == 0)
            s->animation_frame += 1;
        if (s->animation_frame >= 9)
            s->animation_frame = 1;
    }
    else s->animation_frame = 0;
    for (int i = 0; i < 3; i++)
        SDL_RenderCopyEx(game->renderer, s->guy.textures[i], &src, &dest, 0, NULL, s->flip);

}

void scene_test_cleanup(TestScene* data, Game* game) {
    SDL_DestroyTexture(data->tiles);
    SDL_DestroyTexture(data->guy.textures[0]);
    SDL_DestroyTexture(data->guy.textures[1]);
    SDL_DestroyTexture(data->guy.textures[2]);
    game->audio.oneshot_waves[0] = NULL;
    game->audio.looped_waves[0] = NULL; // This is from open_and_play_music, which sucks and should be removed asap.
    free(data->wave->samples);
    free(data->wave);
    free(data->test_sound.samples); // This one is local to this function so only the samples are malloced.
}
