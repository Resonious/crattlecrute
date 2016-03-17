#ifndef ITEM_H
#define ITEM_H

#include "types.h"

typedef struct ItemType {
    int id;
    void(*render)(void* item, struct Game* game, vec2 pos);
} ItemType;

typedef struct ItemCommon {
    int item_type_id;
    byte data[128 - sizeof(int)];
} ItemCommon;

typedef struct Inventory {
    int capacity;
    ItemCommon* items;
} Inventory;

struct Game;
void initialize_inventory(Inventory* inv, int cap);

// ============== ACTUAL ITEMS ===============

enum ItemId {
    ITEM_NONE = -1,
    ITEM_EGG,
    NUMBER_OF_ITEM_TYPES
};

typedef struct ItemEgg {
    int item_type_id;
} ItemEgg;

#endif