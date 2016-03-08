#include "mob.h"
#include "tilemap.h"
#include "assets.h"
#include "game.h"

// ==== PON ====

void mob_pon_initialize(void* vpon, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobPon) <= sizeof(SmallMob));

    MobPon* pon = (MobPon*)vpon;

    pon->pos = pos;
    pon->target_pos = pos;
    pon->frame = 0;
    pon->frame_counter = 0;
    pon->frame_inc = 1;
    pon->velocity.x = 0;
    pon->velocity.y = 0;
    if (rand() % 30 == 1) {
        pon->color.r = 255;
        pon->color.g = 198;
        pon->color.b = 30;
    }
    else {
        pon->color.r = 135;
        pon->color.g = 135;
        pon->color.b = 135;
    }
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

    if (game->net_joining) {
        vec2 diff = v2_sub(pon->target_pos, pon->pos);
        pon->pos.x += diff.x * 0.1f;
        pon->pos.y += diff.y * 0.1f;
    }
    else {
        if (pon->velocity.x == 0 || pon->velocity.y == 0) {
            pon->velocity.x = rand() % 10;
            pon->velocity.y = rand() % 10;
        }
        if (pon->pos.x > map->width)
            pon->velocity.x = -(rand() % 10);
        if (pon->pos.x < 0)
            pon->velocity.x = (rand() % 10);

        if (pon->pos.y > map->height)
            pon->velocity.y = -(rand() % 10);
        if (pon->pos.y < 0)
            pon->velocity.y = (rand() % 10);

        v2_addeq(&pon->pos, v2_mul(1.0f/60.0f, pon->velocity));
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
void mob_pon_save(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    write_to_buffer(buffer, &pon->velocity, pos, sizeof(vec2));
    write_to_buffer(buffer, &pon->pos, pos, sizeof(vec2));
    write_to_buffer(buffer, &pon->color, pos, sizeof(SDL_Color));
}
void mob_pon_load(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    read_from_buffer(buffer, &pon->velocity, pos, sizeof(vec2));
    read_from_buffer(buffer, &pon->pos, pos, sizeof(vec2));
    read_from_buffer(buffer, &pon->color, pos, sizeof(SDL_Color));

    pon->frame = 0;
    pon->frame_counter = 0;
    pon->frame_inc = 1;
    pon->target_pos = pon->pos;
}
bool mob_pon_sync_send(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;

    if (pon->frame_counter % 20 == 0) {
        write_to_buffer(buffer, &pon->pos, pos, sizeof(vec2));
        return true;
    }
    else return false;
}
void mob_pon_sync_receive(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    read_from_buffer(buffer, &pon->target_pos, pos, sizeof(vec2));
}

// ==== SCRIPT ====

void mob_script_initialize(void* vs, struct Game* game, struct Map* map, vec2 pos) {
}
void mob_script_update(void* vs, struct Game* game, struct Map* map) {
}
void mob_script_render(void* vs, struct Game* game, struct Map* map) {
}
void mob_script_save(void* vs, struct Map* map, byte* buffer, int* pos) {
}
void mob_script_load(void* vs, struct Map* map, byte* buffer, int* pos) {
}
bool mob_script_sync_send(void* vs, struct Map* map, byte* buffer, int* pos) {
    return false;
}
void mob_script_sync_receive(void* vs, struct Map* map, byte* buffer, int* pos) {
}
