#ifndef CHARACTER_H
#define CHARACTER_H

#include "types.h"
#include "sound.h"
#include "assets.h"

struct Game;
struct Controls;

#define CHARACTER_LAYERS 3
#define GUY_JUMP_SOUND_CHANNEL 0

typedef struct Character {
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
    bool just_jumped;
    bool left_hit, right_hit;
    float jump_acceleration;
    int width, height;
    int center_x, center_y;
    // Animation shit
    enum { GUY_IDLE, GUY_WALKING, GUY_JUMPING, GUY_ANIMATION_COUNT } animation_state;
    int animation_frame;
    SDL_RendererFlip flip;

    SDL_Color body_color;
    SDL_Color left_foot_color;
    SDL_Color right_foot_color;
} Character;

static const int DEFAULT_ASSETS_FOR_ANIMATIONS[GUY_ANIMATION_COUNT] = {
    ASSET_CRATTLECRUTE_CRATTLECRUTE_PNG,
    ASSET_CRATTLECRUTE_CRATTLECRUTE_WALK_PNG,
    ASSET_CRATTLECRUTE_CRATTLECRUTE_JUMP_PNG
};

typedef struct AnimationAtlas {
    int width, height;
    SDL_Texture* texture;
    SDL_Rect* frames;//[number_of_frames_in_the_texture]
} AnimationAtlas;

typedef struct CharacterView {
    AnimationAtlas animation_textures[GUY_ANIMATION_COUNT];
    // Rather than mallocing when loading animation frames, we can just use this space.
    SDL_Rect rects_for_frames[128];
    AudioWave* jump_sound;
} CharacterView;

// === Routine character functions === //
void default_character(Character* target);
void default_character_animations(struct Game* game, CharacterView* view);
void apply_character_physics(struct Game* game, Character* guy, struct Controls* controls, float gravity, float drag);
void update_character_animation(Character* guy);
void character_post_update(Character* guy);

// === Rendering character functions === //
void draw_character(struct Game* game, struct Character* guy, struct CharacterView* guy_view);

#endif // CHARACTER_H
