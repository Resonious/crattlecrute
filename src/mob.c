#include "mob.h"
#include "tilemap.h"
#include "assets.h"
#include "game.h"
#include "script.h"

// ==== PON ====

void mob_pon_initialize(void* vpon, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobPon) <= sizeof(MediumMob));

    MobPon* pon = (MobPon*)vpon;
    SDL_assert((int)(&pon->body) % 16 == 0);

    memset(&pon->body, 0, sizeof(GenericBody));
    pon->body.position = (vec4) { pos.x, pos.y, 0.0f, 0.0f };
    pon->body.old_position.simd = pon->body.position.simd;

    pon->body.top_sensors.x[S1X] = -25.0f;
    pon->body.top_sensors.x[S1Y] = 10.0f;
    pon->body.top_sensors.x[S1X] = 25.0f;
    pon->body.top_sensors.x[S1Y] = 10.0f;

    pon->body.bottom_sensors.x[S1X] = -25.0f;
    pon->body.bottom_sensors.x[S1Y] = -10.0f;
    pon->body.bottom_sensors.x[S2X] = 25.0f;
    pon->body.bottom_sensors.x[S2Y] = -10.0f;

    pon->body.left_sensors.x[S1X] = -25.0f;
    pon->body.left_sensors.x[S1Y] = 10.0f;
    pon->body.left_sensors.x[S1X] = -25.0f;
    pon->body.left_sensors.x[S1Y] = -10.0f;

    pon->body.right_sensors.x[S1X] = 25.0f;
    pon->body.right_sensors.x[S1Y] = 10.0f;
    pon->body.right_sensors.x[S1X] = 25.0f;
    pon->body.right_sensors.x[S1Y] = -10.0f;

    pon->velocity.simd = _mm_set1_ps(0.0f);
    pon->target_pos = pos;
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

    pon->body.position.simd = _mm_add_ps(pon->body.position.simd, pon->velocity.simd);
    collide_generic_body(&pon->body, &map->tile_collision);

    if (pon->body.hit_ceiling)
        pon->velocity.x[Y] = 0;
    if (pon->body.hit_wall)
        pon->velocity.x[X] = -(pon->velocity.x[X] * 0.5f);

    if (pon->velocity.x[X] > 0)
        pon->flip = SDL_FLIP_NONE;
    else if (pon->velocity.x[X] < 0)
        pon->flip = SDL_FLIP_HORIZONTAL;

    if (game->net_joining) {
    }
    else {
        if (rand() % 100 == 1) {
            if (rand() % 10 < 5)
                pon->velocity.x[X] = -5.0f;
            else
                pon->velocity.x[X] = 5.0f;
            pon->velocity.x[Y] = 6.5f;
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
    world_render_copy_ex(game, pon_tex, &src, pon->body.position.x, 90, 90, 0, &center, pon->flip);
}
void mob_pon_save(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    write_to_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
    write_to_buffer(buffer, pon->body.position.x, pos, sizeof(vec4));
    write_to_buffer(buffer, &pon->color, pos, sizeof(SDL_Color));
}
void mob_pon_load(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    read_from_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
    read_from_buffer(buffer, &pon->body.position.x, pos, sizeof(vec4));
    read_from_buffer(buffer, &pon->color, pos, sizeof(SDL_Color));

    pon->frame = 0;
    pon->frame_counter = 0;
    pon->frame_inc = 1;
}
bool mob_pon_sync_send(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;

    if (pon->hop) {
        write_to_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
        write_to_buffer(buffer, pon->body.position.x, pos, sizeof(vec4));
        pon->hop = false;
        return true;
    }
    else return false;
}
void mob_pon_sync_receive(void* vpon, struct Map* map, byte* buffer, int* pos) {
    MobPon* pon = (MobPon*)vpon;
    read_from_buffer(buffer, pon->velocity.x, pos, sizeof(vec4));
    read_from_buffer(buffer, pon->body.position.x, pos, sizeof(vec4));
}

// ==== SCRIPT ====

/*
void mob_script_initialize(void* vs, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobScript) <= sizeof(LargeMob));
    MobScript* mob = (MobScript*)vs;
    mob->pos = pos;
    mob->bytecode_size = 0;
    mob->self = NULL;
    SDL_memset(mob->bytecode, 0, sizeof(mob->bytecode));
}
void mob_script_update(void* vs, struct Game* game, struct Map* map) {
}
void mob_script_render(void* vs, struct Game* game, struct Map* map) {
}
void mob_script_save(void* vs, struct Map* map, byte* buffer, int* pos) {
    MobScript* mob = (MobScript*)vs;

    write_to_buffer(buffer, &mob->pos, pos, sizeof(vec2));
    write_to_buffer(buffer, &mob->bytecode_size, pos, sizeof(int));
    write_to_buffer(buffer, mob->bytecode, pos, mob->bytecode_size);
}
void mob_script_load(void* vs, struct Map* map, byte* buffer, int* pos) {
    MobScript* mob = (MobScript*)vs;
    mrb_state* mrb = map->game->mrb;

    *pos += SDL_strlcpy(mob->class_name, buffer + *pos, sizeof(mob->class_name));
    read_from_buffer(buffer, &mob->pos, pos, sizeof(vec2));
    read_from_buffer(buffer, &mob->bytecode_size, pos, sizeof(int));

    read_from_buffer(buffer, mob->bytecode, pos, mob->bytecode_size);
    if (!mrb_class_defined(mrb, mob->class_name)) {
        mrb_load_irep(mrb, mob->bytecode);
    }
    struct RClass* mob_class = mrb_class_get(mrb, mob->class_name);
    mob->self = mrb_obj_new(mrb, mob_class, 0, NULL);
    // TODO NO MORE SCRIPT MOB IN THIS SENSE!!!
    // Instead we'll have to implement asset packs that get added to game save data or something...
}
bool mob_script_sync_send(void* vs, struct Map* map, byte* buffer, int* pos) {
    return false;
}
void mob_script_sync_receive(void* vs, struct Map* map, byte* buffer, int* pos) {
}
*/
