#include "data.h"
#include "mob.h"
#include "tilemap.h"
#include "assets.h"
#include "game.h"
#include "script.h"
#include "character.h"
#include "egg.h"

// === Generic functions ===

void data_generic_body(byte rw, AbdBuffer* buf, GenericBody* body) {
    data_vec4(rw, buf, body->position.x);
    data_vec4(rw, buf, body->old_position.x);
    data_bool(rw, buf, &body->grounded);
}
void pick_up_item(PhysicsMob* mob, int item_type, struct Game* game, struct Map* map, struct Character* guy, struct Controls* controls, void* data, DataCallback callback) {
    if (
        just_pressed(controls, C_DOWN) &&
        guy->position.x[X] > mob->body.position.x[0] - 25 &&
        guy->position.x[X] < mob->body.position.x[0] + 25 &&
        guy->position.x[Y] > mob->body.position.x[1] - 20 &&
        guy->position.x[Y] < mob->body.position.x[1] + 20
    ) {
        int slot = find_good_inventory_slot(&guy->inventory);
        if (slot > -1) {
            if (guy->player_id == -1) {
                ItemCommon* item = set_item(&guy->inventory, game, slot, item_type);
                if (item && callback)
                    callback(data, item);
            }
            else
                game->net.set_item(game->current_scene_data, guy, game, slot, item_type, data, callback);

            game->net.despawn_mob(game->current_scene_data, map, game, mob);
        }
    }
}
void unit_collide(PhysicsMob* mob1, PhysicsMob* mob2, float strength) {
    if (mob1->mob_type_id == -1 || mob2->mob_type_id == -1)
        return;

    MobType* reg1 = &mob_registry[mob1->mob_type_id];
    MobType* reg2 = &mob_registry[mob2->mob_type_id];

    if ((reg1->flags & MOBF_UNIT_PUSH) && (reg2->flags & MOBF_UNIT_GET_PUSHED))
        push_generic_bodies(&mob1->body, &mob2->body, strength);
}

// ==== PON ====

#define set_pon_sensors(pon) set_collision_sensors(&pon->body, 34, 34, 0);

