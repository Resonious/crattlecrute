#include "coords.h"
#include "game.h"
#include "character.h"
#include "tilemap.h"
#include <math.h>

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
    for (int i = 0; i < map->number_of_doors; i++) {
        Door* door = &map->doors[i];

        if (!guy->just_went_through_door      &&
            just_pressed(controls, C_UP)      &&
            guy->position.x[X] > door->x + 21 &&
            guy->position.x[X] < door->x + 68 &&
            guy->position.x[Y] > door->y      &&
            guy->position.x[Y] < door->y + 90
        ) {
            guy->just_went_through_door = true;
            if (go_through_door == NULL)
                printf("A character wanted to go through a door! (no callback provided)\n");
            else
                go_through_door(data, game, guy, door);
        }
    }
}

enum InventoryAction apply_character_inventory(Character* guy, struct Controls* controls, struct Game* game, struct Map* map, SpawnMobFunc spawn) {
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
        if (guy->grabbed_slot == -1) {
            if (guy->inventory.items[guy->selected_slot].item_type_id != ITEM_NONE)
                guy->grabbed_slot = guy->selected_slot;
        }
        else {
            ItemCommon* item = &guy->inventory.items[guy->grabbed_slot];
            SDL_assert(item->item_type_id >= 0 && item->item_type_id < NUMBER_OF_ITEM_TYPES);
            ItemType* reg = &item_registry[item->item_type_id];

            vec2 pos_offset;
            pos_offset.x = guy->flip == SDL_FLIP_HORIZONTAL ? -10 : 10;
            pos_offset.y = 0;
            vec2 drop_pos = { guy->position.x[X], guy->position.x[Y] };
            v2_add_to(&drop_pos, pos_offset);

            if (reg->drop(item, game, map, drop_pos, spawn)) {
                guy->inventory.items[guy->grabbed_slot].item_type_id = ITEM_NONE;
            }

            guy->grabbed_slot = -1;
        }
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
                // Swap grabbed with slot item
                ItemCommon swap;
                SDL_memcpy(&swap, &guy->inventory.items[guy->selected_slot], sizeof(ItemCommon));
                SDL_memcpy(&guy->inventory.items[guy->selected_slot], &guy->inventory.items[guy->grabbed_slot], sizeof(ItemCommon));
                SDL_memcpy(&guy->inventory.items[guy->grabbed_slot], &swap, sizeof(ItemCommon));
                guy->grabbed_slot = -1;
            }
        }
    }

    return action_taken;
}

void apply_character_physics(struct Game* game, Character* guy, struct Controls* controls, float gravity, float drag) {
    guy->dy -= gravity; // times 1 frame
    if (!guy->grounded)
        MOVE_TOWARDS(guy->slide_speed, 0, drag);

    bool running = false;
    if (controls) {
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
        if (guy->just_jumped)
            guy->just_jumped = false;
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
            if (guy->dy > cancel_point)
                guy->dy = cancel_point;
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

// ======= RENDERING =========

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
        guy->position.x[X] - guy->center_x - game->camera.x[X],
        game->window_height - (guy->position.x[Y] + guy->center_y - game->camera.x[Y]),

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
    atlas = guy_view->body_animation_textures[guy->animation_state];
    SDL_Texture* eye_texture = cached_texture(game, EYE_TYPE_ASSETS[guy->eye_type]);
    src.x = 0; src.y = 0;
    src.w = EYE_SPRITE_WIDTH;
    src.h = EYE_SPRITE_HEIGHT;

    PeripheralOffset* offset = &atlas->eye_offsets[guy->animation_frame];

    float eye_angle = (guy->flip == SDL_FLIP_HORIZONTAL ? 180.0f - offset->angle : offset->angle) - 90.0f;

    vec2 eye_offset = {
        (guy->flip == SDL_FLIP_HORIZONTAL ? -offset->x : offset->x),
        offset->y
    };
    vec2 eye_pivot = { 1, 1 };

    // THIS is in RADIANS
    float eye_pos_angle = (guy->ground_angle * (float)M_PI / 180.0f) + atan2f(eye_offset.y, eye_offset.x);
    float eye_pos_magnitude = v2_magnitude(&eye_offset);
    vec2 actual_eye_offset = {
        eye_pos_magnitude * cosf(eye_pos_angle),
        eye_pos_magnitude * sinf(eye_pos_angle)
    };

    // mat22 offset_rotation = rotation_mat22(guy->ground_angle);
    // eye_offset = mat_mul_22(&offset_rotation, &eye_offset);
    actual_eye_offset.x += guy->position.x[X];
    actual_eye_offset.y += guy->position.x[Y];

    SDL_SetTextureColorMod(eye_texture, guy->eye_color.r, guy->eye_color.g, guy->eye_color.b);
    world_render_copy_ex(
        game,
        eye_texture, &src,
        &actual_eye_offset, EYE_SPRITE_WIDTH, EYE_SPRITE_HEIGHT,
        guy->ground_angle + eye_angle,
        &eye_pivot,
        guy->flip
    );


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

void default_character(struct Game* game, Character* target) {
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

    target->middle_sensors.x[S1X] = target->left_sensors.x[S1X];
    target->middle_sensors.x[S1Y] = (target->left_sensors.x[S1Y] + target->left_sensors.x[S2Y]) / 2.0f;
    target->middle_sensors.x[S2X] = target->right_sensors.x[S1X];
    target->middle_sensors.x[S2Y] = (target->right_sensors.x[S1Y] + target->right_sensors.x[S2Y]) / 2.0f;

    target->just_went_through_door = false;

    target->body_type = CRATTLECRUTE_STANDARD;
    target->feet_type = CRATTLECRUTE_STANDARD;

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
    ItemFruit* fruit = &target->inventory.items[0];
    fruit->item_type_id = ITEM_FRUIT;
    item_registry[ITEM_FRUIT].initialize(fruit, game);

    // Script stuff:

    mrb_value zero = mrb_fixnum_value(0);
    mrb_value color_args[5];
    color_args[0] = zero; color_args[1] = zero; color_args[2] = zero; color_args[3] = zero;
    color_args[4] = mrb_fixnum_value(1);

#define RUBY_COLOR(attr) \
    target->r##attr = mrb_obj_new(game->mrb, game->ruby.color_class, 5, color_args); \
    SDL_assert(DATA_PTR(target->r##attr) == NULL); \
    mrb_data_init(target->r##attr, &target->attr, &mrb_dont_free_type);

    RUBY_COLOR(body_color);
    RUBY_COLOR(eye_color);
    RUBY_COLOR(left_foot_color);
    RUBY_COLOR(right_foot_color);
}

void load_character_atlases(struct Game* game, Character* guy) {
    if (!game->renderer) return;

    for (int i = 0; i < GUY_ANIMATION_COUNT; i++) {
        guy->view->body_animation_textures[i] = cached_atlas(game, ASSETS_FOR_ANIMATIONS[guy->body_type][i], CHARACTER_SPRITE_WIDTH, CHARACTER_SPRITE_HEIGHT, CHARACTER_EYE_LAYER);
        guy->view->feet_animation_textures[i] = cached_atlas(game, ASSETS_FOR_ANIMATIONS[guy->feet_type][i], CHARACTER_SPRITE_WIDTH, CHARACTER_SPRITE_HEIGHT, CHARACTER_EYE_LAYER);
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
