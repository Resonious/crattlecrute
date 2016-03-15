#ifdef _WIN32
#include "resource.h"
#include <WinSock2.h>
// Stupid...
WSADATA global_wsa;
#endif

#include <stdio.h>

#ifdef __APPLE__
#else
#ifndef __FreeBSD__
#include <malloc.h>
#endif
#endif

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "game.h"
#include "assets.h"
#include "scene.h"
#include "coords.h"
#include "script.h"
#ifdef __APPLE__
#include "SDL_main.h"
#endif
#include "SDL_syswm.h"

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
                case SDL_SCANCODE_Z: this_frame[C_JUMP] = to; break; \
                case SDL_SCANCODE_LSHIFT: this_frame[C_RUN] = to; break; \
\
                case SDL_SCANCODE_P:           this_frame[C_PAUSE]     = to; break; \
                case SDL_SCANCODE_SPACE:       this_frame[C_SPACE]     = to; break; \
                case SDL_SCANCODE_F1:          this_frame[C_F1]        = to; break; \
                case SDL_SCANCODE_F2:          this_frame[C_F2]        = to; break; \
                case SDL_SCANCODE_LEFTBRACKET: this_frame[C_DEBUG_ADV] = to; break;

#ifdef _DEBUG
int ruby_io_thread(void* vdata) {
    Game* game = (Game*)vdata;

    while (true) {
        char c = getchar();
        SDL_LockMutex(game->ruby.io_locked);
        game->ruby.io_buffer[game->ruby.io_pos++] = c;

        if (c == '\n') {
            SDL_AtomicSet(&game->ruby.io_ready, true);
        }
        SDL_UnlockMutex(game->ruby.io_locked);
    }
}
#endif

mrb_value rb_game(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    return game->ruby.game;
}

mrb_value rb_exit(mrb_state* mrb, mrb_value self) {
    Game* game = (Game*)mrb->ud;
    game->running = false;
    return mrb_str_new_cstr(mrb, "bye");
}

int main(int argc, char** argv) {
    BENCH_START(total_initialization);
    srand((unsigned int)time(0));
    ticks_per_second = SDL_GetPerformanceFrequency();
    _MM_SET_ROUNDING_MODE(_MM_ROUND_DOWN);

    Game game;
    memset(&game, 0, sizeof(Game));
    game.argc = argc;
    game.argv = argv;
    game.window_width = 640.0f;
    game.window_height = 480.0f;
    for (int i = 0; i < NUMBER_OF_ASSETS; i++) {
        game.asset_cache.assets[i].id = ASSET_NOT_LOADED;
    }

#ifdef _DEBUG
    // All scene ids should equal their index
    for (int i = 0; i < sizeof(SCENES) / sizeof(Scene); i++)
        SDL_assert(SCENES[i].id == i);
#endif

    game.current_scene = &SCENES[SCENE_WORLD];
#ifdef X86
    game.current_scene_data = _aligned_malloc(SCENE_DATA_SIZE, 16);
#else
    game.current_scene_data = malloc(SCENE_DATA_SIZE);
#endif
    game.camera.simd = _mm_set1_ps(0.0f);
    game.camera_target.simd = _mm_set1_ps(0.0f);
    game.follow_cam_y = false;

    BENCH_START(loading_sdl);
    SDL_Init(SDL_INIT_EVERYTHING & (~SDL_INIT_HAPTIC));
    open_assets_file();
    memset(&game.controls, 0, sizeof(game.controls));
    BENCH_END(loading_sdl);

    BENCH_START(loading_window);
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--headless") == 0) {
          goto no_renderer;
        }
    }
    initialize_sound(&game.audio);

    game.window = SDL_CreateWindow(
        "Crattlecrute",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        (int)game.window_width, (int)game.window_height,
        SDL_WINDOW_RESIZABLE
    );
    main_window = game.window;

    game.renderer = SDL_CreateRenderer(game.window, -1, 0);
no_renderer:
    if (game.renderer == NULL) printf("No renderer!\n");
    SDL_SetRenderDrawColor(game.renderer, 20, 20, 20, 255);
    BENCH_END(loading_window);

    game.font = load_texture(game.renderer, ASSET_FONT_FONT_PNG);

    // Gotta be a tad more aggressive about the icon on windows
    {
#ifdef _WIN32
        HINSTANCE hinst = GetModuleHandle(NULL);
        HICON icon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_ICON1));
        if (icon != NULL) {
            SDL_SysWMinfo wminfo;
            SDL_VERSION(&wminfo.version);
            int result = SDL_GetWindowWMInfo(game.window, &wminfo);
            if (result == 1) {
                HWND hwnd = wminfo.info.win.window;
                SetClassLongPtr(hwnd, -14, (LONG)icon);
                SetClassLongPtr(hwnd, -34, (LONG)icon);
            }
        }
