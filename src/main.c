#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "SDL.h"
#include "assets.h"
#include "types.h"
#include "sound.h"

// Disgusting global window variable so that I can shit out message boxes
// from wherever I want.
SDL_Window* main_window;

// Controls stuff
enum Control {
    C_UP, C_DOWN, C_LEFT, C_RIGHT,
    NUM_CONTROLS
};

typedef struct {
    bool last_frame[NUM_CONTROLS];
    bool this_frame[NUM_CONTROLS];
} Controls;

bool just_pressed(Controls* controls, enum Control key) {
    return controls->this_frame[key] && !controls->last_frame[key];
}

// Character stuff
typedef struct {
    union vec4 position;
    float ground_speed;
    float ground_speed_max;
    float ground_acceleration;
    float ground_deceleration;
} Character;

int main(int argc, char** argv) {
    SDL_Window* window;
    SDL_Renderer* renderer;
    AudioQueue audio;
    Controls controls;

    // Testing physics!!!!
    float window_width = 640.0f, window_height = 480.0f;
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
    const int num_tiles = 15;
    // just one dimensional for now... to test slopes.
    int test_tilemap[15] = {
        0,0,0,0,0,0,0,1,2,2,2,3,0,0,0
    };

    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));
    open_assets_file();
    initialize_sound(&audio);
    memset(&controls, 0, sizeof(controls));

    window = SDL_CreateWindow(
        "Niiiice",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        window_width, window_height,
        0
    );
    main_window = window;
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);

    SDL_Texture* tiles = load_texture(renderer, ASSET_TERRAIN_TESTGROUND_PNG);

    SDL_Texture* textures[3] = {
        load_texture(renderer, ASSET_CRATTLECRUTE_BACK_FOOT_PNG),
        load_texture(renderer, ASSET_CRATTLECRUTE_BODY_PNG),
        load_texture(renderer, ASSET_CRATTLECRUTE_FRONT_FOOT_PNG)
    };

    // TODO oh god testing audio
    AudioWave* wave = open_and_play_music(&audio);
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

        memcpy(controls.last_frame, controls.this_frame, sizeof(controls.last_frame));
        memset(controls.this_frame, 0, sizeof(controls.this_frame));

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            }
        }

        controls.this_frame[C_UP]    = keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W];
        controls.this_frame[C_DOWN]  = keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S];
        controls.this_frame[C_LEFT]  = keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A];
        controls.this_frame[C_RIGHT] = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];

        // Test movement (for controls' sake)
        dy -= gravity; // times 1 frame
        // Get accelerations from controls
        if (controls.this_frame[C_LEFT]) {
            guy.ground_speed -= guy.ground_acceleration;
            flip = SDL_FLIP_HORIZONTAL;
        }
        if (controls.this_frame[C_RIGHT]) {
            guy.ground_speed += guy.ground_acceleration;
            flip = SDL_FLIP_NONE;
        }
        if (just_pressed(&controls, C_UP)) {
            dy += jump_acceleration;
        }
        // TODO having ground_deceleration > ground_acceleration will have a weird effect here.
        if (!controls.this_frame[C_LEFT] && !controls.this_frame[C_RIGHT]) {
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
        animate = controls.this_frame[C_LEFT] || controls.this_frame[C_RIGHT];

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
        const int y_offset_to_center = 75;
        const int x_offset_to_center = 75 / 2;
        if (guy.position.x[1] < 100 + y_offset_to_center) {
            int tile_location = ((int)guy.position.x[0] + x_offset_to_center) / 32;
            if (tile_location < 15) {
                int tile_index = test_tilemap[tile_location];
                TileHeights* collision_heights = &COLLISION_TERRAIN_TESTGROUND[tile_index];
                int* heights = collision_heights->top2down;
                int x_position_within_tile = (guy.position.x[0] + x_offset_to_center) - tile_location * 32;

                guy.position.x[1] = (100 - 32) + heights[x_position_within_tile] + y_offset_to_center;
                dy = 0;
            }
        }

        // Test sound effect
        if (just_pressed(&controls, C_UP)) {
            test_sound.samples_pos = 0;
            audio.oneshot_waves[0] = &test_sound;
        }

        // Draw!!! Finally!!!
        SDL_RenderClear(renderer);

        for (int i = 0; i < 15; i++) {
            SDL_Rect src = { test_tilemap[i] * 32, 0, 32, 32 };
            SDL_Rect dest = { i * 32, window_height - 100, 32, 32 };
            SDL_RenderCopy(renderer, tiles, &src, &dest);
        }

        SDL_Rect src = { animation_frame * 90, 0, 90, 90 };
        // The dude's center of mass is still at the corner.
        SDL_Rect dest = { guy.position.x[0], window_height - guy.position.x[1], 90, 90 };
        if (animate) {
            if (frame_count % 5 == 0)
                animation_frame += 1;
            if (animation_frame >= 9)
                animation_frame = 1;
        }
        else animation_frame = 0;
        for (int i = 0; i < 3; i++)
            SDL_RenderCopyEx(renderer, textures[i], &src, &dest, 0, NULL, flip);

        SDL_RenderPresent(renderer);

        // ======================= Cap Framerate =====================
        Uint64 frame_end = SDL_GetPerformanceCounter();
        Uint64 frame_ms = (frame_end - frame_start) * milliseconds_per_tick;

        if (frame_ms < 17) {
            SDL_Delay(17 - frame_ms);
        }
    }

    SDL_PauseAudio(true);
    SDL_DestroyWindow(window);
    // The wave is malloced, and the samples are malloced by stb_vorbis.
    free(wave->samples);
    free(wave);
    free(test_sound.samples); // This one is local to this function so only the samples are malloced.

    SDL_Quit();
    return 0;
}
