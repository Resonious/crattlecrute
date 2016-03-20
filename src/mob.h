#ifndef MOB_H
#define MOB_H

#include "types.h"
#include "coords.h"

struct Map;
struct Game;
struct Character;
struct Controls;

enum MobSizeClass { SMALL, MEDIUM, LARGE };

typedef struct MobType {
    int id;
    enum MobSizeClass size_class;

    void(*initialize)(void* mob, struct Game* game, struct Map* map, vec2 pos);
    void(*update)(void* mob, struct Game* game, struct Map* map);
    void(*interact)(void* mob, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
    void(*render)(void* mob, struct Game* game, struct Map* map);
    void(*save)(void* mob, struct Map* map, byte* buffer, int* pos);
    void(*load)(void* mob, struct Map* map, byte* buffer, int* pos);
    bool(*sync_send)(void* mob, struct Map* map, byte* buffer, int* pos);
    void(*sync_receive)(void* mob, struct Map* map, byte* buffer, int* pos);
} MobType;

#define MOB_FIELDS\
    int index, mob_type_id

#define PHYSICS_MOB_FIELDS\
    MOB_FIELDS; GenericBody body

typedef struct MobCommon { MOB_FIELDS; } MobCommon;

typedef struct PhysicsMob { PHYSICS_MOB_FIELDS; } PhysicsMob;

typedef struct SmallMob {
    MOB_FIELDS;
    byte data[64 - sizeof(MobCommon)];
} SmallMob;

typedef struct MediumMob {
    MOB_FIELDS;
    byte data[256 - sizeof(MobCommon)];
} MediumMob;

typedef struct LargeMob {
    MOB_FIELDS;
    byte data[1024 - sizeof(MobCommon)];
} LargeMob;

// Utility that mobs often need:
void write_body_to_buffer(byte* buffer, GenericBody* body, int* pos);
void read_body_from_buffer(byte* buffer, GenericBody* body, int* pos);
void pick_up_item(MobCommon* mob, int item_type, struct Game* game, struct Map* map, struct Character* guy, struct Controls* controls);

// ============== ACTUAL MOBS ===============
// ==========
// NOTE: ALL Mob structs MUST lead MOB_FIELDS.
// ==========

// === Mobs === (THIS ENUM IS READ BY A RUBY SCRIPT AT COMPILE TIME)
enum MobId {
    MOB_NONE = -1,
    MOB_PON,
    MOB_FRUIT,
    MOB_EGG,
    NUMBER_OF_MOB_TYPES
};

typedef struct MobPon {
    MOB_FIELDS;

    GenericBody body;
    // Pixels per frame
    vec4 velocity;
    SDL_RendererFlip flip;
    bool hop;
    int frame;
    int frame_counter;
    int frame_inc;
    SDL_Color color;
} MobPon;
void mob_pon_initialize(void* vpon, struct Game* game, struct Map* map, vec2 pos);
void mob_pon_update(void* vpon, struct Game* game, struct Map* map);
void mob_pon_render(void* vpon, struct Game* game, struct Map* map);
void mob_pon_save(void* vpon, struct Map* map, byte* buffer, int* pos);
void mob_pon_load(void* vpon, struct Map* map, byte* buffer, int* pos);
bool mob_pon_sync_send(void* vpon, struct Map* map, byte* buffer, int* pos);
void mob_pon_sync_receive(void* vpon, struct Map* map, byte* buffer, int* pos);

typedef struct MobFruit {
    PHYSICS_MOB_FIELDS;
    float dy;
} MobFruit;
void mob_fruit_initialize(void* vfruit, struct Game* game, struct Map* map, vec2 pos);
void mob_fruit_update(void* vfruit, struct Game* game, struct Map* map);
void mob_fruit_interact(void* vfruit, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
void mob_fruit_render(void* vfruit, struct Game* game, struct Map* map);
void mob_fruit_save(void* vfruit, struct Map* map, byte* buffer, int* pos);
void mob_fruit_load(void* vfruit, struct Map* map, byte* buffer, int* pos);
bool mob_fruit_sync_send(void* vfruit, struct Map* map, byte* buffer, int* pos);
void mob_fruit_sync_receive(void* vfruit, struct Map* map, byte* buffer, int* pos);

typedef struct MobEgg {
    PHYSICS_MOB_FIELDS;
    float dy;
} MobEgg;
void mob_egg_initialize(void* vegg, struct Game* game, struct Map* map, vec2 pos);
void mob_egg_update(void* vegg, struct Game* game, struct Map* map);
void mob_egg_interact(void* vegg, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
void mob_egg_render(void* vegg, struct Game* game, struct Map* map);
void mob_egg_save(void* vegg, struct Map* map, byte* buffer, int* pos);
void mob_egg_load(void* vegg, struct Map* map, byte* buffer, int* pos);
bool mob_egg_sync_send(void* vegg, struct Map* map, byte* buffer, int* pos);
void mob_egg_sync_receive(void* vegg, struct Map* map, byte* buffer, int* pos);

static MobType mob_registry[] = {
    {
        MOB_PON,
        MEDIUM,
        mob_pon_initialize,
        mob_pon_update,
        NULL,
        mob_pon_render,
        mob_pon_save,
        mob_pon_load,
        mob_pon_sync_send,
        mob_pon_sync_receive,
    },
    {
        MOB_FRUIT,
        MEDIUM,
        mob_fruit_initialize,
        mob_fruit_update,
        mob_fruit_interact,
        mob_fruit_render,
        mob_fruit_save,
        mob_fruit_load,
        NULL, // mob_fruit_sync_send,
        NULL, // mob_fruit_sync_receive,
    },
    {
        MOB_EGG,
        MEDIUM,
        mob_egg_initialize,
        mob_egg_update,
        mob_egg_interact,
        mob_egg_render,
        mob_egg_save,
        mob_egg_load,
        NULL, // mob_egg_sync_send,
        NULL, // mob_egg_sync_receive,
    }
};

#endif
