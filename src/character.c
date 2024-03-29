#include "coords.h"
#include "game.h"
#include "character.h"
#include "tilemap.h"
#include "egg.h"
#include <math.h>
#include <stdlib.h>

#define TERMINAL_VELOCITY 17.0f

#ifdef _DEBUG
extern bool debug_pause;
#endif

void interact_character_with_world(
    struct Game* game,
    struct Character* guy,
    struct Controls* controls,
    struct Map* map,
    void* data,
    void (*go_through_door)(void*, struct Game*, struct Character*, struct Door*)
) {
#define INTERACT_WITH_MOBS(size, list) \
    for (int i = 0; i < MAP_STATE_MAX_##size##_MOBS; i++) { \
        MobCommon* mob = (MobCommon*)&map->state->list[i]; \
        if (mob->mob_type_id != MOB_NONE) { \
            MobType* reg = &mob_registry[mob->mob_type_id]; \
            if (reg->interact) \
                reg->interact(mob, game, map, guy, controls); \
        } \
    }
    INTERACT_WITH_MOBS(SMALL,  small_mobs);
    INTERACT_WITH_MOBS(MEDIUM, medium_mobs);
    INTERACT_WITH_MOBS(LARGE,  large_mobs);

    for (int i = 0; i < map->number_of_doors; i++) {
        Door* door = &map->doors[i];

        if (!guy->just_went_through_door      &&
            just_pressed(controls, C_UP)      &&
            guy->position.x[X] > door->x + 21 &&
            guy->position.x[X] < door->x + 68 &&
            guy->position.x[Y] > door->y      &&
            guy->position.x[Y] < door->y + 90
        ) {
            if (!(door->flags & DOOR_VISIBLE)) {
                printf("Guy just tried to go through invisible door!\n");
                continue;
            }
            guy->just_went_through_door = true;
            if (go_through_door == NULL)
                printf("A character wanted to go through a door! (no callback provided)\n");
            else
                go_through_door(data, game, guy, door);
        }
    }
}

enum InventoryAction apply_character_inventory(Character* guy, struct Controls* controls, struct Game* game, struct Map* map) {
    enum InventoryAction action_taken = INV_NONE;
    // A:left, D:right
    if (just_pressed(controls, C_A)) {
        action_taken = INV_ACTION;
        guy->selected_slot -= 1;
    }
    if (just_pressed(controls, C_D)) {
        action_taken = INV_ACTION;
        guy->selected_slot += 1;
    }
    // Wrap to other side
    if (guy->selected_slot < 0)
        guy->selected_slot = guy->inventory.capacity - 1;
    if (guy->selected_slot >= guy->inventory.capacity)
        guy->selected_slot = 0;

    // "Grab" or drop grabbed item
    if (just_pressed(controls, C_W)) {
        action_taken = INV_ACTION;
        wait_for_then_use_lock(guy->inventory.locked);

        if (guy->grabbed_slot == -1) {
            if (guy->inventory.items[guy->selected_slot].item_type_id != ITEM_NONE)
                guy->grabbed_slot = guy->selected_slot;
        }
        else {
            ItemCommon* item = &guy->inventory.items[guy->grabbed_slot];
            SDL_assert(item->item_type_id >= 0 && item->item_type_id < NUMBER_OF_ITEM_TYPES);
            ItemType* reg = &item_registry[item->item_type_id];

            vec2 pos_offset;
            pos_offset.x = guy->flip == SDL_FLIP_HORIZONTAL ? -20.0f : 20.0f;
            pos_offset.y = 20;
            vec2 drop_pos = { guy->position.x[X], guy->position.x[Y] };
            v2_add_to(&drop_pos, pos_offset);

            if (reg->drop(item, game, map, drop_pos)) {
                guy->inventory.items[guy->grabbed_slot].item_type_id = ITEM_NONE;
            }

            guy->grabbed_slot = -1;
        }

        SDL_UnlockMutex(guy->inventory.locked);
    }

