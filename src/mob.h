#ifndef MOB_H
#define MOB_H

#include "types.h"
#include "coords.h"
#include "data.h"

struct Map;
struct Game;
struct Character;
struct Controls;

enum MobSizeClass { SMALL, MEDIUM, LARGE };

#define MOB_FIELDS\
    int index, mob_type_id

#define PHYSICS_MOB_FIELDS\
    MOB_FIELDS; GenericBody body

typedef struct MobCommon { MOB_FIELDS; } MobCommon;

typedef struct PhysicsMob { PHYSICS_MOB_FIELDS; } PhysicsMob;

typedef struct MobType {
    int id;
    enum MobSizeClass size_class;
    Uint16 flags;

    void(*initialize)(void* mob, struct Game* game, struct Map* map, vec2 pos);
    void(*update)(void* mob, struct Game* game, struct Map* map);
    void(*interact)(void* mob, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
    void(*mob_interact)(void* mob, struct Game* game, struct Map* map, MobCommon* other_mob);
    void(*render)(void* mob, struct Game* game, struct Map* map);
    void(*transfer)(void* mob, struct Game* game, struct Map* map, byte rw, AbdBuffer* buf);
    bool(*sync_send)(void* mob, struct Game* game, struct Map* map, byte* buffer, int* pos);
    void(*sync_receive)(void* mob, struct Game* game, struct Map* map, byte* buffer, int* pos);
} MobType;

#define MOBF_HAS_BODY (1 << 1)
#define MOBF_UNIT_PUSH (1 << 2)
#define MOBF_UNIT_GET_PUSHED (1 << 3)
#define MOBF_UNIT_COLLIDE (MOBF_UNIT_PUSH | MOBF_UNIT_GET_PUSHED)

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
void pick_up_item(PhysicsMob* mob, int item_type, struct Game* game, struct Map* map, struct Character* guy, struct Controls* controls, void* data, DataCallback callback);

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
    MOB_GARDEN_CRATTLECRUTE,
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
void mob_pon_interact(void* vpon, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
void mob_pon_mob_interact(void* vpon, struct Game* game, struct Map* map, MobCommon* other_mob);
void mob_pon_render(void* vpon, struct Game* game, struct Map* map);
void mob_pon_transfer(void* mob, struct Game*, struct Map* map, byte rw, AbdBuffer* buf);
bool mob_pon_sync_send(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);
void mob_pon_sync_receive(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);

typedef struct MobFruit {
    PHYSICS_MOB_FIELDS;
    float dy;
} MobFruit;
void mob_fruit_initialize(void* vfruit, struct Game* game, struct Map* map, vec2 pos);
void mob_fruit_update(void* vfruit, struct Game* game, struct Map* map);
void mob_fruit_interact(void* vfruit, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
void mob_fruit_mob_interact(void* mob, struct Game* game, struct Map* map, MobCommon* other_mob);
void mob_fruit_render(void* vfruit, struct Game* game, struct Map* map);
void mob_fruit_transfer(void* mob, struct Game*, struct Map* map, byte rw, AbdBuffer* buf);
bool mob_fruit_sync_send(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);
void mob_fruit_sync_receive(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);

void mob_egg_initialize(void* vegg, struct Game* game, struct Map* map, vec2 pos);
void mob_egg_update(void* vegg, struct Game* game, struct Map* map);
void mob_egg_interact(void* vegg, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
void mob_egg_mob_interact(void* mob, struct Game* game, struct Map* map, MobCommon* other_mob);
void mob_egg_render(void* vegg, struct Game* game, struct Map* map);
void mob_egg_transfer(void* mob, struct Game*, struct Map* map, byte rw, AbdBuffer* buf);
bool mob_egg_sync_send(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);
void mob_egg_sync_receive(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);

typedef struct MobGardenCrattle {
    MOB_FIELDS;
    int character_index;
    bool just_switched_guys;
} MobGardenCrattle;
void mob_mgc_initialize(void* vmgc, struct Game* game, struct Map* map, vec2 pos);
void mob_mgc_update(void* vmgc, struct Game* game, struct Map* map);
void mob_mgc_interact(void* vmgc, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls);
void mob_mgc_render(void* vmgc, struct Game* game, struct Map* map);
void mob_mgc_transfer(void* mob, struct Game*, struct Map* map, byte rw, AbdBuffer* buf);
bool mob_mgc_sync_send(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);
void mob_mgc_sync_receive(void* mob, struct Game*, struct Map* map, byte* buffer, int* pos);

static MobType mob_registry[] = {
    {
        MOB_PON,
        MEDIUM,
        MOBF_HAS_BODY | MOBF_UNIT_COLLIDE,
        mob_pon_initialize,
        mob_pon_update,
        mob_pon_interact,
        mob_pon_mob_interact,
        mob_pon_render,
        mob_pon_transfer,
        mob_pon_sync_send,
        mob_pon_sync_receive,
    },
    {
        MOB_FRUIT,
        MEDIUM,
        MOBF_HAS_BODY | MOBF_UNIT_COLLIDE,
        mob_fruit_initialize,
        mob_fruit_update,
        mob_fruit_interact,
        mob_fruit_mob_interact,
        mob_fruit_render,
        mob_fruit_transfer,
        NULL, // mob_fruit_sync_send,
        NULL, // mob_fruit_sync_receive,
    },
    {
        MOB_EGG,
        MEDIUM,
        MOBF_HAS_BODY | MOBF_UNIT_COLLIDE,
        mob_egg_initialize,
        mob_egg_update,
        mob_egg_interact,
        mob_egg_mob_interact,
        mob_egg_render,
        mob_egg_transfer,
        mob_egg_sync_send,
        mob_egg_sync_receive,
    },
    {
        MOB_GARDEN_CRATTLECRUTE,
        SMALL,
        MOBF_HAS_BODY | MOBF_UNIT_COLLIDE,
        mob_mgc_initialize,
        mob_mgc_update,
        mob_mgc_interact,
        NULL,
        mob_mgc_render,
        mob_mgc_transfer,
        mob_mgc_sync_send,
        mob_mgc_sync_receive,
    }
};

#endif
