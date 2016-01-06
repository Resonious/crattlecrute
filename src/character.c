#include "character.h"
#include "coords.h"
#include "game.h"

#define TERMINAL_VELOCITY 17.0f

#ifdef _DEBUG
extern bool debug_pause;
#endif

void apply_character_physics(Game* game, Character* guy, Controls* controls, float gravity, float drag) {
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
            if (just_pressed(controls, C_UP)) {
                guy->grounded = false;
                guy->jumped = true;
                guy->just_jumped = true;
                guy->dy = guy->jump_acceleration;
            }
        }
        else if (just_released(controls, C_UP) && guy->jumped) {
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
}

void update_character_animation(Character* guy) {
    guy->animation_counter += 1;

    if (guy->animation_state == GUY_WALKING) {
        if (guy->animation_counter % 5 == 0)
            guy->animation_frame += 1;
        if (guy->animation_frame >= 9)
            guy->animation_frame = 1;
    }
    else guy->animation_frame = 0;
}

void character_post_update(Character* guy) {
    guy->old_position = guy->position;
}

void draw_character(Game* game, Character* guy, CharacterView* guy_view) {
    // Play sound effects!
    if (guy->just_jumped) {
        guy_view->jump_sound->samples_pos = 0;
        game->audio.oneshot_waves[GUY_JUMP_SOUND_CHANNEL] = guy_view->jump_sound;
    }

    // DRAW GUY
    SDL_Rect src = { guy->animation_frame * 90, 0, 90, 90 };
    SDL_Rect dest = {
        guy->position.x[X] - guy->center_x - game->camera.x[X],
        game->window_height - guy->position.x[Y] - guy->center_y + game->camera.x[Y],

        90, 90
    };
    // Chearfully assume that center_y is right after center_x and aligned the same as SDL_Point...
    SDL_Point* center = (SDL_Point*)&guy->center_x;
    for (int i = 0; i < 3; i++)
        SDL_RenderCopyEx(game->renderer, guy_view->textures[i], &src, &dest, 360 - guy->ground_angle, center, guy->flip);

    // Draw sensors
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
    target->jump_acceleration = 20.0f;
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
}