#else
        SDL_Surface* icon = load_image(ASSET_ICON_PNG);
        SDL_SetWindowIcon(game.window, icon);
        free_image(icon);
#endif
    }

    int key_count;
    const Uint8* keys = SDL_GetKeyboardState(&key_count);

#ifdef _WIN32 // Windows sucks and needs networking initialized
    if (WSAStartup(MAKEWORD(2,2),&global_wsa) != 0)
        SDL_ShowSimpleMessageBox(0, "Uh oh", "Couldn't initialize Windows networking", game.window);
#endif

    // MRB ????
    BENCH_START(loading_ruby);
    game.mrb = mrb_open();
    if (game.mrb == NULL) {
        SDL_ShowSimpleMessageBox(0, "NO MRUBY?", "ARE YOU KIDDING ME", game.window);
        return 0;
    }
    script_init(&game);
    SDL_assert(!game.mrb->exc);

    mrb_value ruby_context = mrb_obj_value(game.mrb->top_self);
#ifdef _DEBUG
    game.ruby.io_locked = SDL_CreateMutex();
    if (!game.ruby.io_locked)
        printf("RUBY I/O IS MESSED UP.\n");
    else {
        memset(game.ruby.io_buffer, 0, sizeof(game.ruby.io_buffer));
        game.ruby.io_pos = 0;
        game.ruby.io_cxt = mrbc_context_new(game.mrb);
        game.ruby.io_cxt->capture_errors = true;
        mrbc_filename(game.mrb, game.ruby.io_cxt, "(crattlecrute)");
        SDL_AtomicSet(&game.ruby.io_ready, false);
        SDL_CreateThread(ruby_io_thread, "Ruby I/O", &game);
    }

    if (load_script_file(game.mrb))
        printf("script.rb loaded\n");
    else
        printf("script.rb not loaded\n");

    if (game.mrb->exc) {
        printf("ERROR in script.rb: ");
        ruby_p(game.mrb, mrb_obj_value(game.mrb->exc), 0);
        game.mrb->exc = NULL;
    }
#endif

    mrb_value rgame = mrb_class_new_instance(game.mrb, 0, NULL, game.ruby.game_class);
    DATA_PTR(rgame) = &game;
    game.ruby.game = rgame;

    mrb_define_singleton_method(game.mrb, game.mrb->top_self, "game", rb_game, MRB_ARGS_NONE());
    mrb_define_singleton_method(game.mrb, game.mrb->top_self, "exit", rb_exit, MRB_ARGS_NONE());
    mrb_define_singleton_method(game.mrb, game.mrb->top_self, "quit", rb_exit, MRB_ARGS_NONE());

    mrb_const_set(
        game.mrb,
        mrb_obj_value(game.ruby.game_class),
        mrb_intern_lit(game.mrb, "TICKS_PER_SECOND"),
        mrb_fixnum_value(ticks_per_second)
    );

    BENCH_END(loading_ruby);

    // Main loop bitch
    SDL_Event event;
    game.running = true;
    const Uint64 milliseconds_per_tick = 1000 / ticks_per_second;
    const Uint64 ticks_per_frame = ticks_per_second / 60;
    Uint64 frame_start, frame_end;
    Uint64 last_frame_ticks = 0;
    // For getting FPS
    double frame_count_this_second = 0;
    game.frames_per_second = 60.0;
    double tick_second_counter = 0;
    bool keys_up[NUM_CONTROLS];
    bool keys_down[NUM_CONTROLS];

    SDL_assert(!game.mrb->exc);
    game.current_scene->initialize(game.current_scene_data, &game);
    SDL_assert(!game.mrb->exc);

    BENCH_END(total_initialization);

    while (game.running) {
        frame_start = SDL_GetPerformanceCounter();
        game.frame_count += 1;
        frame_count_this_second += 1;
        game.tick_count += last_frame_ticks;
        tick_second_counter += last_frame_ticks;

        controls_pre_update(&game.controls);

        memset(keys_up, 0, sizeof(keys_up));
        memset(keys_down, 0, sizeof(keys_down));

        game.text_edit.enter_pressed = false;
        game.text_edit.canceled = false;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                game.running = false;
                break;

            case SDL_TEXTINPUT:
                input_text(&game, event.text.text);
                break;
            case SDL_TEXTEDITING:
                if (game.text_edit.text) {
                    game.text_edit.composition = event.edit.text;
                    game.text_edit.cursor = event.edit.start;
                    game.text_edit.selection_length = event.edit.length;
                }
                break;

            case SDL_KEYDOWN:
                if (game.text_edit.text)
                    handle_key_during_text_edit(&game, &event);
                else
                    switch (event.key.keysym.scancode) { SET_CONTROL(keys_down, true) }
                break;
            case SDL_KEYUP:
#ifdef _DEBUG
                if (event.key.keysym.sym == SDLK_F5 || (event.key.keysym.sym == SDLK_r && event.key.keysym.mod & KMOD_CTRL))
                    load_script_file(game.mrb);
#endif

                if (!game.text_edit.text)
                    switch (event.key.keysym.scancode) { SET_CONTROL(keys_up, true) }
                break;

            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                case SDL_WINDOWEVENT_RESIZED: case SDL_WINDOWEVENT_SIZE_CHANGED:
                    game.window_width = (float)event.window.data1;
                    game.window_height = (float)event.window.data2;
                    break;
                case SDL_WINDOWEVENT_MAXIMIZED:
                    break;
                }
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

        // Check for exception and print the details if necessary
        if (game.mrb->exc) {
            ruby_p(game.mrb, mrb_obj_value(game.mrb->exc), 0);
            game.mrb->exc = 0;
        }
        if (mrb_respond_to(game.mrb, ruby_context, game.ruby.sym_update))
            mrb_funcall(game.mrb, ruby_context, "update", 1, rgame);

