#ifndef GAME_H
#define GAME_H
#include "SDL.h"
#include "types.h"
#include "assets.h"
#include "sound.h"

enum Control {
    C_UP, C_DOWN, C_LEFT, C_RIGHT,
    C_W, C_S, C_A, C_D,
    C_F1, C_SPACE,
    NUM_CONTROLS
};

typedef struct {
    bool last_frame[NUM_CONTROLS];
    bool this_frame[NUM_CONTROLS];
} Controls;

static bool just_pressed(Controls* controls, enum Control key) {
    return controls->this_frame[key] && !controls->last_frame[key];
}

// Number of bytes long scene datas are expected to be.
#define SCENE_DATA_SIZE 4096

struct Scene;
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    AudioQueue audio;
    Controls controls;
    float window_width, window_height;
    Uint64 frame_count;
    // Remember to #include "scene.h" if you're gonna use this.
    struct Scene* current_scene;
    void* current_scene_data;
} Game;

void switch_scene(Game* game, int to_scene);

typedef struct {
    // (x[0] left to right, x[1] down to up)
    vec4 position;

    // (x[0], x[1])  (x[2], x[3])
    vec4i bottom_sensors;

    float ground_speed;
    float ground_speed_max;
    float ground_acceleration;
    float ground_deceleration;
    SDL_Texture* textures[3];
} Character;

#endif