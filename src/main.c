#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "game.h"
#include "scene.h"

// Disgusting global window variable so that I can shit out message boxes
// from wherever I want.
SDL_Window* main_window;
Uint64 ticks_per_second;

#if _DEBUG
    bool debug_pause = false;
#endif

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

void default_character(Character* target) {
    target->width  = 90;
    target->height = 90;
    target->center_x = 45;
    target->center_y = 45;
    target->ground_speed = 0.0f;
    target->ground_speed_max = 6.0f;
    target->ground_acceleration = 1.15f;
    target->ground_deceleration = 1.1f;
    target->ground_angle = 0.0f;
    target->position.simd = _mm_set1_ps(0.0f);
    target->grounded = false;

    target->top_sensors.x[S1X] = 31 - 45;
    target->top_sensors.x[S1Y] = 72 - 45;
    target->top_sensors.x[S2X] = 58 - 45;
    target->top_sensors.x[S2Y] = 72 - 45;

    target->bottom_sensors.x[S1X] = 31 - 45;
    target->bottom_sensors.x[S1Y] = 16 - 45;
    target->bottom_sensors.x[S2X] = 58 - 45;
    target->bottom_sensors.x[S2Y] = 16 - 45;

    target->left_sensors.x[S1X] = 30 - 45;
    target->left_sensors.x[S1Y] = 71 - 45;
    target->left_sensors.x[S2X] = 31 - 45;
    target->left_sensors.x[S2Y] = 30 - 45;

    target->right_sensors.x[S1X] = 59 - 45;
    target->right_sensors.x[S1Y] = 71 - 45;
    target->right_sensors.x[S2X] = 59 - 45;
    target->right_sensors.x[S2Y] = 33 - 45;

    /*
    data->animation_frame = 0;
    data->flip = SDL_FLIP_NONE;
    data->flip = false;
    data->dy = 0; // pixels per second
    data->jump_acceleration = 20.0f;
    */
}

int main(int argc, char** argv) {
    ticks_per_second = SDL_GetPerformanceFrequency();
    _MM_SET_ROUNDING_MODE(_MM_ROUND_DOWN);

    Game game;
    memset(&game, 0, sizeof(Game));
    game.window_width = 640.0f;
    game.window_height = 480.0f;

#ifdef _DEBUG
    // All scene ids should equal their index
    for (int i = 0; i < sizeof(SCENES) / sizeof(Scene); i++)
        assert(SCENES[i].id == i);
#endif

    game.current_scene = &SCENES[SCENE_TEST];
    game.current_scene_data = malloc(SCENE_DATA_SIZE);

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

    game.current_scene->initialize(game.current_scene_data, &game);

    while (running) {
        frame_start = SDL_GetPerformanceCounter();
        game.frame_count += 1;
        game.tick_count += last_frame_ticks;

        memcpy(game.controls.last_frame, game.controls.this_frame, sizeof(game.controls.last_frame));
        // NOTE all controls should be re-set every frame, so this technically shouldn't be necessary.
        // memset(game.controls.this_frame, 0, sizeof(game.controls.this_frame));

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            }
        }

        {
            game.controls.this_frame[C_UP]    = keys[SDL_SCANCODE_UP];
            game.controls.this_frame[C_DOWN]  = keys[SDL_SCANCODE_DOWN];
            game.controls.this_frame[C_LEFT]  = keys[SDL_SCANCODE_LEFT];
            game.controls.this_frame[C_RIGHT] = keys[SDL_SCANCODE_RIGHT];

            game.controls.this_frame[C_W] = keys[SDL_SCANCODE_W];
            game.controls.this_frame[C_S] = keys[SDL_SCANCODE_S];
            game.controls.this_frame[C_A] = keys[SDL_SCANCODE_A];
            game.controls.this_frame[C_D] = keys[SDL_SCANCODE_D];

            game.controls.this_frame[C_PAUSE] = keys[SDL_SCANCODE_P];

            game.controls.this_frame[C_SPACE] = keys[SDL_SCANCODE_SPACE];
            game.controls.this_frame[C_F1] = keys[SDL_SCANCODE_F1];
        }

#if _DEBUG
        if (just_pressed(&game.controls, C_PAUSE))
            debug_pause = !debug_pause;
        if (!debug_pause || just_pressed(&game.controls, C_SPACE))
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

        set_text_color(&game, 255, 255, 20);
        float fps = (float)game.frame_count / ((float)game.tick_count / (float)ticks_per_second);
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
        if (last_frame_ticks > 2 * ticks_per_frame) {
            printf("Bro.. You lagging?");
        }
#endif
    }

    game.current_scene->cleanup(game.current_scene_data, &game);
    SDL_PauseAudio(true);
    SDL_DestroyWindow(game.window);

    SDL_Quit();
    return 0;
}
