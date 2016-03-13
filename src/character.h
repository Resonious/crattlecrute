#ifndef CHARACTER_H
#define CHARACTER_H

#include "types.h"
#include "sound.h"
#include "assets.h"
#include "cache.h"

struct Game;
struct Controls;

#define CHARACTER_LAYERS 4
#define GUY_JUMP_SOUND_CHANNEL 0

#define ANIMATION_MAX_FRAMES 128
#define ANIMATION_MAX_PERIPHERALS 128

enum CharacterAnimation {
    GUY_IDLE, GUY_WALKING, GUY_JUMPING,
    GUY_ANIMATION_COUNT
};

typedef struct CharacterView {
    AnimationAtlas* body_animation_textures[GUY_ANIMATION_COUNT];
    AnimationAtlas* feet_animation_textures[GUY_ANIMATION_COUNT];

    AudioWave* jump_sound;
} CharacterView;

// NOTE the beginning of this struct should match up with GenericBody
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

    bool left_hit, right_hit, grounded, hit_ceiling, hit_wall;
    // In degrees
    float ground_angle;
    // === END OF GenericBody ===

    // (x[0], x[1])  (x[2], x[3])
    vec4i middle_sensors;

    int animation_counter;
    float dy;
    float ground_speed;
    float ground_speed_max;
    float ground_acceleration;
    float ground_deceleration;
    float slide_speed;
    bool jumped;
    bool just_jumped;
    float jump_acceleration;
    int width, height;
    int center_x, center_y;
    // Animation shit
    enum CharacterAnimation animation_state;
    int animation_frame;
    SDL_RendererFlip flip;
    // This is NULL for a rendererless environment
    CharacterView* view;

    // For if we want to find the remoteplayer of a character
    int player_id;
    // Totally useful...
    bool just_went_through_door;

    int eye_type;
    int body_type;
    int feet_type;
    SDL_Color eye_color;
    SDL_Color body_color;
    SDL_Color left_foot_color;
    SDL_Color right_foot_color;
} Character;

// So, all eyes must be the same dimensions
#define EYE_SPRITE_WIDTH 3
#define EYE_SPRITE_HEIGHT 6
enum EyeFrame {
    EYE_OPEN, EYE_CLOSED,
    EYE_FRAME_COUNT
};

enum CrattlecruteType {
    CRATTLECRUTE_YOUNG,
    CRATTLECRUTE_STANDARD,
    CRATTLECRUTE_TYPE_COUNT
};

static const int ASSETS_FOR_ANIMATIONS[CRATTLECRUTE_TYPE_COUNT][GUY_ANIMATION_COUNT] = {
    { // CRATTLECRUTE_YOUNG (0)
        ASSET_CRATTLECRUTE_YOUNG_IDLE_PNG,
        ASSET_CRATTLECRUTE_YOUNG_WALK_PNG,
        ASSET_CRATTLECRUTE_YOUNG_JUMP_PNG
    },
    { // CRATTLECRUTE_STANDARD (1)
        ASSET_CRATTLECRUTE_CRATTLECRUTE_PNG,
        ASSET_CRATTLECRUTE_CRATTLECRUTE_WALK_PNG,
        ASSET_CRATTLECRUTE_CRATTLECRUTE_JUMP_PNG
    }
};

static const int EYE_TYPE_ASSETS[] = {
    ASSET_EYES_EYE_BLINK_PNG
};

// === Routine character functions === //
void default_character(Character* target);
void default_character_animations(struct Game* game, Character* guy);
void apply_character_physics(struct Game* game, Character* guy, struct Controls* controls, float gravity, float drag);
void update_character_animation(Character* guy);

// data will be passed to any of the provided callbacks when called.
void interact_character_with_world(
    struct Game* game,
    struct Character* guy,
    struct Controls* controls,
    struct Map* map,
    void* data,
    void(*go_through_door)(void*, struct Game*, struct Character*, struct Door*)
);

void character_post_update(Character* guy);

// === Rendering character functions === //
void draw_character(struct Game* game, struct Character* guy, struct CharacterView* guy_view);

#endif // CHARACTER_H
