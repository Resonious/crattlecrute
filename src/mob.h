#ifndef MOB_H
#define MOB_H

#include "types.h"

struct Map;
struct Controls;

enum MobSizeClass { SMALL, MEDIUM, LARGE };

typedef struct MobType {
    int id;
    enum MobSizeClass size_class;

    void(*initialize)(void* mob, struct Game* game, struct Map* map, vec2 pos);
    void(*update)(void* mob, struct Game* game, struct Map* map);
    void(*render)(void* mob, struct Game* game, struct Map* map);
    void(*save)(void* mob, struct Map* map, byte* buffer, int* pos);
    void(*load)(void* mob, struct Map* map, byte* buffer, int* pos);
} MobType;

#define MOB_FIELDS\
    int index, mob_type_id;\
    struct Controls* controls;

typedef struct MobCommon { MOB_FIELDS } MobCommon;

typedef struct SmallMob {
    MOB_FIELDS
    byte data[64];
} SmallMob;

typedef struct MediumMob {
    MOB_FIELDS
    byte data[256];
} MediumMob;

typedef struct LargeMob {
    MOB_FIELDS
    byte data[1024];
} LargeMob;

// ============== ACTUAL MOBS ===============
// ==========
// NOTE: ALL Mob structs MUST lead MOB_FIELDS.
// ==========

// === Mobs === (THIS ENUM IS READ BY A RUBY SCRIPT AT COMPILE TIME)
enum MobId {
    MOB_PON,
    NUMBER_OF_MOB_TYPES
};

typedef struct MobPon {
    MOB_FIELDS

    vec2 pos;
    int frame;
    int frame_counter;
    int frame_inc;
    SDL_Color color;
} MobPon;
void mob_pon_initialize(void* pon, struct Game* game, struct Map* map, vec2 pos);
void mob_pon_update(void* pon, struct Game* game, struct Map* map);
void mob_pon_render(void* pon, struct Game* game, struct Map* map);
void mob_pon_save(void* pon, struct Map* map, byte* buffer, int* pos);
void mob_pon_load(void* pon, struct Map* map, byte* buffer, int* pos);

static MobType mob_registry[] = {
    {
        MOB_PON,
        SMALL,
        mob_pon_initialize,
        mob_pon_update,
        mob_pon_render,
        mob_pon_save,
        mob_pon_load
    }
};

#endif