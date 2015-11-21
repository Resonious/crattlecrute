#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "game.h"

// Disgusting global window variable so that I can shit out message boxes
// from wherever I want.
SDL_Window* main_window;

// Character stuff
typedef struct {
    union vec4 position;
    float ground_speed;
    float ground_speed_max;
    float ground_acceleration;
    float ground_deceleration;
} Character;

int main(int argc, char** argv) {
    Game game;
    memset(&game, 0, sizeof(Game));

    game.window_width = 640.0f;
    game.window_height = 480.0f;
    // Testing physics!!!!
    float gravity = 1.15f; // In pixels per frame per frame
    float terminal_velocity = 14.3f;
    Character guy;
    guy.ground_speed = 0.0f;
    guy.ground_speed_max = 6.0f;
    guy.ground_acceleration = 1.15f;
    guy.ground_deceleration = 1.1f;
    guy.position.simd = _mm_set1_ps(10.0f);
    guy.position.x[1] = 500.0f;

    // Testing tiles (for physics, mainly)!!!!
    int test_tilemap[] = {
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

    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));
    open_assets_file();
    initialize_sound(&game.audio);
    memset(&game.controls, 0, sizeof(game.controls));

    game.window = SDL_CreateWindow(
        "Niiiice",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        game.window_width, game.window_height,
        0
    );
    main_window = game.window;
    game.renderer = SDL_CreateRenderer(game.window, -1, 0);
    if (game.renderer == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), game.window);
    SDL_SetRenderDrawColor(game.renderer, 20, 20, 20, 255);

    SDL_Texture* tiles = load_texture(game.renderer, ASSET_TERRAIN_TESTGROUND_PNG);

    SDL_Texture* textures[3] = {
        load_texture(game.renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG),
        load_texture(game.renderer, ASSET_CRATTLECRUTE_BODY_PNG),
        load_texture(game.renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG)
    };

    // TODO oh god testing audio
    AudioWave* wave = open_and_play_music(&game.audio);
    AudioWave test_sound = decode_ogg(ASSET_SOUNDS_EXPLOSION_OGG);

    int key_count;
    const Uint8* keys = SDL_GetKeyboardState(&key_count);

    // Main loop bitch
    SDL_Event event;
    bool running = true;
    Uint64 milliseconds_per_tick = 1000 / SDL_GetPerformanceFrequency();
    Uint64 frame_count = 0;
    Uint64 last_frame_ms = 17;
    // test stuff
    int animation_frame = 0;
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    bool animate = false;
    float dy = 0; // pixels per second
    float jump_acceleration = 20.0f;

    while (running) {
        Uint64 frame_start = SDL_GetPerformanceCounter();
        frame_count += 1;

        memcpy(game.controls.last_frame, game.controls.this_frame, sizeof(game.controls.last_frame));
        memset(game.controls.this_frame, 0, sizeof(game.controls.this_frame));

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            }
        }

        game.controls.this_frame[C_UP]    = keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W];
        game.controls.this_frame[C_DOWN]  = keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S];
        game.controls.this_frame[C_LEFT]  = keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A];
        game.controls.this_frame[C_RIGHT] = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];

        // Test movement (for controls' sake)
        dy -= gravity; // times 1 frame
        // Get accelerations from controls
        if (game.controls.this_frame[C_LEFT]) {
            guy.ground_speed -= guy.ground_acceleration;
            flip = SDL_FLIP_HORIZONTAL;
        }
        if (game.controls.this_frame[C_RIGHT]) {
            guy.ground_speed += guy.ground_acceleration;
            flip = SDL_FLIP_NONE;
        }
        if (just_pressed(&game.controls, C_UP)) {
            dy += jump_acceleration;
        }
        // TODO having ground_deceleration > ground_acceleration will have a weird effect here.
        if (!game.controls.this_frame[C_LEFT] && !game.controls.this_frame[C_RIGHT]) {
            if (guy.ground_speed > 0) {
                guy.ground_speed -= guy.ground_deceleration;
                if (guy.ground_speed < 0)
                    guy.ground_speed = 0;
            }
            else if (guy.ground_speed < 0) {
                guy.ground_speed += guy.ground_deceleration;
                if (guy.ground_speed > 0)
                    guy.ground_speed = 0;
            }
        }
        animate = game.controls.this_frame[C_LEFT] || game.controls.this_frame[C_RIGHT];

        // Cap speeds
        if (guy.ground_speed > guy.ground_speed_max)
            guy.ground_speed = guy.ground_speed_max;
        else if (guy.ground_speed < -guy.ground_speed_max)
            guy.ground_speed = -guy.ground_speed_max;
        if (dy < -terminal_velocity)
            dy = -terminal_velocity;

        // Actually move the dude (ground_speed is just x for now)
        __m128 movement = {guy.ground_speed, dy, 0.0f, 0.0f};
        guy.position.simd = _mm_add_ps(guy.position.simd, movement);

        // Temporary ground @ 20px
        if (guy.position.x[1] < 20) {
            guy.position.x[1] = 20;
            dy = 0;
        }
        // Test out tile collision!!!
        {
            const int y_offset_to_center = 75;
            const int x_offset_to_center = 75 / 2;
            const int guy_x = (int)guy.position.x[0] + x_offset_to_center;
            const int guy_y = (int)guy.position.x[1] - y_offset_to_center;
            const int tile_x = guy_x / 32;
            const int tile_y = (game.window_height - guy_y) / 32;
            if (tile_y >= 0 && tile_y < 15 && tile_x >= 0 && tile_x < 20) {
                const int tile_index = test_tilemap[tile_y * 20 + tile_x];
                if (tile_index >= 0) {
                    TileHeights* collision_heights = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                    int* heights = collision_heights->top2down;
                    int x_position_within_tile = guy_x - tile_x * 32;
                    guy.position.x[1] = (game.window_height - tile_y * 32 - 32) + heights[x_position_within_tile] + y_offset_to_center;
                    dy = 0;
                }
            }
        }

        // Test sound effect
        if (just_pressed(&game.controls, C_UP)) {
            test_sound.samples_pos = 0;
            game.audio.oneshot_waves[0] = &test_sound;
        }

        // Draw!!! Finally!!!
        SDL_RenderClear(game.renderer);

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

                SDL_RenderCopy(game.renderer, tiles, &src, &dest);
            }
        }

        SDL_Rect src = { animation_frame * 90, 0, 90, 90 };
        // The dude's center of mass is still at the corner.
        SDL_Rect dest = { guy.position.x[0], game.window_height - guy.position.x[1], 90, 90 };
        if (animate) {
            if (frame_count % 5 == 0)
                animation_frame += 1;
            if (animation_frame >= 9)
                animation_frame = 1;
        }
        else animation_frame = 0;
        for (int i = 0; i < 3; i++)
            SDL_RenderCopyEx(game.renderer, textures[i], &src, &dest, 0, NULL, flip);

        SDL_RenderPresent(game.renderer);

        // ======================= Cap Framerate =====================
        Uint64 frame_end = SDL_GetPerformanceCounter();
        Uint64 frame_ms = (frame_end - frame_start) * milliseconds_per_tick;

        if (frame_ms < 17) {
            SDL_Delay(17 - frame_ms);
        }
    }

    SDL_PauseAudio(true);
    SDL_DestroyWindow(game.window);
    // The wave is malloced, and the samples are malloced by stb_vorbis.
    free(wave->samples);
    free(wave);
    free(test_sound.samples); // This one is local to this function so only the samples are malloced.

    SDL_Quit();
    return 0;
}
