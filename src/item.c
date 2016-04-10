#include "item.h"
#include "game.h"
#include "assets.h"
#include "egg.h"

void initialize_inventory(Inventory* inv, int cap) {
    inv->locked = SDL_CreateMutex();
    inv->capacity = cap;
    size_t items_size = sizeof(ItemCommon) * cap;
    inv->items = aligned_malloc(items_size);
    SDL_memset(inv->items, 0, items_size);
    for (int i = 0; i < cap; i++) {
        inv->items[i].item_type_id = ITEM_NONE;
    }
}

void render_layered_icon_item(void* vitem, struct Game* game, SDL_Rect* dest) {
    LayeredIconItem* item = (LayeredIconItem*)vitem;
    int layer_mask;
    SDL_Texture* tex;

    if (item->item_type_id < 0 || item->item_type_id >= NUMBER_OF_ITEM_TYPES) {
        // Show question mark for messed up items.
        layer_mask = LAYER_MASK_2_FRAMES;
        tex = cached_texture(game, ASSET_MISC_Q_INV_PNG);
    }
    else {
        ItemType* reg = &item_registry[item->item_type_id];
        layer_mask = item->layer_mask;
        tex = cached_texture(game, reg->icon_asset);
    }

    int tex_width, tex_height;
    int r = SDL_QueryTexture(tex, NULL, NULL, &tex_width, &tex_height)
          + SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    SDL_assert(r == 0);

    for (int i = 0; i < 8; i++) {
        if (item->layer_mask & (1 << i)) {
            SDL_Rect src = src_rect_frame(i, tex_width, tex_height, 32, 32);
            SDL_RenderCopy(game->renderer, tex, &src, dest);
        }
    }
}

int find_good_inventory_slot(Inventory* inv) {
    for (int i = 0; i < inv->capacity; i++) {
        if (inv->items[i].item_type_id == ITEM_NONE)
            return i;
    }
    return -1;
}

ItemCommon* set_item(Inventory* inv, struct Game* game, int slot, int type) {
    if (slot < 0)
        return NULL;

    ItemCommon* item = &inv->items[slot];
    item->item_type_id = type;
    item_registry[type].initialize(item, game);

    return item;
}

// ================== FRUIT =================

void item_fruit_initialize(void* vitem, struct Game* game) {
    SDL_assert(sizeof(Inventory) <= sizeof(ItemCommon));
    ItemFruit* fruit = (ItemFruit*)vitem;
    fruit->layer_mask = LAYER_MASK_2_FRAMES;
}

bool item_fruit_drop(void* vitem, struct Game* game, struct Map* map, vec2 position) {
    // ItemFruit* fruit = (ItemFruit*)vitem;

    game->net.spawn_mob(game->current_scene_data, map, game, MOB_FRUIT, position, NULL, NULL);

    return true;
}

// ================== EGG ===================

void item_egg_initialize(void* vitem, struct Game* game) {
    SDL_assert(sizeof(ItemEgg) <= sizeof(ItemCommon));
    ItemEgg* egg = (ItemEgg*)vitem;
    egg->layer_mask = LAYER_MASK_2_FRAMES;
    egg->e.age = 0;
    egg->e.hatching_age = 15 MINUTES;
    default_egg(&egg->e);
}
void item_egg_render(void* vitem, struct Game* game, SDL_Rect* dest) {
    ItemEgg* egg = (ItemEgg*)vitem;
    int layer_mask;
    SDL_Texture* tex;

    if (egg->item_type_id < 0 || egg->item_type_id >= NUMBER_OF_ITEM_TYPES) {
        // Show question mark for messed up items.
        layer_mask = LAYER_MASK_2_FRAMES;
        tex = cached_texture(game, ASSET_MISC_Q_INV_PNG);
    }
    else {
        ItemType* reg = &item_registry[egg->item_type_id];
        layer_mask = egg->layer_mask;
        tex = cached_texture(game, reg->icon_asset);
    }

    int tex_width, tex_height;
    int r = SDL_QueryTexture(tex, NULL, NULL, &tex_width, &tex_height)
          + SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    SDL_assert(r == 0);

    MOD_SOLID_COLOR(egg->e, tex);
    for (int i = 0; i < 8; i++) {
        if (egg->layer_mask & (1 << i)) {
            SDL_Rect src = src_rect_frame(i, tex_width, tex_height, 32, 32);
            SDL_RenderCopy(game->renderer, tex, &src, dest);
        }
    }

    UNMOD_SOLID_COLOR(egg->e, tex);
}
void egg_dropped(void* vdata, void* vegg) {
    MobEgg* egg_drop = (MobEgg*)vegg;
    struct EggData* data = vdata;
    egg_drop->e = *data;
}
bool item_egg_drop(void* vitem, struct Game* game, struct Map* map, vec2 position) {
    ItemEgg* egg = (ItemEgg*)vitem;

    game->net.spawn_mob(game->current_scene_data, map, game, MOB_EGG, position, &egg->e, egg_dropped);

    return true;
}