    // Swap grabbed if presetn, or toggle inventory
    if (just_pressed(controls, C_S)) {
        if (guy->grabbed_slot == -1) {
            action_taken = INV_TOGGLE;
        }
        else {
            action_taken = INV_ACTION;
            if (guy->grabbed_slot == guy->selected_slot) {
                guy->grabbed_slot = -1;
            }
            else {
                wait_for_then_use_lock(guy->inventory.locked);
                // Swap grabbed with slot item
                ItemCommon swap;
                SDL_memcpy(&swap, &guy->inventory.items[guy->selected_slot], sizeof(ItemCommon));
                SDL_memcpy(&guy->inventory.items[guy->selected_slot], &guy->inventory.items[guy->grabbed_slot], sizeof(ItemCommon));
                SDL_memcpy(&guy->inventory.items[guy->grabbed_slot], &swap, sizeof(ItemCommon));
                guy->grabbed_slot = -1;
                SDL_UnlockMutex(guy->inventory.locked);
            }
        }
    }

    return action_taken;
}

void apply_character_physics(struct Game* game, Character* guy, struct Controls* controls, float gravity, float drag) {
    Controls zero_controls;
    memset(zero_controls.this_frame, 0, sizeof(zero_controls.this_frame));
    memset(zero_controls.last_frame, 0, sizeof(zero_controls.last_frame));
    if (controls == NULL)
        controls = &zero_controls;

    guy->dy -= gravity; // times 1 frame
    if (!guy->grounded)
        MOVE_TOWARDS(guy->slide_speed, 0, drag);

    bool running = false;

    // Running?
    running = controls->this_frame[C_RUN];
    // Get accelerations from controls
    if (controls->this_frame[C_LEFT]) {
        guy->ground_speed -= CHARA_GROUND_ACCELERATION * guy->ground_acceleration;
        guy->flip = SDL_FLIP_HORIZONTAL;
        if (running && guy->grounded)
            guy->run_speed -= CHARA_GROUND_ACCELERATION * guy->ground_acceleration;
    }
    if (controls->this_frame[C_RIGHT]) {
        guy->ground_speed += CHARA_GROUND_ACCELERATION * guy->ground_acceleration;
        guy->flip = SDL_FLIP_NONE;
        if (running && guy->grounded)
            guy->run_speed += CHARA_GROUND_ACCELERATION * guy->ground_acceleration;
    }

    // JUMP
    if (guy->just_jumped) {
        guy->just_jumped = false;
        guy->stats.times_jumped += 1;
    }
    if (guy->grounded) {
        if (guy->jumped)
            guy->jumped = false;
        if (just_pressed(controls, C_JUMP)) {
            guy->grounded = false;
            guy->jumped = true;
            guy->just_jumped = true;
            guy->dy = CHARA_JUMP_ACCELERATION * guy->jump_acceleration;
        }
    }
    else if (just_released(controls, C_JUMP) && guy->jumped) {
        float cancel_point = guy->jump_cancel_dy * CHARA_JUMP_CANCEL_DY;
        if (guy->dy > cancel_point) {
            guy->dy = cancel_point;
            guy->stats.times_jump_canceled += 1;
        }
        guy->jumped = false;
    }

    // TODO having ground_deceleration > ground_acceleration will have a weird effect here.
    if (!controls->this_frame[C_LEFT] && !controls->this_frame[C_RIGHT]) {
        MOVE_TOWARDS(guy->ground_speed, 0, (CHARA_GROUND_DECELERATION * guy->ground_deceleration));
        MOVE_TOWARDS(guy->run_speed, 0, (CHARA_GROUND_DECELERATION * guy->ground_deceleration));
    }
    else if (!running) {
        MOVE_TOWARDS(guy->run_speed, 0, (CHARA_GROUND_DECELERATION * guy->ground_deceleration));
    }

    if (controls->this_frame[C_LEFT] || controls->this_frame[C_RIGHT])
        if (running)
            guy->animation_state = GUY_RUNNING;
        else
            guy->animation_state = GUY_WALKING;
    else
        guy->animation_state = GUY_IDLE;

    // Stats for running/walking/in-air
    if (guy->grounded) {
        guy->stats.frames_on_ground += 1;
        if (controls->this_frame[C_LEFT] || controls->this_frame[C_RIGHT]) {
            if (running)
                guy->stats.frames_ran += 1;
            else
                guy->stats.frames_walked += 1;
        }
    }
    else {
        guy->stats.frames_in_air += 1;
    }

    // Cap speeds
    float max_speed = CHARA_GROUND_SPEED_MAX * guy->ground_speed_max;
    if (guy->ground_speed > max_speed)
        guy->ground_speed = max_speed;
    else if (guy->ground_speed < -max_speed)
        guy->ground_speed = -max_speed;
    float max_run = CHARA_RUN_SPEED_MAX * guy->run_speed_max;
    if (guy->run_speed > max_run)
        guy->run_speed = max_run;
    else if (guy->run_speed < -max_run)
        guy->run_speed = -max_run;
    if (guy->dy < -TERMINAL_VELOCITY)
        guy->dy = -TERMINAL_VELOCITY;

    // Actually move the dude
    __m128 movement = {guy->ground_speed + guy->slide_speed + guy->run_speed, guy->dy, 0.0f, 0.0f};
    guy->position.simd = _mm_add_ps(guy->position.simd, movement);

    // Temporary ground @ -100px
    if (guy->position.x[1] < -100) {
        guy->position.x[1] = -100;
        guy->dy = 0;
    }
    if (!guy->grounded || guy->jumped) {
        guy->animation_state = GUY_JUMPING;
    }
}