#if _DEBUG
        // Execute ruby code from console i/o
        if (SDL_AtomicGet(&game.ruby.io_ready)) {
            SDL_LockMutex(game.ruby.io_locked);

            // mrb_p(game.mrb, mrb_load_nstring_cxt(game.mrb, game.ruby.io_buffer, game.ruby.io_pos, game.ruby.io_cxt));
            struct mrb_parser_state* parser = mrb_parser_new(game.mrb);
            parser->s = game.ruby.io_buffer;
            parser->send = game.ruby.io_buffer + game.ruby.io_pos;
            parser->lineno = ++game.ruby.io_cxt->lineno;
            mrb_parser_parse(parser, game.ruby.io_cxt);

            if (0 < parser->nerr) {
                printf("line %d: %s\n", parser->error_buffer[0].lineno, parser->error_buffer[0].message);
            }
            else {
                struct RProc *proc = mrb_generate_code(game.mrb, parser);
                if (proc == NULL)
                    printf("ruby parser fucked up.\n");
                else {
                    mrb_value result = mrb_toplevel_run_keep(game.mrb, proc, game.ruby.io_locals);
                    game.ruby.io_locals = proc->body.irep->nlocals;

                    if (game.mrb->exc) {
                        ruby_p(game.mrb, mrb_obj_value(game.mrb->exc), 0);
                        game.mrb->exc = 0;
                    }
                    else {
                        ruby_p(game.mrb, result, 0);
                    }
                }
            }
            mrb_parser_free(parser);

            game.ruby.io_pos = 0;
            SDL_AtomicSet(&game.ruby.io_ready, false);
            SDL_UnlockMutex(game.ruby.io_locked);
        }
        // End of ruby parser

        // Freeze-frame
        if (just_pressed(&game.controls, C_PAUSE))
            debug_pause = !debug_pause;
        if (!debug_pause || just_pressed(&game.controls, C_DEBUG_ADV))
#endif
        game.current_scene->update(game.current_scene_data, &game);

        // Draw!!! Finally!!!
        if (game.renderer) {
            SDL_RenderClear(game.renderer);
            game.current_scene->render(game.current_scene_data, &game);
#if _DEBUG
            if (debug_pause) {
                set_text_color(&game, 50, 50, 255);
                draw_text_ex(&game, 32, game.window_height - 32, "FREEZE-FRAME!", 1, 0.7f);
            }
#endif
            if (tick_second_counter / (double)ticks_per_second >= 1.0) {
                game.frames_per_second = frame_count_this_second / (tick_second_counter / (double)ticks_per_second);
                frame_count_this_second = 0;
                tick_second_counter = 0;
            }
#if DRAW_FPS
            set_text_color(&game, 255, 255, 20);
            draw_text_ex_f(&game, (int)game.window_width - 150, (int)game.window_height - 20, -1, 0.7f, "FPS: %.2f", game.frames_per_second);
#endif
            SDL_RenderPresent(game.renderer);
        }

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
                printf("Frame %l took longer than 16ms\n", game.frame_count);
            }
            Uint64 f = SDL_GetPerformanceCounter();
            last_frame_ticks += f - i;
        }
#endif
    }

    game.current_scene->cleanup(game.current_scene_data, &game);
    SDL_PauseAudio(true);
    SDL_DestroyWindow(game.window);

#ifdef _WIN32 // Windows sucks and needs networking cleaned up
    WSACleanup();
#endif

    SDL_Quit();
    return 0;
}
