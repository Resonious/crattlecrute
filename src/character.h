#ifndef CHARACTER_H
#define CHARACTER_H

#include "types.h"
#include "sound.h"

struct Game;
struct Controls;

#define GUY_JUMP_SOUND_CHANNEL 0

typedef struct {
    // (x[0] left to right, x[1] down to up)
    vec4 position;
    // (x[0] left to right, x[1] down to up)
    vec4 old_position;

    // (x[0], x[1])  (x[2], x[3])
    vec4i top_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i bottom_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i left_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i right_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i middle_sensors;

    int animation_counter;
    float dy;
    float ground_speed;
    float ground_speed_max;
    float ground_acceleration;
    float ground_deceleration;
    float slide_speed;
    // In degrees
    float ground_angle;
    bool grounded;
    bool jumped;
    bool left_hit, right_hit;
    float jump_acceleration;
    SDL_Texture* textures[3];
    int width, height;
    int center_x, center_y;
    // Animation shit
    enum { GUY_IDLE, GUY_WALKING } animation_state;
    int animation_frame;
    SDL_RendererFlip flip;
    AudioWave* jump_sound;
} Character;

// === Routine character functions === //
void default_character(Character* target);
void apply_character_physics(struct Game* game, Character* guy, struct Controls* controls, float gravity, float drag);
void update_character_animation(Character* guy);
void character_post_update(Character* guy);

// === Rendering character functions === //
void draw_character(struct Game* game, Character* guy);

#endif // CHARACTER_H