void mob_pon_initialize(void* vpon, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobPon) <= sizeof(MediumMob));

    MobPon* pon = (MobPon*)vpon;
    SDL_assert((int)(&pon->body) % 16 == 0);

    memset(&pon->body, 0, sizeof(GenericBody));
    pon->body.position = (vec4) { pos.x, pos.y, 0.0f, 0.0f };
    pon->body.old_position.simd = pon->body.position.simd;
    pon->body.push_velocity.simd = _mm_set1_ps(0.0f);
    set_pon_sensors(pon);

    pon->velocity.simd = _mm_set1_ps(0.0f);
    pon->frame = 0;
    pon->frame_counter = 0;
    pon->frame_inc = 1;
    pon->hop = false;
    if (pcg32_boundedrand(30) == 1) {
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

    pon->velocity.x[Y] -= map->gravity;
    if (pon->velocity.x[Y] < -17.0f)
      pon->velocity.x[Y] = -17.0f;

    MOVE_TOWARDS(pon->velocity.x[X], 0, pon->body.grounded ? 1.0f : 0.025f);

    pon->body.position.simd = _mm_add_ps(pon->body.position.simd, pon->velocity.simd);
    apply_push_velocity(&pon->body);
    collide_generic_body(&pon->body, &map->tile_collision);

    if (pon->body.hit_ceiling)
        pon->velocity.x[Y] = -1;
    if (pon->body.grounded)
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
        if (pon->body.grounded && pcg32_boundedrand(100) == 1) {
            if (pcg32_boundedrand(10) < 5)
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
void mob_pon_interact(void* vpon, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls) {
    MobPon* pon = (MobPon*)vpon;
    push_generic_bodies(&pon->body, (GenericBody*)character, 50.0f);
}
void mob_pon_mob_interact(void* vpon, struct Game* game, struct Map* map, MobCommon* other_mob) {
    unit_collide((PhysicsMob*)vpon, (PhysicsMob*)other_mob, 50.0f);
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
void mob_pon_transfer(void* vpon, struct Game* game, struct Map* map, byte rw, AbdBuffer* buf) {
    MobPon* pon = (MobPon*)vpon;
    data_vec4_a(rw, buf, pon->velocity.x, "Pon velocity");
    data_vec4_a(rw, buf, pon->body.position.x, "Pon position");
    data_vec4_a(rw, buf, pon->body.old_position.x, "Pon old position");
    data_color(rw, buf, &pon->color);
}
bool mob_pon_sync_send(void* vpon, struct Game* game, struct Map* map, byte* buffer, int* pos) {
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
void mob_pon_sync_receive(void* vpon, struct Game* game, struct Map* map, byte* buffer, int* pos) {
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
    fruit->body.push_velocity.simd = _mm_set1_ps(0.0f);
    fruit->body.position.x[X] = pos.x;
    fruit->body.position.x[Y] = pos.y;
    fruit->body.old_position.simd = fruit->body.position.simd;
    fruit->body.grounded = false;
    fruit->dy = 0;
}
void mob_fruit_update(void* vfruit, struct Game* game, struct Map* map) {
    MobFruit* fruit = (MobFruit*)vfruit;

    fruit->dy -= map->gravity;
    if (fruit->dy < -17.0f)
        fruit->dy = -17.0f;
    fruit->body.position.x[Y] += fruit->dy;

    apply_push_velocity(&fruit->body);
    collide_generic_body(&fruit->body, &map->tile_collision);

    if (fruit->body.grounded)
        fruit->dy = 0.0f;

    fruit->body.old_position.simd = fruit->body.position.simd;
}
void mob_fruit_interact(void* vfruit, struct Game* game, struct Map* map, struct Character* guy, struct Controls* controls) {
    MobFruit* fruit = (MobFruit*)vfruit;
    pick_up_item((PhysicsMob*)fruit, ITEM_FRUIT, game, map, guy, controls, NULL, NULL);
}
void mob_fruit_mob_interact(void* vf, struct Game* game, struct Map* map, MobCommon* other_mob) {
    unit_collide((PhysicsMob*)vf, (PhysicsMob*)other_mob, 50.0f);
}
void mob_fruit_render(void* vfruit, struct Game* game, struct Map* map) {
    MobFruit* fruit = (MobFruit*)vfruit;

    vec2 p = { fruit->body.position.x[X], fruit->body.position.x[Y] };
    vec2 c = { 32, 32 };
    world_render_copy(game, cached_texture(game, ASSET_FOOD_FRUIT_PNG), NULL, &p, 64, 64, &c);
}
void mob_fruit_transfer(void* vfruit, struct Game* game, struct Map* map, byte rw, AbdBuffer* buf) {
    MobFruit* fruit = (MobFruit*)vfruit;
    data_section(rw, buf, "Fruit body:");
    data_generic_body(rw, buf, &fruit->body);
    data_float_a(rw, buf, &fruit->dy, "dy");
}

// NOTE these functions are not actually in the registry right now
bool mob_fruit_sync_send(void* vfruit, struct Game* game, struct Map* map, byte* buffer, int* pos) {
    MobFruit* fruit = (MobFruit*)vfruit;
    // TODO
    return false;
}
void mob_fruit_sync_receive(void* vfruit, struct Game* game, struct Map* map, byte* buffer, int* pos) {
    MobFruit* fruit = (MobFruit*)vfruit;
    // TODO
}

// ===== EGG =====

#define set_egg_sensors(egg) set_collision_sensors(&egg->body, 26, 40, 0);

void mob_egg_initialize(void* vegg, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobEgg) <= sizeof(MediumMob));
    MobEgg* egg = (MobEgg*)vegg;
    set_egg_sensors(egg);
    egg->body.push_velocity.simd = _mm_set1_ps(0.0f);
    egg->body.position.x[X] = pos.x;
    egg->body.position.x[Y] = pos.y;
    egg->body.old_position.simd = egg->body.position.simd;
    egg->body.grounded = false;
    egg->dy = 0;
    default_egg(&egg->e);
    egg->decided_color.a = 0;
    egg->net_flags = 0;
}
struct SpawnBabyData {
    Game* game;
    int index;
    MobEgg* egg;
};
void spawned_new_baby(void* data, void* mob) {
    MobGardenCrattle* crattle = (MobGardenCrattle*)mob;
    struct SpawnBabyData* spawn = (struct SpawnBabyData*)data;

    if (crattle)
        crattle->character_index = spawn->index;

    Character* guy = &spawn->game->characters[spawn->index];

    default_character(spawn->game, guy);
    default_character_animations(spawn->game, guy);
    randomize_character(guy);

    guy->genes = spawn->egg->e.genes;
    initialize_genes_with_colors(spawn->game, guy, &spawn->egg->decided_color, &spawn->egg->decided_eye_color, spawn->egg->decided_eye_type);

    load_character_atlases(spawn->game, guy);

    vec4 offset;
    offset.x[0] = 0; offset.x[1] = 50;
    offset.x[2] = 0; offset.x[3] = 0;
    guy->position.simd = _mm_add_ps(spawn->egg->body.position.simd, offset.simd);
    guy->old_position.simd = _mm_add_ps(spawn->egg->body.old_position.simd, offset.simd);

    SDL_strlcpy(guy->name, spawn->game->new_character_name_buffer, CHARACTER_NAME_LENGTH);
}

#define EGGNET_POKING_OUT (1 << 0)
#define EGGNET_HATCHED (1 << 1)

void mob_egg_update(void* vegg, struct Game* game, struct Map* map) {
    MobEgg* egg = (MobEgg*)vegg;

    egg->dy -= map->gravity;
    if (egg->dy < -17.0f)
        egg->dy = -17.0f;
    egg->body.position.x[Y] += egg->dy;

    apply_push_velocity(&egg->body);
    collide_generic_body(&egg->body, &map->tile_collision);

    if (egg->body.grounded)
        egg->dy = 0.0f;

    if (game->net_joining)
        return;

    if (area_is_garden(map->area_id)) {
        if (game->text_edit.text == NULL) {
            egg->e.age += 1;

            if (egg->e.age == egg->e.hatching_age) {
                genes_decide_body_color(&egg->e.genes, &egg->decided_color);
                genes_decide_eye_color(&egg->e.genes, &egg->decided_eye_color);
                genes_decide_eye_type(&egg->e.genes, &egg->decided_eye_type);

                start_editing_text(game, game->new_character_name_buffer, CHARACTER_NAME_LENGTH, NULL);
                game->pending_egg = egg;

                egg->net_flags |= EGGNET_POKING_OUT;
            }
        }
        else if (egg->e.age == egg->e.hatching_age && game->text_edit.text == game->new_character_name_buffer) {
            if (game->text_edit.enter_pressed || game->text_edit.canceled) {
                stop_editing_text(game);

                int character_index = game->data.character_count;

                if (character_index == 0)
                    game->data.character = 0;
                game->data.character_count += 1;

                Character* guy = &game->characters[character_index].guy;
                SDL_memset(guy, 0, sizeof(Character));

                vec2 pos;
                pos.x = egg->body.position.x[0];
                pos.y = egg->body.position.x[1];

                struct SpawnBabyData spawn;
                spawn.game = game;
                spawn.index = character_index;
                spawn.egg = egg;

                if (character_index > 0)
                    game->net.spawn_mob(game->current_scene_data, map, game, MOB_GARDEN_CRATTLECRUTE, pos, &spawn, spawned_new_baby);
                else
                    spawned_new_baby(&spawn, NULL);

                game->pending_egg = NULL;

                egg->net_flags |= EGGNET_HATCHED;
                egg->e.age += 1;
            }
        }
    }

    egg->body.old_position.simd = egg->body.position.simd;
}

static void set_egg_item_data(void* vd, void* vegg) {
    ItemEgg* egg = (ItemEgg*)vegg;
    egg->e = *(struct EggData*)vd;
    if (egg->e.age > egg->e.hatching_age)
        egg->layer_mask = (1 << 0) | (1 << 2);
}
void mob_egg_interact(void* vegg, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls) {
    MobEgg* egg = (MobEgg*)vegg;
    if (egg->e.age != egg->e.hatching_age)
        pick_up_item((PhysicsMob*)vegg, ITEM_EGG, game, map, character, ctrls, &egg->e, set_egg_item_data);
}
void mob_egg_mob_interact(void* ve, struct Game* game, struct Map* map, MobCommon* other_mob) {
    unit_collide((PhysicsMob*)ve, (PhysicsMob*)other_mob, 50.0f);
}
void mob_egg_render(void* vegg, struct Game* game, struct Map* map) {
    MobEgg* egg = (MobEgg*)vegg;

    vec2 p = { egg->body.position.x[X], egg->body.position.x[Y] };
    vec2 c = { 32, 32 };

    int image_width, image_height;
    AnimationAtlas* atlas = cached_atlas(game, ASSET_EGG_BASIC_PNG, 64, 64, 6, 3);
    SDL_QueryTexture(atlas->texture, NULL, NULL, &image_width, &image_height);

    MOD_SOLID_COLOR(egg->e, atlas->texture);

    if (egg->e.age < egg->e.hatching_age) {
        SDL_Rect src = src_rect_frame(3, image_width, image_height, 64, 64);

        // Just render the egg bottom and top on frames 3 and 5.
        world_render_copy(game, atlas->texture, &src, &p, 64, 64, &c);
        increment_src_rect(&src, 2, image_width, image_height);
        world_render_copy(game, atlas->texture, &src, &p, 64, 64, &c);
    }
    else {
        SDL_Rect src = src_rect_frame(0, image_width, image_height, 64, 64);

        world_render_copy(game, atlas->texture, &src, &p, 64, 64, &c);
        increment_src_rect(&src, 1, image_width, image_height);
        if (egg->e.age == egg->e.hatching_age) {
            Uint8 r, g, b;
            SDL_GetTextureColorMod(atlas->texture, &r, &g, &b);
            SDL_SetTextureColorMod(atlas->texture, egg->decided_color.r, egg->decided_color.g, egg->decided_color.b);
            world_render_copy(game, atlas->texture, &src, &p, 64, 64, &c);
            SDL_SetTextureColorMod(atlas->texture, r, g, b);

            draw_eye(game, atlas, &egg->body, egg->decided_eye_type, 0, 0, &egg->decided_eye_color);
        }

        increment_src_rect(&src, 2, image_width, image_height);
        world_render_copy(game, atlas->texture, &src, &p, 64, 64, &c);
    }

    UNMOD_SOLID_COLOR(egg->e, atlas->texture);

    if (game->text_edit.text == game->new_character_name_buffer) {
        SDL_Rect name_rect = {
            game->window_width / 2, game->window_height / 2,
            350, 40
        };
        name_rect.x -= name_rect.w / 2;
        name_rect.y -= name_rect.h / 2 + 64;

        set_text_color(game, 0, 0, 0);
        draw_text(game, name_rect.x, (game->window_height - name_rect.y) + 128, "Enter a name:");
        draw_text_box(game, &name_rect, game->text_edit.text);
    }
}
void mob_egg_transfer(void* vegg, struct Game* game, struct Map* map, byte rw, AbdBuffer* buf) {
    MobEgg* egg = (MobEgg*)vegg;
    data_section(rw, buf, "Egg");
    data_generic_body(rw, buf, &egg->body);
    data_float_a(rw, buf, &egg->dy, "dy");
    data_s32_a(rw, buf, &egg->e.age, "age");
    data_s32_a(rw, buf, &egg->e.hatching_age, "hatching age");
    data_s32_a(rw, buf, (Sint32*)&egg->e.genes, "genes");
    data_color_a(rw, buf, &egg->decided_color, "decided body color");
    data_color_a(rw, buf, &egg->decided_eye_color, "decided eye color");
    data_s32_a(rw, buf, &egg->decided_eye_type, "decided eye type");
}

bool mob_egg_sync_send(void* mob, struct Game* game, struct Map* map, byte* buffer, int* pos) {
    MobEgg* egg = (MobEgg*)mob;
    if (egg->net_flags == 0)
        return false;

    write_to_buffer(buffer, &egg->net_flags, pos, sizeof(int));

    if (egg->net_flags & (EGGNET_POKING_OUT | EGGNET_HATCHED)) {
        write_to_buffer(buffer, &egg->e.age, pos, sizeof(int));
        write_to_buffer(buffer, &egg->e.hatching_age, pos, sizeof(int));
        write_to_buffer(buffer, &egg->decided_color, pos, sizeof(SDL_Color));
        write_to_buffer(buffer, &egg->decided_eye_color, pos, sizeof(SDL_Color));
        write_to_buffer(buffer, &egg->decided_eye_type, pos, sizeof(int));
    }
    egg->net_flags = 0;
    return true;
}
void mob_egg_sync_receive(void* mob, struct Game* game, struct Map* map, byte* buffer, int* pos) {
    MobEgg* egg = (MobEgg*)mob;

    read_from_buffer(buffer, &egg->net_flags, pos, sizeof(int));

    if (egg->net_flags & (EGGNET_POKING_OUT | EGGNET_HATCHED)) {
        read_from_buffer(buffer, &egg->e.age, pos, sizeof(int));
        read_from_buffer(buffer, &egg->e.hatching_age, pos, sizeof(int));
        read_from_buffer(buffer, &egg->decided_color, pos, sizeof(SDL_Color));
        read_from_buffer(buffer, &egg->decided_eye_color, pos, sizeof(SDL_Color));
        read_from_buffer(buffer, &egg->decided_eye_type, pos, sizeof(int));
    }
    egg->net_flags = 0;
}

// ============= GARDEN CRATTLECRUTE ================

static Character* mgc_character(Game* game, MobGardenCrattle* mob) {
    if (game->net_joining) {
        Character* guy = game->misc_characters[mob->character_index];
        if (guy == NULL) {
            guy = aligned_malloc(sizeof(Character));
            SDL_memset(guy, 0, sizeof(Character));
            game->misc_characters[mob->character_index] = guy;
        }
        return guy;
    }
    else
        return &game->characters[mob->character_index];
}

void mob_mgc_initialize(void* vmgc, struct Game* game, struct Map* map, vec2 pos) {
    SDL_assert(sizeof(MobGardenCrattle) <= sizeof(SmallMob));

    MobGardenCrattle* mob = (MobGardenCrattle*)vmgc;
    mob->character_index = -1;
    mob->just_switched_guys = false;
}
void mob_mgc_update(void* vmgc, struct Game* game, struct Map* map) {
    MobGardenCrattle* mob = (MobGardenCrattle*)vmgc;
    if (mob->character_index < 0)
        return;

    Character* guy = mgc_character(game, mob);
    Controls* controls = NULL;
    int rstack = mrb_gc_arena_save(game->mrb);

    mrb_value rcontrols = mrb_ary_entry(game->ruby.controls, mob->character_index);
    if (mrb_obj_class(game->mrb, rcontrols) == game->ruby.controls_class)
        controls = DATA_PTR(rcontrols);

    update_genes(game, guy);
    apply_character_physics(game, guy, controls, map->gravity, map->drag);
    collide_character(guy, &map->tile_collision);
    slide_character(map->gravity, guy);
    update_character_animation(guy);

    character_post_update(guy);
    mrb_gc_arena_restore(game->mrb, rstack);
}
void mob_mgc_interact(void* vmgc, struct Game* game, struct Map* map, struct Character* character, struct Controls* ctrls) {
    MobGardenCrattle* mob = (MobGardenCrattle*)vmgc;
    if (mob->character_index < 0)
        return;
    Character* guy = mgc_character(game, mob);

    if (!game->net_joining && just_pressed(ctrls, C_DOWN) && character->player_id <= 0) {
        // TODO NETGAME ?????
        vec4 vdiff;
        vdiff.simd = _mm_sub_ps(character->position.simd, guy->position.simd);
        float diff = sqrtf(powf(vdiff.x[0], 2) + powf(vdiff.x[1], 2));

        if (diff <= 40.0f) {
            int become = game->data.character;
            game->data.character = mob->character_index;
            mob->character_index = become;
            SDL_memset(ctrls->this_frame, 0, sizeof(ctrls->this_frame));
            SDL_memset(ctrls->last_frame, 0, sizeof(ctrls->last_frame));
            mob->just_switched_guys = true;
        }
    }
}
void mob_mgc_render(void* vmgc, struct Game* game, struct Map* map) {
    MobGardenCrattle* mob = (MobGardenCrattle*)vmgc;
    if (mob->character_index < 0)
        return;

    Character* guy = mgc_character(game, mob);
    draw_character(game, guy, guy->view);
}
void mob_mgc_transfer(void* vmgc, struct Game* game, struct Map* map, byte rw, AbdBuffer* buf) {
    MobGardenCrattle* mob = (MobGardenCrattle*)vmgc;

    data_u32(rw, buf, &mob->character_index);

    if (mob->character_index >= 0) {
        Character* guy = mgc_character(game, mob);

        if (rw == ABD_WRITE)
            transfer_character(guy, ABD_WRITE, &game->data.characters[mob->character_index]);
        else {
            default_character(game, guy);
            default_character_animations(game, guy);
        }

        data_character_physics(guy, rw, buf);
        data_character(guy, rw, buf);

        if (rw == ABD_READ)
            load_character_atlases(game, guy);
    }
}
bool mob_mgc_sync_send(void* vmgc, struct Game* game, struct Map* map, byte* buffer, int* pos) {
    MobGardenCrattle* mob = (MobGardenCrattle*)vmgc;
    if (mob->character_index >= 0 && mob->just_switched_guys) {
        SDL_assert(!game->net_joining);
        mob->just_switched_guys = false;

        AbdBuffer buf = buf_new(buffer, *pos);
        Character* guy = mgc_character(game, mob);

        data_character_physics(guy, ABD_WRITE, &buf);
        data_character(guy, ABD_WRITE, &buf);

        *pos = buf.pos;

        return true;
    }

    return false;
}
void mob_mgc_sync_receive(void* vmgc, struct Game* game, struct Map* map, byte* buffer, int* pos) {
    MobGardenCrattle* mob = (MobGardenCrattle*)vmgc;
    AbdBuffer buf = buf_new(buffer, *pos);
    Character* guy = mgc_character(game, mob);

    data_character_physics(guy, ABD_WRITE, &buf);
    data_character(guy, ABD_WRITE, &buf);

    *pos = buf.pos;
}
