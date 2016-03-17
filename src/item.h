#ifndef ITEM_H
#define ITEM_H

#include "types.h"

typedef struct ItemType {
    int id;
    void(*initialize)(void* vitem, struct Game* game);
    void(*render)(void* vitem, struct Game* game, SDL_Rect* dest);
} ItemType;

#define COMMON_ITEM       int item_type_id
#define LAYERED_ICON_ITEM int item_type_id, asset, layer_count

typedef struct ItemCommon {
    COMMON_ITEM;
    byte data[128 - sizeof(int)];
} ItemCommon;

typedef struct LayeredIconItem {
    LAYERED_ICON_ITEM;
} LayeredIconItem;

typedef struct Inventory {
    int capacity;
    ItemCommon* items;
} Inventory;

struct Game;
void initialize_inventory(Inventory* inv, int cap);
void render_layered_icon_item(void* item, struct Game* game, SDL_Rect* dest);

// ============== ACTUAL ITEMS ===============

enum ItemId {
    ITEM_NONE = -1,
    ITEM_FRUIT,
    ITEM_EGG,
    NUMBER_OF_ITEM_TYPES
};

typedef struct ItemFruit {
    LAYERED_ICON_ITEM;
} ItemFruit;

void item_fruit_initialize(void* vitem, struct Game* game);

typedef struct ItemEgg {
    COMMON_ITEM;
} ItemEgg;

static ItemType item_registry[] = {
    {
        ITEM_FRUIT,
        item_fruit_initialize,
        render_layered_icon_item
    },
    {
        ITEM_EGG,
        NULL,
        NULL // TODO probably also layered
    }
};

#endif