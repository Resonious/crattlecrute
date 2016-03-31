#ifndef EGG_H
#define EGG_H

#include "item.h"
#include "mob.h"

struct EggData {
    int age;
    int hatching_age;
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

#endif