void set_character_age(struct Game* game, Character* guy, int age) {
    Uint64 old_age = guy->age;
    guy->age = age;
    if (old_age < guy->age_of_maturity && guy->age >= guy->age_of_maturity) {
        guy->body_type = CRATTLECRUTE_STANDARD;
        guy->feet_type = CRATTLECRUTE_STANDARD;
        mature_genes(game, guy);
        set_character_bounds(guy);
        load_character_atlases(game, guy);
    }
}

void update_character_animation(Character* guy) {
    guy->animation_counter += 1;

    switch (guy->animation_state) {
    case GUY_IDLE:
        // increment every 10 frames
        if (guy->animation_counter % 10 == 0)
            guy->animation_frame += 1;

        // cap at 8
        if (guy->animation_frame >= 8)
            guy->animation_frame = 0;
        break;
    case GUY_WALKING: case GUY_RUNNING: {
        // the standard walking animation "walks" at about 10 pixels per frame. (as if this is based on that and not me just trying values)
        float disp = fabsf(guy->position.x[0] - guy->old_position.x[0]);
        int advance_every = (int)(1 / disp * 25.0f);
        if (advance_every <= 0)
            advance_every = 5;
        if (guy->animation_counter % advance_every == 0)
            guy->animation_frame += 1;

        // cap at 8
        if (guy->animation_frame >= 8)
            guy->animation_frame = 0;
    } break;
    case GUY_JUMPING:
        if (guy->dy >= 0)
            guy->animation_frame = 1;
        else {
            guy->animation_frame = (int)floorf(-guy->dy / 5.0f) + 1;
            if (guy->animation_frame >= 4)
                guy->animation_frame = 3;
        }

        break;
    default: guy->animation_frame = 0;
        break;
    }
}

void character_post_update(Character* guy) {
    guy->old_position = guy->position;
    guy->just_went_through_door = false;
}

void data_character_physics(Character* guy, byte rw, struct AbdBuffer* buf) {
    data_section(rw, buf, "character physics");

    data_vec4(rw, buf, guy->position.x);
    data_vec4(rw, buf, guy->old_position.x);
    data_float(rw, buf, &guy->ground_angle);
    data_float(rw, buf, &guy->dy);
    data_float(rw, buf, &guy->ground_speed);
    data_float(rw, buf, &guy->run_speed);
    data_float(rw, buf, &guy->slide_speed);
    data_bool(rw, buf, &guy->jumped);
    data_bool(rw, buf, &guy->just_jumped);
    data_s32(rw, buf, &guy->width);
    data_s32(rw, buf, &guy->height);
    data_s32(rw, buf, &guy->animation_state);
    data_s32(rw, buf, &guy->flip);
}

