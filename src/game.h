#ifndef GAME_H
#define GAME_H
#include "SDL.h"
#include "types.h"
#include "sound.h"
#include "cache.h"

enum Control {
    C_UP, C_DOWN, C_LEFT, C_RIGHT,
    C_W, C_S, C_A, C_D,
    C_PAUSE,
    C_F1, C_SPACE,
    C_DEBUG_ADV,
    NUM_CONTROLS
};

typedef struct {
    bool last_frame[NUM_CONTROLS];
    bool this_frame[NUM_CONTROLS];
} Controls;

static bool just_pressed(Controls* controls, enum Control key) {
    return controls->this_frame[key] && !controls->last_frame[key];
}
static bool just_released(Controls* controls, enum Control key) {
    return controls->last_frame[key] && !controls->this_frame[key];
}

// Number of bytes long scene datas are expected to be.
#define SCENE_DATA_SIZE (1024 * 5)

struct Scene;
typedef struct Game {
    SDL_Window* window;
    SDL_Renderer* renderer;
    AssetCache asset_cache;
    AudioQueue audio;
    Controls controls;
    float window_width, window_height;
    Uint64 frame_count;
    Uint64 tick_count;
    SDL_Texture* font;
    // Remember to #include "scene.h" if you're gonna use this.
    struct Scene* current_scene;
    void* current_scene_data;
    bool follow_cam_y;
    vec4 camera_target;
    vec4 camera;
} Game;

// Switch to a new scene (COMING SOON: fade to scene!?)
void switch_scene(Game* game, int to_scene);

// === Text functions ===

#define set_text_color(game, r, g, b) SDL_SetTextureColorMod((game)->font, (r), (g), (b));
void draw_text_ex(Game* game, int x, int y, char* text, int padding, float scale);
void draw_text(Game* game, int x, int y, char* text);
#define draw_text_ex_f(game, x, y, padding, scale, fmttext, ...)\
    {\
        char _strbuf[sizeof(fmttext) * 2];\
        SDL_snprintf(_strbuf, sizeof(_strbuf), fmttext, __VA_ARGS__);\
        draw_text_ex(game, x, y, _strbuf, padding, scale);\
    }
#define draw_text_f(game, x, y, fmttext, ...)\
    {\
        char _strbuf[sizeof(fmttext) * 2];\
        SDL_snprintf(_strbuf, sizeof(_strbuf), fmttext, __VA_ARGS__);\
        draw_text(game, x, y, _strbuf);\
    }
// For those REALLY dire situations...
#define draw_text_ex_f_len(game, x, y, padding, scale, buflen, fmttext, ...)\
    {\
        char _strbuf[buflen];\
        SDL_snprintf(_strbuf, buflen, fmttext, __VA_ARGS__);\
        draw_text_ex(game, x, y, _strbuf, padding, scale);\
    }

#endif // GAME_H