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
    byte data[64 - sizeof(int) * 2];
} SmallMob;

typedef struct MediumMob {
    MOB_FIELDS;
    byte data[256 - sizeof(int) * 2];
} MediumMob;

typedef struct LargeMob {
    MOB_FIELDS;
    byte data[1024 - sizeof(int) * 2];
} LargeMob;

// ============== ACTUAL MOBS ===============
// ==========
// NOTE: ALL Mob structs MUST lead MOB_FIELDS.
// ==========

// === Mobs === (THIS ENUM IS READ BY A RUBY SCRIPT AT COMPILE TIME)
enum MobId {
    MOB_PON,
    MOB_SCRIPT,
    NUMBER_OF_MOB_TYPES
};

typedef struct MobPon {
    MOB_FIELDS;

    int pad : 12;
    GenericBody body;
    // Pixels per frame
    vec4 velocity;
    vec2 target_pos;
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

/*
typedef struct MobScript {
    MOB_FIELDS;

    vec2 pos;

    char class_name[30];
    int bytecode_size;
    byte bytecode[1024 * 4];

    mrb_value self;
} MobScript;
void mob_script_initialize(void* vpon, struct Game* game, struct Map* map, vec2 pos);
void mob_script_update(void* vpon, struct Game* game, struct Map* map);
void mob_script_render(void* vpon, struct Game* game, struct Map* map);
void mob_script_save(void* vpon, struct Map* map, byte* buffer, int* pos);
void mob_script_load(void* vpon, struct Map* map, byte* buffer, int* pos);
bool mob_script_sync_send(void* vpon, struct Map* map, byte* buffer, int* pos);
void mob_script_sync_receive(void* vpon, struct Map* map, byte* buffer, int* pos);
*/

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
    /*
    {
        MOB_SCRIPT,
        LARGE,
        mob_script_initialize,
        mob_script_update,
        mob_script_render,
        mob_script_save,
        mob_script_load,
        mob_script_sync_send,
        mob_script_sync_receive,
    }
    */
};

#endif
