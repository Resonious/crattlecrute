#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "game.h"
#include "assets.h"
#include "scene.h"
#include "coords.h"

// Disgusting global window variable so that I can shit out message boxes
// from wherever I want.
SDL_Window* main_window;
Uint64 ticks_per_second;

#if _DEBUG
    bool debug_pause = false;
#endif

#define SET_CONTROL(this_frame, to) \
                case SDL_SCANCODE_UP:     this_frame[C_UP]    = to; break; \
                case SDL_SCANCODE_DOWN:   this_frame[C_DOWN]  = to; break; \
                case SDL_SCANCODE_LEFT:   this_frame[C_LEFT]  = to; break; \
                case SDL_SCANCODE_RIGHT:  this_frame[C_RIGHT] = to; break; \
\
                case SDL_SCANCODE_W: this_frame[C_W] = to; break; \
                case SDL_SCANCODE_S: this_frame[C_S] = to; break; \
                case SDL_SCANCODE_A: this_frame[C_A] = to; break; \
                case SDL_SCANCODE_D: this_frame[C_D] = to; break; \
\
                case SDL_SCANCODE_P:           this_frame[C_PAUSE]     = to; break; \
                case SDL_SCANCODE_SPACE:       this_frame[C_SPACE]     = to; break; \
                case SDL_SCANCODE_F1:          this_frame[C_F1]        = to; break; \
                case SDL_SCANCODE_LEFTBRACKET: this_frame[C_DEBUG_ADV] = to; break;

void switch_scene(Game* game, int to_scene) {
    assert(to_scene >= 0);
    assert(to_scene < sizeof(SCENES) / sizeof(Scene));

    game->current_scene->cleanup(game->current_scene_data, game);
    memset(game->current_scene_data, 0, SCENE_DATA_SIZE);

    game->current_scene = &SCENES[to_scene];
    game->current_scene->initialize(game->current_scene_data, game);
    // TODO do an update here? (this is the only case where a render can happen WITHOUT an update preceding..)
}

void draw_text_ex(Game* game, int x, int y, char* text, int padding, float scale) {
    const int original_x = x;

    while (*text) {
        // font image is 320x448
        // characters are 20x28
        if (*text == '\n') {
            x = original_x;
            y -= 28 * scale + padding;
        }
        else {
            int char_index = *text;
            SDL_Rect glyph = { (char_index % 16) * 20, (char_index / 16) * 28, 20, 28 };
            SDL_Rect drawto = { x, game->window_height - y, 20, 28 };
            drawto.w = (int)roundf((float)drawto.w * scale);
            drawto.h = (int)roundf((float)drawto.h * scale);
            SDL_RenderCopy(game->renderer, game->font, &glyph, &drawto);

            x += 20.0f * scale + padding;
        }
        text++;
    }
}
void draw_text(Game* game, int x, int y, char* text) {
    draw_text_ex(game, x, y, text, -1, 1.0f);
}

void controls_pre_update(Controls* controls) {
    memcpy(controls->last_frame, controls->this_frame, sizeof(controls->last_frame));
}

