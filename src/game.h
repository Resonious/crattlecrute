#ifndef GAME_H
#define GAME_H
#include "SDL.h"
#include "types.h"
#include "assets.h"
#include "sound.h"

enum Control {
    C_UP, C_DOWN, C_LEFT, C_RIGHT,
    NUM_CONTROLS
};

typedef struct {
    bool last_frame[NUM_CONTROLS];
    bool this_frame[NUM_CONTROLS];
} Controls;

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    AudioQueue audio;
    Controls controls;
    float window_width, window_height;
} Game;

static bool just_pressed(Controls* controls, enum Control key) {
    return controls->this_frame[key] && !controls->last_frame[key];
}

#endif