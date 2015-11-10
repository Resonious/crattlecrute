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

int main(int argc, char** argv) {
    SDL_Window* window;
    SDL_Renderer* renderer;

    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));
    open_assets_file();
    initialize_sound();

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
    // if (body_texture == NULL) SDL_ShowSimpleMessageBox(0, "FUCK!", SDL_GetError(), window);

    // Main loop bitch
    SDL_Event event;
    bool running = true;
    Uint64 milliseconds_per_tick = 1000 / SDL_GetPerformanceFrequency();
    Uint64 frame_count = 0;
    int animation_frame = 0; // test
    while (running) {
        Uint64 frame_start = SDL_GetPerformanceCounter();
        frame_count += 1;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            }
        }

        // Draw!!! Finally!!!
        SDL_RenderClear(renderer);

        if (animation_frame >= 9)
            animation_frame = 1;
        SDL_Rect src = { animation_frame * 90, 0, 90, 90 };
        SDL_Rect dest = { 20, 20, 90, 90 };
        if (frame_count % 5 == 0)
            animation_frame += 1;
        for (int i = 0; i < 3; i++)
            SDL_RenderCopy(renderer, textures[i], &src, &dest);

        SDL_RenderPresent(renderer);

        // ======================= Cap Framerate =====================
        Uint64 frame_end = SDL_GetPerformanceCounter();
        Uint64 frame_ms = (frame_end - frame_start) * milliseconds_per_tick;

        if (frame_ms < 17)
            SDL_Delay(17 - frame_ms);
    }

    SDL_DestroyWindow(window);

    SDL_Quit();
    return 0;
}
