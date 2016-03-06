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

void apply_character_physics(struct Game* game, Character* guy, struct Controls* controls, float gravity, float drag) {
    guy->dy -= gravity; // times 1 frame
    if (!guy->grounded)
        MOVE_TOWARDS(guy->slide_speed, 0, drag);

    if (controls) {
        // Get accelerations from controls
        if (controls->this_frame[C_LEFT]) {
            guy->ground_speed -= guy->ground_acceleration;
            guy->flip = SDL_FLIP_HORIZONTAL;
        }
        if (controls->this_frame[C_RIGHT]) {
            guy->ground_speed += guy->ground_acceleration;
            guy->flip = SDL_FLIP_NONE;
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
                guy->dy = guy->jump_acceleration;
            }
        }
        else if (just_released(controls, C_JUMP) && guy->jumped) {
            const float jump_cancel_dy = 10.0f;
            if (guy->dy > jump_cancel_dy)
                guy->dy = jump_cancel_dy;
            guy->jumped = false;
        }


        // TODO having ground_deceleration > ground_acceleration will have a weird effect here.
        if (!controls->this_frame[C_LEFT] && !controls->this_frame[C_RIGHT]) {
            MOVE_TOWARDS(guy->ground_speed, 0, guy->ground_deceleration);
        }
        guy->animation_state = controls->this_frame[C_LEFT] || controls->this_frame[C_RIGHT] ? GUY_WALKING : GUY_IDLE;
    }

    // Cap speeds
    if (guy->ground_speed > guy->ground_speed_max)
        guy->ground_speed = guy->ground_speed_max;
    else if (guy->ground_speed < -guy->ground_speed_max)
        guy->ground_speed = -guy->ground_speed_max;
    if (guy->dy < -TERMINAL_VELOCITY)
        guy->dy = -TERMINAL_VELOCITY;

    // Actually move the dude
    __m128 movement = {guy->ground_speed + guy->slide_speed, guy->dy, 0.0f, 0.0f};
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
    case GUY_WALKING:
        // increment every 5 frames
        if (guy->animation_counter % 5 == 0)
            guy->animation_frame += 1;

        // cap at 8
        if (guy->animation_frame >= 8)
            guy->animation_frame = 0;
        break;
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
    AnimationAtlas* atlas = &guy_view->animation_textures[guy->animation_state];
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
    src = atlas->frames[guy->animation_frame];
    for (int i = 0; i < number_of_layers; i++) {
        SDL_Color* color;
        if (i == 1)
            color = &guy->body_color;
        else if (guy->flip == SDL_FLIP_HORIZONTAL) {
            if (i == 0)
                color = &guy->right_foot_color;
            else
                color = &guy->left_foot_color;
        }
        else {
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
        if (src.x >= atlas->width) {
            src.x -= atlas->width;
            src.y -= sprite_height;
        }
    }

    // And now the eye.
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

void default_character(Character* target) {
    target->player_id = -1;
    target->animation_counter = 0;
    target->width  = 90;
    target->height = 90;
    target->center_x = 45;
    target->center_y = 45;
    target->dy = 0;
    target->ground_speed = 0.0f;
    target->ground_speed_max = 6.0f;
    target->ground_acceleration = 0.8f;
    target->ground_deceleration = 0.5f;
    target->ground_angle = 0.0f;
    target->slide_speed = 0.0f;
    target->position.x[0] = 0.0f;
    target->position.x[1] = 0.0f;
    target->position.x[2] = 0.0f;
    target->position.x[3] = 0.0f;
    target->grounded = false;
    target->jumped = false;
    target->just_jumped = false;
    target->jump_acceleration = 20.0f;
    target->animation_state = GUY_IDLE;
    target->animation_frame = 0;
    target->flip = SDL_FLIP_NONE;
    target->left_hit = false;
    target->right_hit = false;

    target->top_sensors.x[S1X] = 31 - 45;
    target->top_sensors.x[S1Y] = 72 - 45;
    target->top_sensors.x[S2X] = 58 - 45;
    target->top_sensors.x[S2Y] = 72 - 45;

    target->bottom_sensors.x[S1X] = 31 - 45;
    target->bottom_sensors.x[S1Y] = 16 - 45;
    target->bottom_sensors.x[S2X] = 58 - 45;
    target->bottom_sensors.x[S2Y] = 16 - 45;

    target->left_sensors.x[S1X] = 30 - 45;
    target->left_sensors.x[S1Y] = 71 - 45;
    target->left_sensors.x[S2X] = 31 - 45;
    target->left_sensors.x[S2Y] = 17 - 45;

    target->right_sensors.x[S1X] = 59 - 45;
    target->right_sensors.x[S1Y] = 71 - 45;
    target->right_sensors.x[S2X] = 59 - 45;
    target->right_sensors.x[S2Y] = 17 - 45;

    target->middle_sensors.x[S1X] = target->left_sensors.x[S1X];
    target->middle_sensors.x[S1Y] = (target->left_sensors.x[S1Y] + target->left_sensors.x[S2Y]) / 2.0f;
    target->middle_sensors.x[S2X] = target->right_sensors.x[S1X];
    target->middle_sensors.x[S2Y] = (target->right_sensors.x[S1Y] + target->right_sensors.x[S2Y]) / 2.0f;

    target->just_went_through_door = false;

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
}

void default_character_animations(struct Game* game, CharacterView* view) {
    const int sprite_width = 90, sprite_height = 90;
    const int eye_offset_layer = 4; // 1-based.
    SDL_Rect* rect = view->rects_for_frames;
    PeripheralOffset* eye_offset = view->offsets_for_frames;

    for (int i = 0; i < GUY_ANIMATION_COUNT; i++) {
        AnimationAtlas* animation = &view->animation_textures[i];

        int asset = DEFAULT_ASSETS_FOR_ANIMATIONS[i];
        SDL_Surface* image = load_image(asset);
        // NOTE this is kind of an optimization for in case this is the first time this asset
        // has been loaded - which is probably the case. (So we don't load and free the image twice)
        CachedAsset* cached_tex = &game->asset_cache.assets[asset];
        if (cached_tex->id == ASSET_NOT_LOADED) {
            cached_tex->id = asset;
            cached_tex->texture = SDL_CreateTextureFromSurface(game->renderer, image);
            cached_tex->free = SDL_DestroyTexture;
        }

        animation->width       = image->w;
        animation->height      = image->h;
        animation->texture     = cached_tex->texture;
        animation->frames      = rect;
        animation->eye_offsets = eye_offset;

        int number_of_frames = (animation->width / sprite_width * animation->height / sprite_height) / CHARACTER_LAYERS;
        for (int frame = 0; frame < number_of_frames; frame++, rect++, eye_offset++) {
            rect->x = sprite_width * frame * CHARACTER_LAYERS;
            rect->y = animation->height - sprite_height;
            rect->w = sprite_width;
            rect->h = sprite_height;

            while (rect->x >= animation->width) {
                rect->x -= animation->width;
                rect->y -= sprite_height;
            }

            SDL_assert(rect->x >= 0);
            SDL_assert(rect->x + sprite_width <= animation->width);
            SDL_assert(rect->y >= 0);
            SDL_assert(rect->y + sprite_height <= animation->height);

            SDL_Rect eye_layer = *rect;
            eye_layer.x += sprite_width * (eye_offset_layer - 1);
            while (eye_layer.x >= animation->width) {
                eye_layer.x -= animation->width;
                eye_layer.y -= sprite_height;
            }

            SDL_Point eye_bottom = { 0,0 }, eye_top = { 0,0 };

            for (int y = eye_layer.y; y < eye_layer.y + eye_layer.h; y++) {
                for (int x = eye_layer.x; x < eye_layer.x + eye_layer.w; x++) {
                    int p = y * animation->width + x;
                    Uint32 pixel = ((Uint32*)image->pixels)[p];

                    if (pixel & AMASK) {
                        int pixel_x = x - eye_layer.x - eye_layer.w / 2;
                        int pixel_y = eye_layer.h - (y - eye_layer.y) - eye_layer.h / 2;

                        // Red indicates top
                        if (pixel == (AMASK | RMASK)) {
                            eye_top.x = pixel_x;
                            eye_top.y = pixel_y;
                        }
                        // Black indicates bottom
                        else if (pixel == (AMASK)) {
                            eye_bottom.x = pixel_x;
                            eye_bottom.y = pixel_y;
                        }
                        // No other pixels should be present
                        else {
                            SDL_assert(false);
                        }
                    }
                    if (eye_bottom.x != 0 && eye_top.x != 0)
                        goto FoundOffsets;
                }
            }
            continue;
            FoundOffsets:;

            eye_offset->x = eye_bottom.x;
            eye_offset->y = eye_bottom.y;
            eye_offset->angle = atan2f(eye_top.y - eye_bottom.y, eye_top.x - eye_bottom.x) / (float)M_PI * 180.0f;
        }
        free_image(image);
    }

    SDL_assert(rect - view->rects_for_frames < ANIMATION_MAX_FRAMES);
    SDL_assert(eye_offset - view->offsets_for_frames < ANIMATION_MAX_PERIPHERALS);
}