void data_character(Character* guy, byte rw, struct AbdBuffer* buf) {
    data_section(rw, buf, "character attributes");

    data_string(rw, buf, guy->name);
    data_float(rw, buf, &guy->ground_speed_max);
    data_float(rw, buf, &guy->run_speed_max);
    data_float(rw, buf, &guy->ground_acceleration);
    data_float(rw, buf, &guy->ground_deceleration);
    data_float(rw, buf, &guy->jump_acceleration);
    data_float(rw, buf, &guy->jump_cancel_dy);
    data_color(rw, buf, &guy->eye_color);
    data_color(rw, buf, &guy->body_color);
    data_color(rw, buf, &guy->left_foot_color);
    data_color(rw, buf, &guy->right_foot_color);
    data_s32(rw, buf, &guy->body_type);
    data_s32(rw, buf, &guy->feet_type);
    data_inventory(rw, buf, &guy->inventory);
    data_u32(rw, buf, &guy->age);
    data_u32(rw, buf, &guy->age_of_maturity);

    if (rw == ABD_READ) set_character_bounds(guy);
}

void transfer_character_physics(Character* guy, byte rw, struct DataChunk* chunk) {
    DATA_CHUNK_TO_BUF(rw, chunk, buf, 1024);
    data_character_physics(guy, rw, buf);
}

void transfer_character(Character* guy, byte rw, struct DataChunk* chunk) {
    DATA_CHUNK_TO_BUF(rw, chunk, buf, 1024);
    data_character(guy, rw, buf);
}

// ======= RENDERING =========

void draw_eye(struct Game* game, AnimationAtlas* atlas, struct GenericBody* body, int eye_type, int animation_frame, SDL_RendererFlip flip, SDL_Color* color) {
    SDL_Texture* eye_texture = cached_texture(game, EYE_TYPE_ASSETS[eye_type]);
    SDL_Rect src = { 0, 0, EYE_SPRITE_WIDTH, EYE_SPRITE_HEIGHT };

    PeripheralOffset* offset = &atlas->eye_offsets[animation_frame];

    float eye_angle = (flip == SDL_FLIP_HORIZONTAL ? 180.0f - offset->angle : offset->angle) - 90.0f;

    vec2 eye_offset = {
        (float)(flip == SDL_FLIP_HORIZONTAL ? -offset->x : offset->x),
        (float)offset->y
    };
    vec2 eye_pivot = { 1, 1 };

    // THIS is in RADIANS
    float eye_pos_angle = (body->ground_angle * (float)M_PI / 180.0f) + atan2f(eye_offset.y, eye_offset.x);
    float eye_pos_magnitude = v2_magnitude(&eye_offset);
    vec2 actual_eye_offset = {
        eye_pos_magnitude * cosf(eye_pos_angle),
        eye_pos_magnitude * sinf(eye_pos_angle)
    };

    // mat22 offset_rotation = rotation_mat22(guy->ground_angle);
    // eye_offset = mat_mul_22(&offset_rotation, &eye_offset);
    actual_eye_offset.x += body->position.x[X];
    actual_eye_offset.y += body->position.x[Y];

    SDL_SetTextureColorMod(eye_texture, color->r, color->g, color->b);
    world_render_copy_ex(
        game,
        eye_texture, &src,
        &actual_eye_offset, EYE_SPRITE_WIDTH, EYE_SPRITE_HEIGHT,
        body->ground_angle + eye_angle,
        &eye_pivot,
        flip
    );
}

