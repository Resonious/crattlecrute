#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_truetype.h"
#include "game.h"
#include "scene.h"

// Disgusting global window variable so that I can shit out message boxes
// from wherever I want.
SDL_Window* main_window;
Uint64 ticks_per_second;

void switch_scene(Game* game, int to_scene) {
    assert(to_scene >= 0);
    assert(to_scene < sizeof(SCENES) / sizeof(Scene));

    game->current_scene->cleanup(game->current_scene_data, game);
    memset(game->current_scene_data, 0, SCENE_DATA_SIZE);

    game->current_scene = &SCENES[to_scene];
    game->current_scene->initialize(game->current_scene_data, game);
    // TODO do an update here? (this is the only case where a render can happen WITHOUT an update preceding..)
}

int main(int argc, char** argv) {
    ticks_per_second = SDL_GetPerformanceFrequency();

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

    int key_count;
    const Uint8* keys = SDL_GetKeyboardState(&key_count);

    // Main loop bitch
    SDL_Event event;
    bool running = true;
    Uint64 milliseconds_per_tick = 1000 / ticks_per_second;
    Uint64 last_frame_ms = 17;
    // test stuff

    game.current_scene->initialize(game.current_scene_data, &game);

    while (running) {
        Uint64 frame_start = SDL_GetPerformanceCounter();
        game.frame_count += 1;

        memcpy(game.controls.last_frame, game.controls.this_frame, sizeof(game.controls.last_frame));
        memset(game.controls.this_frame, 0, sizeof(game.controls.this_frame));

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

            game.controls.this_frame[C_F1] = keys[SDL_SCANCODE_F1];
        }

        game.current_scene->update(game.current_scene_data, &game);

        // Draw!!! Finally!!!
        SDL_RenderClear(game.renderer);
        game.current_scene->render(game.current_scene_data, &game);
        SDL_RenderPresent(game.renderer);

        // ======================= Cap Framerate =====================
        Uint64 frame_end = SDL_GetPerformanceCounter();
        Uint64 frame_ms = (frame_end - frame_start) * milliseconds_per_tick;

        if (frame_ms < 17) {
            SDL_Delay(17 - frame_ms);
        }
    }

    game.current_scene->cleanup(game.current_scene_data, &game);
    SDL_PauseAudio(true);
    SDL_DestroyWindow(game.window);

    SDL_Quit();
    return 0;
}
