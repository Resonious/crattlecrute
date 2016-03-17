#include "item.h"
#include "game.h"
#include "assets.h"

void initialize_inventory(Inventory* inv, int cap) {
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
    SDL_Texture* tex = cached_texture(game, item->asset);
    int tex_width, tex_height;
    int r = SDL_QueryTexture(tex, NULL, NULL, &tex_width, &tex_height);
    r += SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    SDL_assert(r == 0);
    SDL_assert(item->layer_count > 0);

    SDL_Rect src = { 0, tex_height - 32, 32, 32 };

    for (int i = 0; i < item->layer_count; i++) {
        SDL_RenderCopy(game->renderer, tex, &src, dest);
        increment_src_rect(&src, 1, tex_width, tex_height);
    }
}

// ================== FRUIT =================

void item_fruit_initialize(void* vitem, struct Game* game) {
    ItemFruit* fruit = (ItemFruit*)vitem;
    fruit->asset = ASSET_FOOD_FRUIT_INV_PNG;
    fruit->layer_count = 2;
}