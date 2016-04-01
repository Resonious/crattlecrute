#ifndef EGG_H
#define EGG_H

#include "item.h"
#include "mob.h"

struct EggData {
    int age;
    int hatching_age;
    SDL_Color body_color;
    SDL_Color eye_color;
    SDL_Color left_foot_color;
    SDL_Color right_foot_color;
};

typedef struct MobEgg {
    PHYSICS_MOB_FIELDS;
    float dy;
    struct EggData e;
} MobEgg;

typedef struct ItemEgg {
    LAYERED_ICON_ITEM;
    struct EggData e;
} ItemEgg;

void default_egg(struct EggData* egg);

#endif