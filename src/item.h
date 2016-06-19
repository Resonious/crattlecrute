#ifndef ITEM_H
#define ITEM_H

#include "assets.h"
#include "types.h"

struct Map;

typedef struct ItemType {
    int id;
    int icon_asset;
    void(*initialize)(void* vitem);
    void(*render)(void* vitem, struct Game* game, SDL_Rect* dest);
    void(*transfer)(void* vitem, byte rw, AbdBuffer* buf);
    bool(*drop)(void* vitem, struct Game* game, struct Map* map, vec2 position);
} ItemType;

#define COMMON_ITEM       int item_type_id
#define LAYERED_ICON_ITEM int item_type_id; byte layer_mask

typedef struct ItemCommon {
    COMMON_ITEM;
    byte data[32 - sizeof(int)];
} ItemCommon;

#define LAYER_MASK_2_FRAMES ((1 << 0) | (1 << 1))

typedef struct LayeredIconItem {
    LAYERED_ICON_ITEM;
} LayeredIconItem;

typedef struct Inventory {
    SDL_mutex* locked;
    int capacity;
    ItemCommon* items;
} Inventory;

struct Game;
void initialize_inventory(Inventory* inv, int cap);
void free_inventory(Inventory* inv);
void data_inventory(byte rw, AbdBuffer* buf, Inventory* inv);
int find_good_inventory_slot(Inventory* inv);
void render_layered_icon_item(void* item, struct Game* game, SDL_Rect* dest);
void item_no_data_transfer(void* vitem, byte rw, AbdBuffer* buf);
ItemCommon* set_item(Inventory* inv, struct Game* game, int slot, int item_type_id);

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

void item_fruit_initialize(void* vitem);
// void item_fruit_transfer(void* vitem, struct Game* game, byte rw, AbdBuffer* buf);
bool item_fruit_drop(void* vitem, struct Game* game, struct Map* map, vec2 position);

// ItemEgg struct is defined in egg.h

void item_egg_initialize(void* vitem);
bool item_egg_drop(void* vitem, struct Game* game, struct Map* map, vec2 position);
void item_egg_transfer(void* vitem, byte rw, AbdBuffer* buf);
// copy/paste of layered icon thing
void item_egg_render(void* item, struct Game* game, SDL_Rect* dest);

static ItemType item_registry[] = {
    {
        ITEM_FRUIT, ASSET_FOOD_FRUIT_INV_PNG,
        item_fruit_initialize,
        render_layered_icon_item,
        item_no_data_transfer,
        item_fruit_drop
    },
    {
        ITEM_EGG, ASSET_EGG_BASIC_INV_PNG,
        item_egg_initialize,
        item_egg_render,
        item_egg_transfer,
        item_egg_drop
    }
};

#endif