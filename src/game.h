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
    C_RUN, C_JUMP,
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
#ifdef _DEBUG
struct mrbc_context;
#endif
typedef struct Game {
    SDL_Window* window;
    SDL_Renderer* renderer;
    mrb_state* mrb;
    struct {
#ifdef _DEBUG
        SDL_mutex* io_locked;
        SDL_atomic_t io_ready;
        int io_pos;
        char io_buffer[512];
        int io_locals;
        struct mrbc_context* io_cxt;
#endif
        mrb_sym sym_atcontrols;
        mrb_sym sym_atworld;
        mrb_sym sym_atgame;
        mrb_sym sym_atmap;
        mrb_sym sym_atlocal_character;
        mrb_sym sym_update;
        mrb_value game;
        struct RClass* color_class;
        struct RClass* controls_class;
        struct RClass* game_class;
        struct RClass* world_class;
        struct RClass* map_class;
        struct RClass* mob_class;
        struct RClass* character_class;
        mrb_value cc_type_to_sym;
        mrb_value cc_sym_to_type;
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
    // DON'T FUCK WITH THIS. I REALLY HOPE AN OVERRUN DOESN'T TRIP THIS.
    bool running;
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
