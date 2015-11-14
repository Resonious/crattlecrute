#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "SDL.h"
#include "assets.h"
#include "types.h"
#include "sound.h"
#include <xmmintrin.h>

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
    return controls->this_frame[key] && controls->last_frame[key];
}

union vec4 {
    __m128 simd;
    float f[4];
};

// Character stuff
typedef struct {
    union vec4 position;
} Character;

int main(int argc, char** argv) {
    SDL_Window* window;
    SDL_Renderer* renderer;
    AudioQueue audio;
    Controls controls;

    Character guy;
    guy.position.simd = _mm_set1_ps(10.0f);

    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));
    open_assets_file();
    initialize_sound(&audio);
    memset(&controls, 0, sizeof(controls));

    window = SDL_CreateWindow(
        "Niiiice",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        0
    );
    main_window = window;
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);

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
    // test stuff
    int animation_frame = 0;
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    bool animate = false;

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
        float dx = 0, dy = 0;
        if (controls.this_frame[C_UP])
            dy -= 2;
        if (controls.this_frame[C_DOWN])
            dy += 2;
        if (controls.this_frame[C_LEFT]) {
            dx -= 4;
            flip = SDL_FLIP_HORIZONTAL;
        }
        if (controls.this_frame[C_RIGHT]) {
            dx += 4;
            flip = SDL_FLIP_NONE;
        }
        animate = controls.this_frame[C_LEFT] || controls.this_frame[C_RIGHT];
        __m128 movement = {dx, dy, 0.0f, 0.0f};
        guy.position.simd = _mm_add_ps(guy.position.simd, movement);

        // Test sound effect
        if (just_pressed(&controls, C_UP))
            audio.oneshot_waves[0] = &test_sound;

        // Draw!!! Finally!!!
        SDL_RenderClear(renderer);

        SDL_Rect src = { animation_frame * 90, 0, 90, 90 };
        SDL_Rect dest = { guy.position.f[0], guy.position.f[1], 90, 90 };
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

        if (frame_ms < 17)
            SDL_Delay(17 - frame_ms);
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
