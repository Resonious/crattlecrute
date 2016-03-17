#include "item.h"

void initialize_inventory(Inventory* inv, int cap) {
    inv->capacity = cap;
    size_t items_size = sizeof(ItemCommon) * cap;
    inv->items = aligned_malloc(items_size);
    SDL_memset(inv->items, 0, items_size);
    for (int i = 0; i < cap; i++) {
        inv->items[i].item_type_id = ITEM_NONE;
    }
}