void draw_character(struct Game* game, Character* guy, CharacterView* guy_view) {
    // Play sound effects!
    if (guy->just_jumped) {
        guy_view->jump_sound->samples_pos = 0;
        game->audio.oneshot_waves[GUY_JUMP_SOUND_CHANNEL] = guy_view->jump_sound;
    }

    // DRAW GUY
    AnimationAtlas* atlas = guy_view->body_animation_textures[guy->animation_state];
    const int sprite_width = 90, sprite_height = 90;
    const int number_of_layers = 3;
    SDL_Rect src = atlas->frames[guy->animation_frame];
    SDL_Rect dest = {
        (int)(guy->position.x[X] - guy->center_x - game->camera.x[X]),
        (int)(game->window_height - (guy->position.x[Y] + guy->center_y - game->camera.x[Y])),

        90, 90
    };
    // Chearfully assume that center_y is right after center_x and aligned the same as SDL_Point...
    SDL_Point* center = (SDL_Point*)&guy->center_x;

    // First pass to draw body shadow:
    {
        SDL_SetTextureColorMod(atlas->texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(atlas->texture, 135);
        SDL_Rect outline_dest = dest;
        SDL_Point outline_center = *center;
        outline_center.x += 1;
        outline_center.y -= 1;
        outline_dest.x -= 1;
        outline_dest.y += 1;
        outline_dest.w += 2;
        outline_dest.h += 2;
        src.x += sprite_width;
        if (src.x >= atlas->width) {
            src.x -= atlas->width;
            src.y -= sprite_height;
        }
        SDL_RenderCopyEx(game->renderer,
            atlas->texture,
            &src, &outline_dest,
            360 - guy->ground_angle, &outline_center, guy->flip
        );
    }
    SDL_SetTextureAlphaMod(atlas->texture, 255);
    // Second pass: draw character with color

    atlas = guy_view->feet_animation_textures[guy->animation_state];
    // Assuming the frames line up with each different atlas.
    src = atlas->frames[guy->animation_frame];
    for (int i = 0; i < number_of_layers; i++) {
        SDL_Color* color;
        if (i == 1) {
            color = &guy->body_color;
            atlas = guy_view->body_animation_textures[guy->animation_state];
        }
        else if (guy->flip == SDL_FLIP_HORIZONTAL) {
            atlas = guy_view->feet_animation_textures[guy->animation_state];
            if (i == 0)
                color = &guy->right_foot_color;
            else
                color = &guy->left_foot_color;
        }
        else {
            atlas = guy_view->feet_animation_textures[guy->animation_state];
            if (i == 0)
                color = &guy->left_foot_color;
            else
                color = &guy->right_foot_color;
        }

        SDL_SetTextureColorMod(atlas->texture, color->r, color->g, color->b);
        SDL_RenderCopyEx(game->renderer,
            atlas->texture,
            &src, &dest,
            360 - guy->ground_angle, center, guy->flip
        );
        src.x += sprite_width;
        while (src.x >= atlas->width) {
            src.x -= atlas->width;
            src.y -= sprite_height;
        }
    }

    // And now the eye.
    draw_eye(game, guy_view->body_animation_textures[guy->animation_state], (GenericBody*)guy, guy->eye_type, guy->animation_frame, guy->flip, &guy->eye_color);

    // Draw sensors for debug
#ifdef _DEBUG
    if (debug_pause) {
        dest.x += guy->center_x;
        SDL_Rect offset = { 0, 0, 1, 1 };
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor(game->renderer, &r, &b, &g, &a);

        // TOP
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 255, 255);
        offset.x = dest.x + guy->top_sensors.x[S1X];
        offset.y = dest.y + guy->center_y - guy->top_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + guy->top_sensors.x[S2X];
        offset.y = dest.y + guy->center_y - guy->top_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);

        // BOTTOM
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 255, 255);
        offset.x = dest.x + guy->bottom_sensors.x[S1X];
        offset.y = dest.y + guy->center_y - guy->bottom_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + guy->bottom_sensors.x[S2X];
        offset.y = dest.y + guy->center_y - guy->bottom_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);

        // LEFT
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 0, 255);
        offset.x = dest.x + guy->left_sensors.x[S1X];
        offset.y = dest.y + guy->center_y - guy->left_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + guy->left_sensors.x[S2X];
        offset.y = dest.y + guy->center_y - guy->left_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);

        // RIGHT
        SDL_SetRenderDrawColor(game->renderer, 255, 0, 0, 255);
        offset.x = dest.x + guy->right_sensors.x[S1X];
        offset.y = dest.y + guy->center_y - guy->right_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + guy->right_sensors.x[S2X];
        offset.y = dest.y + guy->center_y - guy->right_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);

        // MIDDLE
        SDL_SetRenderDrawColor(game->renderer, 0, 255, 255, 255);
        offset.x = dest.x + guy->middle_sensors.x[S1X];
        offset.y = dest.y + guy->center_y - guy->middle_sensors.x[S1Y];
        SDL_RenderFillRect(game->renderer, &offset);
        offset.x = dest.x + guy->middle_sensors.x[S2X];
        offset.y = dest.y + guy->center_y - guy->middle_sensors.x[S2Y];
        SDL_RenderFillRect(game->renderer, &offset);


        SDL_SetRenderDrawColor(game->renderer, r, g, b, a);
    }
