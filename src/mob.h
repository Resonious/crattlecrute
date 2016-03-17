#ifndef MOB_H
#define MOB_H

#include "types.h"
#include "coords.h"

struct Map;
struct Game;

enum MobSizeClass { SMALL, MEDIUM, LARGE };

typedef struct MobType {
    int id;
    enum MobSizeClass size_class;

    void(*initialize)(void* mob, struct Game* game, struct Map* map, vec2 pos);
    void(*update)(void* mob, struct Game* game, struct Map* map);
    void(*render)(void* mob, struct Game* game, struct Map* map);
    void(*save)(void* mob, struct Map* map, byte* buffer, int* pos);
    void(*load)(void* mob, struct Map* map, byte* buffer, int* pos);
    bool(*sync_send)(void* mob, struct Map* map, byte* buffer, int* pos);
    void(*sync_receive)(void* mob, struct Map* map, byte* buffer, int* pos);
} MobType;

#define MOB_FIELDS\
    int index, mob_type_id

typedef struct MobCommon { MOB_FIELDS; } MobCommon;

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

// ============== ACTUAL MOBS ===============
// ==========
// NOTE: ALL Mob structs MUST lead MOB_FIELDS.
// ==========

// === Mobs === (THIS ENUM IS READ BY A RUBY SCRIPT AT COMPILE TIME)
enum MobId {
    MOB_PON,
    MOB_FRUIT,
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
    MOB_FIELDS;

    GenericBody body;
    float dy;
} MobFruit;
void mob_fruit_initialize(void* vfruit, struct Game* game, struct Map* map, vec2 pos);
void mob_fruit_update(void* vfruit, struct Game* game, struct Map* map);
void mob_fruit_render(void* vfruit, struct Game* game, struct Map* map);
void mob_fruit_save(void* vfruit, struct Map* map, byte* buffer, int* pos);
void mob_fruit_load(void* vfruit, struct Map* map, byte* buffer, int* pos);
bool mob_fruit_sync_send(void* vfruit, struct Map* map, byte* buffer, int* pos);
void mob_fruit_sync_receive(void* vfruit, struct Map* map, byte* buffer, int* pos);

static MobType mob_registry[] = {
    {
        MOB_PON,
        MEDIUM,
        mob_pon_initialize,
        mob_pon_update,
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
        mob_fruit_render,
        mob_fruit_save,
        mob_fruit_load,
        mob_fruit_sync_send,
        mob_fruit_sync_receive,
    }
};

#endif
