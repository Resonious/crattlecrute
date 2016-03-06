#ifndef GAME_H
#define GAME_H
#ifdef __FreeBSD__
#include "SDL2/SDL.h"
#else
#include "SDL.h"
#endif
#include "types.h"
#include "sound.h"
#include "cache.h"
#include <mruby.h>

enum Control {
    C_UP, C_DOWN, C_LEFT, C_RIGHT,
    C_JUMP,
    C_W, C_S, C_A, C_D,
    C_PAUSE,
    C_F1, C_F2, C_SPACE,
    C_DEBUG_ADV,
    NUM_CONTROLS
};

typedef struct Controls {
    bool last_frame[NUM_CONTROLS];
    bool this_frame[NUM_CONTROLS];
} Controls;

static bool just_pressed(Controls* controls, enum Control key) {
    return controls->this_frame[key] && !controls->last_frame[key];
}
static bool just_released(Controls* controls, enum Control key) {
    return controls->last_frame[key] && !controls->this_frame[key];
}
void controls_pre_update(Controls* controls);

// Number of bytes long scene datas are expected to be.
#define SCENE_DATA_SIZE (1024 * 50)

struct Scene;
typedef struct Game {
    SDL_Window* window;
    SDL_Renderer* renderer;
    mrb_state* mrb;
    struct {
        mrb_sym sym_atcontrols;
        struct RClass* controls_class;
        struct RClass* game_class;
    } ruby;
    struct {
        int text_buf_size;
        char* text;
        char* composition;
        int cursor;
        int selection_length;
        bool enter_pressed;
        bool canceled;
    } text_edit;
    int argc;
    char** argv;
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
    bool net_joining;
    bool follow_cam_y;
    vec4 camera_target;
    vec4 camera;
    double frames_per_second;
} Game;

// Switch to a new scene (COMING SOON: fade to scene!?)
void switch_scene(Game* game, int to_scene);
// input_rect can be null who gives a shit
void start_editing_text(Game* game, char* text_to_edit, int buffer_size, SDL_Rect* input_rect);
void stop_editing_text(Game* game);

// === Areas === (THIS ENUM IS READ BY A RUBY SCRIPT AT COMPILE TIME)
enum AreaId {
    AREA_TESTZONE_ONE,
    AREA_TESTZONE_TWO,
    NUMBER_OF_AREAS
};
int map_asset_for_area(int area_id);

// === Text functions ===

#define set_text_color(game, r, g, b) SDL_SetTextureColorMod((game)->font, (r), (g), (b));
void draw_text_ex_caret(Game* game, int x, int y, char* text, int padding, float scale, int caret);
void draw_text_ex(Game* game, int x, int y, char* text, int padding, float scale);
void draw_text_caret(Game* game, int x, int y, char* text, int caret);
void draw_text(Game* game, int x, int y, char* text);
void input_text(Game* game, char* text);
void handle_key_during_text_edit(Game* game, SDL_Event* event);
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