int main(int argc, char** argv) {
    ticks_per_second = SDL_GetPerformanceFrequency();
    _MM_SET_ROUNDING_MODE(_MM_ROUND_DOWN);

    Game game;
    memset(&game, 0, sizeof(Game));
    game.window_width = 640.0f;
    game.window_height = 480.0f;
    for (int i = 0; i < NUMBER_OF_ASSETS; i++) {
        game.asset_cache.assets[i].id = ASSET_NOT_LOADED;
    }

#ifdef _DEBUG
    // All scene ids should equal their index
    for (int i = 0; i < sizeof(SCENES) / sizeof(Scene); i++)
        assert(SCENES[i].id == i);
#endif

    game.current_scene = &SCENES[SCENE_TEST];
#ifdef X86
    game.current_scene_data = _aligned_malloc(SCENE_DATA_SIZE, 16);
#else
    game.current_scene_data = malloc(SCENE_DATA_SIZE);
#endif
    game.camera.simd = _mm_set1_ps(0.0f);
    game.camera_target.simd = _mm_set1_ps(0.0f);
    game.follow_cam_y = false;

    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));
    open_assets_file();
    initialize_sound(&game.audio);
    memset(&game.controls, 0, sizeof(game.controls));

    game.window = SDL_CreateWindow(
        "Crattlecrute",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        game.window_width, game.window_height,
        0
    );
    main_window = game.window;
    game.renderer = SDL_CreateRenderer(game.window, -1, 0);
    if (game.renderer == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), game.window);
    SDL_SetRenderDrawColor(game.renderer, 20, 20, 20, 255);

    game.font = load_texture(game.renderer, ASSET_FONT_FONT_PNG);

    int key_count;
    const Uint8* keys = SDL_GetKeyboardState(&key_count);

    // Main loop bitch
    SDL_Event event;
    bool running = true;
    const Uint64 milliseconds_per_tick = 1000 / ticks_per_second;
    const Uint64 ticks_per_frame = ticks_per_second / 60;
    Uint64 frame_start, frame_end;
    Uint64 last_frame_ticks = 0;
    // For getting FPS
    double frame_count_this_second = 0;
    double fps = 60.0;
    double tick_second_counter = 0;
    bool keys_up[NUM_CONTROLS];
    bool keys_down[NUM_CONTROLS];

    game.current_scene->initialize(game.current_scene_data, &game);

    while (running) {
        frame_start = SDL_GetPerformanceCounter();
        game.frame_count += 1;
        frame_count_this_second += 1;
        game.tick_count += last_frame_ticks;
        tick_second_counter += last_frame_ticks;

        controls_pre_update(&game.controls);

        memset(keys_up, 0, sizeof(keys_up));
        memset(keys_down, 0, sizeof(keys_down));

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode) { SET_CONTROL(keys_down, true) }
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.scancode) { SET_CONTROL(keys_up, true) }
                break;
            }
        }
        // UNSURE IF NECESSARY (KEY STICK BUG STILL HAPPENS)
        for (int i = 0; i < NUM_CONTROLS; i++) {
            if (keys_up[i])
                game.controls.this_frame[i] = false;
            else if (keys_down[i])
                game.controls.this_frame[i] = true;

            if (keys_up[i] && keys_down[i]) {
                printf("KEY %i IS DOWN AND UP!!!!\n", i);
            }
        }

#if _DEBUG
        if (just_pressed(&game.controls, C_PAUSE))
            debug_pause = !debug_pause;
        if (!debug_pause || just_pressed(&game.controls, C_DEBUG_ADV))
#endif
        game.current_scene->update(game.current_scene_data, &game);

        // Draw!!! Finally!!!
        SDL_RenderClear(game.renderer);
        game.current_scene->render(game.current_scene_data, &game);
#if _DEBUG
        if (debug_pause) {
            set_text_color(&game, 50, 50, 255);
            draw_text_ex(&game, 32, game.window_height - 32, "FREEZE-FRAME!", 1, 0.7f);
        }
#endif
#if DRAW_FPS
        set_text_color(&game, 255, 255, 20);
        if (tick_second_counter / (double)ticks_per_second >= 1.0) {
            fps = frame_count_this_second / (tick_second_counter / (double)ticks_per_second);
            frame_count_this_second = 0;
            tick_second_counter = 0;
        }
        draw_text_ex_f(&game, game.window_width - 150, game.window_height - 20, -1, 0.7f, "FPS: %.2f", fps);
#endif
        SDL_RenderPresent(game.renderer);

        // ======================= Cap Framerate =====================
        frame_end = SDL_GetPerformanceCounter();
        last_frame_ticks = frame_end - frame_start;

        // Save energy sorta if we have a good amount of time until our 16.666 ms is up.
        while (last_frame_ticks < ticks_per_frame - ticks_per_frame / 5) {
            Uint64 i = SDL_GetPerformanceCounter();
            SDL_Delay(1);
            Uint64 f = SDL_GetPerformanceCounter();
            last_frame_ticks += f - i;
        }
        // Eagerly await next frame
        while (last_frame_ticks < ticks_per_frame) {
            Uint64 i = SDL_GetPerformanceCounter();
            Uint64 f = SDL_GetPerformanceCounter();
            last_frame_ticks += f - i;
        }
#if _DEBUG
        {
            Uint64 i = SDL_GetPerformanceCounter();
            if (last_frame_ticks > 2 * ticks_per_frame) {
                printf("Frame %i took longer than 16ms\n", game.frame_count);
            }
            Uint64 f = SDL_GetPerformanceCounter();
            last_frame_ticks += f - i;
        }
#endif
    }

    game.current_scene->cleanup(game.current_scene_data, &game);
    SDL_PauseAudio(true);
    SDL_DestroyWindow(game.window);

    SDL_Quit();
    return 0;
}