#endif
}

void randomize_character(Character* guy) {
    guy->body_color.r = pcg32_boundedrand(255);
    guy->body_color.g = pcg32_boundedrand(255);
    guy->body_color.b = pcg32_boundedrand(255);
    guy->left_foot_color.r = pcg32_boundedrand(255);
    guy->left_foot_color.g = pcg32_boundedrand(255);
    guy->left_foot_color.b = pcg32_boundedrand(255);
    if (PERCENT_CHANCE(75))
        guy->right_foot_color = guy->left_foot_color;
    else {
        guy->right_foot_color.r = pcg32_boundedrand(255);
        guy->right_foot_color.g = pcg32_boundedrand(255);
        guy->right_foot_color.b = pcg32_boundedrand(255);
    }
    if (PERCENT_CHANCE(75)) {
        guy->eye_color.r = pcg32_boundedrand(255);
        guy->eye_color.g = pcg32_boundedrand(255);
        guy->eye_color.b = pcg32_boundedrand(255);
    }
    SDL_AtomicSet(&guy->dirty, true);
}

void set_character_bounds(Character* target) {
    // top1: left
    target->top_sensors.x[S1X] = -14;
    target->top_sensors.x[S1Y] = 27;
    // top2: right
    target->top_sensors.x[S2X] = 13;
    target->top_sensors.x[S2Y] = 27;

    // bottom1: left
    target->bottom_sensors.x[S1X] = -14;
    target->bottom_sensors.x[S1Y] = -29;
    // bottom2: right
    target->bottom_sensors.x[S2X] = 13;
    target->bottom_sensors.x[S2Y] = -29;

    // left1: top
    target->left_sensors.x[S1X] = -15;
    target->left_sensors.x[S1Y] = 26;
    // left2: bottom
    target->left_sensors.x[S2X] = -14;
    target->left_sensors.x[S2Y] = -28;

    // right1: top
    target->right_sensors.x[S1X] = 14;
    target->right_sensors.x[S1Y] = 26;
    // right2: bottom
    target->right_sensors.x[S2X] = 14;
    target->right_sensors.x[S2Y] = -28;

    if (target->age < target->age_of_maturity) {
        vec4i top_offset;
        top_offset.rect = (SDL_Rect) { +7, -7, -7, -7 };
        target->top_sensors.simd = _mm_add_epi32(target->top_sensors.simd, top_offset.simd);

        vec4i bottom_offset;
        bottom_offset.rect = (SDL_Rect) {+7, +7, -7, +7};
        target->bottom_sensors.simd = _mm_add_epi32(target->bottom_sensors.simd, bottom_offset.simd);

        vec4i left_offset;
        left_offset.rect = (SDL_Rect) {+7, -7, +7, +7};
        target->left_sensors.simd = _mm_add_epi32(target->left_sensors.simd, left_offset.simd);

        vec4i right_offset;
        right_offset.rect = (SDL_Rect) {-7, -7, -7, +7};
        target->right_sensors.simd = _mm_add_epi32(target->right_sensors.simd, right_offset.simd);
    }

    target->middle_sensors.x[S1X] = (int)target->left_sensors.x[S1X];
    target->middle_sensors.x[S1Y] = (int)((target->left_sensors.x[S1Y] + target->left_sensors.x[S2Y]) / 2.0f);
    target->middle_sensors.x[S2X] = (int)target->right_sensors.x[S1X];
    target->middle_sensors.x[S2Y] = (int)((target->right_sensors.x[S1Y] + target->right_sensors.x[S2Y]) / 2.0f);
}

