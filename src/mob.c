#include "mob.h"
#include "tilemap.h"
#include "assets.h"
#include "game.h"

void mob_pon_initialize(void* vpon, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobPon) <= sizeof(SmallMob));

    MobPon* pon = (MobPon*)vpon;
    printf("SPAWNING PON AT (%2.f, %2.f)\n", pos.x, pos.y);

    pon->pos = pos;
    pon->frame = 0;
    pon->frame_counter = 0;
    pon->frame_inc = 1;
    pon->color.r = 135;
    pon->color.g = 135;
    pon->color.b = 135;
    pon->color.a = 255;
}
void mob_pon_update(void* vpon, struct Game* game, struct Map* map) {
    MobPon* pon = (MobPon*)vpon;

    pon->frame_counter += 1;
    if (pon->frame_counter % 7 == 0) {
        pon->frame += pon->frame_inc;
        if (pon->frame == 2 || pon->frame == 0)
            pon->frame_inc *= -1;
    }
}
void mob_pon_render(void* vpon, struct Game* game, struct Map* map) {
    MobPon* pon = (MobPon*)vpon;

    SDL_Texture* pon_tex = cached_texture(game, ASSET_MOB_PON_PNG);
    SDL_Rect src = {0, 90, 90, 90};
    vec2 center = {90 / 2, 90 / 2};
    increment_src_rect(&src, pon->frame, 180, 180);

    SDL_SetTextureColorMod(pon_tex, pon->color.r, pon->color.g, pon->color.b);
    SDL_SetTextureAlphaMod(pon_tex, pon->color.a);
    world_render_copy(game, pon_tex, &src, &pon->pos, 90, 90, &center);
}