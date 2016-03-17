#include "mob.h"
#include "tilemap.h"
#include "assets.h"
#include "game.h"
#include "script.h"

// ==== PON ====

#define set_pon_sensors(pon) set_collision_sensors(&pon->body, 50, 34, 0);

void mob_pon_initialize(void* vpon, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobPon) <= sizeof(MediumMob));

    MobPon* pon = (MobPon*)vpon;
    SDL_assert((int)(&pon->body) % 16 == 0);

    memset(&pon->body, 0, sizeof(GenericBody));
    pon->body.position = (vec4) { pos.x, pos.y, 0.0f, 0.0f };
    pon->body.old_position.simd = pon->body.position.simd;
    set_pon_sensors(pon);

    pon->velocity.simd = _mm_set1_ps(0.0f);
    pon->frame = 0;
    pon->frame_counter = 0;
    pon->frame_inc = 1;
    pon->hop = false;
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

    pon->velocity.x[Y] -= 1.15f;
    if (pon->velocity.x[Y] < -17.0f)
      pon->velocity.x[Y] = -17.0f;

    MOVE_TOWARDS(pon->velocity.x[X], 0, pon->body.grounded ? 1.0f : 0.025f);

    pon->body.position.simd = _mm_add_ps(pon->body.position.simd, pon->velocity.simd);
    collide_generic_body(&pon->body, &map->tile_collision);

    if (pon->body.hit_ceiling || pon->body.grounded)
        pon->velocity.x[Y] = 0;
    if (pon->body.hit_wall)
        pon->velocity.x[X] = -(pon->velocity.x[X]);

    if (pon->velocity.x[X] > 0)
        pon->flip = SDL_FLIP_NONE;
    else if (pon->velocity.x[X] < 0)
        pon->flip = SDL_FLIP_HORIZONTAL;

    if (game->net_joining) {
    }
    else {
        if (pon->body.grounded && rand() % 100 == 1) {
            if (rand() % 10 < 5)
                pon->velocity.x[X] = -5.0f;
            else
                pon->velocity.x[X] = 5.0f;
            pon->velocity.x[Y] = 13.0f;
            pon->body.grounded = false;
            pon->hop = true;
        }
    }

    pon->body.old_position.simd = pon->body.position.simd;
}
void mob_pon_render(void* vpon, struct Game* game, struct Map* map) {
    MobPon* pon = (MobPon*)vpon;

    SDL_Texture* pon_tex = cached_texture(game, ASSET_MOB_PON_PNG);
    SDL_Rect src = {0, 90, 90, 90};
    vec2 center = {90 / 2, 90 / 2};
    increment_src_rect(&src, pon->frame, 180, 180);

    SDL_SetTextureColorMod(pon_tex, pon->color.r, pon->color.g, pon->color.b);
    SDL_SetTextureAlphaMod(pon_tex, pon->color.a);
    world_render_copy_ex(game, pon_tex, &src, pon->body.position.x, 90, 90, pon->body.ground_angle, &center, pon->flip);
}
void mob_pon_save(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    write_to_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
    write_to_buffer(buffer, pon->body.position.x, pos, sizeof(vec4));
    write_to_buffer(buffer, pon->body.old_position.x, pos, sizeof(vec4));
    write_to_buffer(buffer, &pon->color, pos, sizeof(SDL_Color));
}
void mob_pon_load(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    memset(&pon->body, 0, sizeof(GenericBody));

    read_from_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
    read_from_buffer(buffer, pon->body.position.x, pos, sizeof(vec4));
    read_from_buffer(buffer, pon->body.old_position.x, pos, sizeof(vec4));
    read_from_buffer(buffer, &pon->color, pos, sizeof(SDL_Color));

    set_pon_sensors(pon);
    pon->frame = 0;
    pon->frame_counter = 0;
    pon->frame_inc = 1;
}
bool mob_pon_sync_send(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    
    if (pon->hop) {
        write_to_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
        write_to_buffer(buffer, pon->body.position.x, pos, sizeof(vec4));
        write_to_buffer(buffer, pon->body.old_position.x, pos, sizeof(vec4));
        write_to_buffer(buffer, &pon->body.grounded, pos, 1);
        write_to_buffer(buffer, &pon->body.ground_angle, pos, sizeof(float));
        pon->hop = false;
        return true;
    }
    else return false;
}
void mob_pon_sync_receive(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    read_from_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
    read_from_buffer(buffer, pon->body.position.x, pos, sizeof(vec4));
    read_from_buffer(buffer, pon->body.old_position.x, pos, sizeof(vec4));
    read_from_buffer(buffer, &pon->body.grounded, pos, 1);
    read_from_buffer(buffer, &pon->body.ground_angle, pos, sizeof(float));
}

// ==== FRUIT ====

#define set_fruit_sensors(fruit) set_collision_sensors(&fruit->body, 26, 37, 0);

void mob_fruit_initialize(void* vfruit, struct Game* game, struct Map* map, vec2 pos) {
    MobFruit* fruit = (MobFruit*)vfruit;
    set_fruit_sensors(fruit);
    fruit->body.position.x[X] = pos.x;
    fruit->body.position.x[Y] = pos.y;
    fruit->body.old_position.simd = fruit->body.position.simd;
    fruit->body.grounded = false;
    fruit->dy = 0;
}
void mob_fruit_update(void* vfruit, struct Game* game, struct Map* map) {
    MobFruit* fruit = (MobFruit*)vfruit;

    fruit->dy -= 1.15f;
    if (fruit->dy < -17.0f)
        fruit->dy = -17.0f;
    fruit->body.position.x[Y] += fruit->dy;

    collide_generic_body(&fruit->body, &map->tile_collision);
}
void mob_fruit_render(void* vfruit, struct Game* game, struct Map* map) {
    MobFruit* fruit = (MobFruit*)vfruit;

    vec2 p = { fruit->body.position.x[X], fruit->body.position.x[Y] };
    vec2 c = { 32, 32 };
    world_render_copy(game, cached_texture(game, ASSET_FOOD_FRUIT_PNG), NULL, &p, 64, 64, &c);
}
void mob_fruit_save(void* vfruit, struct Map* map, byte* buffer, int* pos) {
    MobFruit* fruit = (MobFruit*)vfruit;
    write_to_buffer(buffer, fruit->body.position.x, pos, sizeof(vec4));
    write_to_buffer(buffer, fruit->body.old_position.x, pos, sizeof(vec4));
    write_to_buffer(buffer, &fruit->body.grounded, pos, 1);
    write_to_buffer(buffer, &fruit->dy, pos, sizeof(float));
}
void mob_fruit_load(void* vfruit, struct Map* map, byte* buffer, int* pos) {
    MobFruit* fruit = (MobFruit*)vfruit;
    set_fruit_sensors(fruit);
    read_from_buffer(buffer, fruit->body.position.x, pos, sizeof(vec4));
    read_from_buffer(buffer, fruit->body.old_position.x, pos, sizeof(vec4));
    read_from_buffer(buffer, &fruit->body.grounded, pos, 1);
    read_from_buffer(buffer, &fruit->dy, pos, sizeof(float));
}

// NOTE these functions are not actually in the registry right now
bool mob_fruit_sync_send(void* vfruit, struct Map* map, byte* buffer, int* pos) {
    MobFruit* fruit = (MobFruit*)vfruit;
    // TODO
}
void mob_fruit_sync_receive(void* vfruit, struct Map* map, byte* buffer, int* pos) {
    MobFruit* fruit = (MobFruit*)vfruit;
    // TODO
}