void default_character(struct Game* game, Character* target) {
    target->age = 0;
    target->age_of_maturity = 2 HOURS;

    target->player_id = -1;
    target->animation_counter = 0;
    target->width  = 90;
    target->height = 90;
    target->center_x = 45;
    target->center_y = 45;
    target->dy = 0;
    target->ground_speed = 0.0f;
    target->run_speed = 0.0f;

    // "Attributes"
    target->ground_speed_max = 1.0f;
    target->run_speed_max = 1.0f;
    target->ground_acceleration = 1.0f;
    target->ground_deceleration = 1.0f;
    target->jump_acceleration = 1.0f;
    target->jump_cancel_dy = 1.0f;

    // "Stats" (not bar-stats or whatever)
    SDL_memset(&target->stats, 0, sizeof(target->stats));

    target->push_velocity.simd = _mm_set1_ps(0.0f);
    target->ground_angle = 0.0f;
    target->slide_speed = 0.0f;
    target->position.x[0] = 0.0f;
    target->position.x[1] = 0.0f;
    target->position.x[2] = 0.0f;
    target->position.x[3] = 0.0f;
    target->grounded = false;
    target->jumped = false;
    target->just_jumped = false;
    target->animation_state = GUY_IDLE;
    target->animation_frame = 0;
    target->flip = SDL_FLIP_NONE;
    target->left_hit = false;
    target->right_hit = false;

    set_character_bounds(target);

    target->just_went_through_door = false;

    target->body_type = CRATTLECRUTE_YOUNG;
    target->feet_type = CRATTLECRUTE_YOUNG;

    target->body_color.r = 0;
    target->body_color.g = 210;
    target->body_color.b = 255;
    target->body_color.a = 255;

    target->left_foot_color.r = 255;
    target->left_foot_color.g = 0;
    target->left_foot_color.b = 0;
    target->left_foot_color.a = 255;
    target->right_foot_color = target->left_foot_color;

    target->eye_color.a = 255;
    target->eye_color.r = 255;
    target->eye_color.g = 255;
    target->eye_color.b = 255;
    target->eye_type = 0;

    SDL_AtomicSet(&target->dirty, false);

    target->selected_slot = 0;
    target->grabbed_slot = -1;
    initialize_inventory(&target->inventory, 10);

    // TODO TEMP add fruit to test rendering and swapping
    set_item(&target->inventory, game, 0, ITEM_FRUIT);

    // Script stuff:

#define RUBY_MEMBER(attr, type) \
    if (!mrb_nil_p(target->r##attr)) \
        mrb_gc_unregister(game->mrb, target->r##attr); \
    target->r##attr = mrb_instance_alloc(game->mrb, game->ruby.type##_class); \
    mrb_data_init(target->r##attr, &target->attr, &mrb_dont_free_type); \
    mrb_gc_register(game->mrb, target->r##attr); \

    RUBY_MEMBER(body_color,       color);
    RUBY_MEMBER(eye_color,        color);
    RUBY_MEMBER(left_foot_color,  color);
    RUBY_MEMBER(right_foot_color, color);
    RUBY_MEMBER(inventory, inventory);
}

void load_character_atlases(struct Game* game, Character* guy) {
    if (!game->renderer) return;

    for (int i = 0; i < GUY_ANIMATION_COUNT; i++) {
        guy->view->body_animation_textures[i] = cached_atlas(game, ASSETS_FOR_ANIMATIONS[guy->body_type][i], CHARACTER_SPRITE_WIDTH, CHARACTER_SPRITE_HEIGHT, CHARACTER_LAYERS, CHARACTER_EYE_LAYER);
        guy->view->feet_animation_textures[i] = cached_atlas(game, ASSETS_FOR_ANIMATIONS[guy->feet_type][i], CHARACTER_SPRITE_WIDTH, CHARACTER_SPRITE_HEIGHT, CHARACTER_LAYERS, CHARACTER_EYE_LAYER);
    }
}

void default_character_animations(struct Game* game, Character* guy) {
    if (!game->renderer) {
        printf("No renderer - no guy view\n");
        guy->view = NULL;
        return;
    }

    guy->view = malloc(sizeof(CharacterView));
    CharacterView* view = guy->view;

    load_character_atlases(game, guy);

    view->jump_sound = cached_sound(game, ASSET_SOUNDS_JUMP_OGG);
}
