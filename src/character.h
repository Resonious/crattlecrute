#ifndef CHARACTER_H
#define CHARACTER_H

#include "types.h"
#include "sound.h"
#include "assets.h"
#include "cache.h"
#include "script.h"
#include "item.h"

struct Game;
struct Controls;

#define CHARACTER_LAYERS 4
#define GUY_JUMP_SOUND_CHANNEL 0

#define ANIMATION_MAX_FRAMES 128
#define ANIMATION_MAX_PERIPHERALS 128

// Physics baselines - character stats are factors of these.
#define CHARA_GROUND_SPEED_MAX     (4.0f)
#define CHARA_RUN_SPEED_MAX        (2.0f)
#define CHARA_GROUND_ACCELERATION  (0.8f)
#define CHARA_GROUND_DECELERATION  (0.5f)
#define CHARA_JUMP_ACCELERATION    (20.0f)
#define CHARA_JUMP_CANCEL_DY       (10.0f)

enum CharacterAnimation {
    GUY_IDLE, GUY_WALKING, GUY_JUMPING,
    GUY_RUNNING,
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
    // This is additional speed, ontop of ground speed, from running
    float run_speed;

    // === attributes ===
    float ground_speed_max;
    float run_speed_max;
    float ground_acceleration;
    float ground_deceleration;
    float jump_acceleration;
    float jump_cancel_dy;

    float slide_speed;
    bool jumped;
    bool just_jumped;
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

    mrb_value reye_color;
    mrb_value rbody_color;
    mrb_value rleft_foot_color;
    mrb_value rright_foot_color;

    // For synchronizing attributes over net or whatever.
    SDL_atomic_t dirty;

    int selected_slot;
    int grabbed_slot;
    Inventory inventory;
} Character;

#define CHARACTER_SPRITE_WIDTH 90
#define CHARACTER_SPRITE_HEIGHT 90
#define CHARACTER_EYE_LAYER 4 

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
        ASSET_CRATTLECRUTE_YOUNG_JUMP_PNG,
        ASSET_CRATTLECRUTE_YOUNG_WALK_PNG
    },
    { // CRATTLECRUTE_STANDARD (1)
        ASSET_CRATTLECRUTE_CRATTLECRUTE_PNG,
        ASSET_CRATTLECRUTE_CRATTLECRUTE_WALK_PNG,
        ASSET_CRATTLECRUTE_CRATTLECRUTE_JUMP_PNG,
        ASSET_CRATTLECRUTE_CRATTLECRUTE_RUN_PNG,
    }
};

static const int EYE_TYPE_ASSETS[] = {
    ASSET_EYES_EYE_BLINK_PNG
};

enum InventoryAction {
    INV_NONE,
    INV_ACTION,
    INV_TOGGLE
};

// === Routine character functions === //
void default_character(struct Game* game, Character* target);
void default_character_animations(struct Game* game, Character* guy);
// This should be called after changing the body or feet type.
void load_character_atlases(struct Game* game, Character* guy);
enum InventoryAction apply_character_inventory(Character* guy, struct Controls* controls, struct Game* game, struct Map* map);
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
