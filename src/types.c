#include "types.h"
#include <stdlib.h>
#include <math.h>
#include "assets.h"

void wait_for_then_use_lock(SDL_mutex* mutex) {
    if (SDL_LockMutex(mutex) != 0)
        printf("ERROR MUTEX DIDN'T LOCK!");
}

void write_to_buffer(byte* buffer, void* src, int* pos, int size) {
    SDL_assert(size >= 0);
    memcpy(buffer + *pos, src, size);
    *pos += size;
}

void read_from_buffer(byte* buffer, void* dest, int* pos, int size) {
    memcpy(dest, buffer + *pos, size);
    *pos += size;
}

mat22 rotation_mat22(float angle) {
    float rads = angle * (float)M_PI / 180;
    mat22 r = {
        cosf(rads), -sinf(rads),
        sinf(rads),  cosf(rads)
    };
    return r;
}

vec2 mat_mul_22(mat22* mat, vec2* vec) {
    vec2 r = {
        mat->a11 * vec->x + mat->a12 * vec->y,
        mat->a21 * vec->x + mat->a22 * vec->y
    };
    return r;
}

float v2_dot(vec2* u, vec2* v) {
    return u->x * v->x + u->y * v->y;
}

float v2_magnitude(vec2* u) {
    return sqrtf(u->x * u->x + u->y * u->y);
}

void v2_addeq(vec2* u, vec2 v) {
    u->x += v.x;
    u->y += v.y;
}

vec2 v2_add(vec2 u, vec2 v) {
    return (vec2) {
        u.x + v.x,
        u.y + v.y
    };
}

vec2 v2_sub(vec2 u, vec2 v) {
    return (vec2) {
        u.x - v.x,
        u.y - v.y
    };
}

vec2 v2_mul(float s, vec2 u) {
    return (vec2) {
        u.x * s,
        u.y * s
    };
}

SDL_Color hue_to_color(float hue) {
    SDL_assert(hue < 360.0f);
    const float th = tanf_360[(int)hue];
    const float r3 = SQRT3;

    vec4 fcolor; fcolor.simd = _mm_set1_ps(0);

    if (hue >= 0 && hue <= 60.0f) {
        fcolor.x[0] = 1.0f;
        fcolor.x[1] = (hue - 0.0f) / 60.0f;
        fcolor.x[2] = 0.0f;
    }
    else if (hue >= 60.0f && hue <= 120.0f) {
        fcolor.x[0] = (60.0f - (hue - 60.0f)) / 60.0f;
        fcolor.x[1] = 1.0f;
        fcolor.x[2] = 0.0f;
    }
    else if (hue >= 120.0f && hue <= 180.0f) {
        fcolor.x[0] = 0.0f;
        fcolor.x[1] = 1.0f;
        fcolor.x[2] = (hue - 120.0f) / 60.0f;
    }
    else if (hue >= 180.0f && hue <= 240.0f) {
        fcolor.x[0] = 0.0f;
        fcolor.x[1] = (60.0f - (hue - 180.0f)) / 60.0f;
        fcolor.x[2] = 1.0f;
    }
    else if (hue >= 240.0f && hue <= 300.0f) {
        fcolor.x[0] = (hue - 240.0f) / 60.0f;
        fcolor.x[1] = 0.0f;
        fcolor.x[2] = 1.0f;
    }
    else if (hue >= 300.0f && hue < 360.0f) {
        fcolor.x[0] = 1.0f;
        fcolor.x[1] = 0.0f;
        fcolor.x[2] = (60.0f - (hue - 300.0f)) / 60.0f;
    }
    fcolor.x[3] = 1.0f;
    
    SDL_assert(fcolor.x[0] <= 1.0f);
    SDL_assert(fcolor.x[1] <= 1.0f);
    SDL_assert(fcolor.x[2] <= 1.0f);
    SDL_assert(fcolor.x[3] <= 1.0f);

    vec4i icolor;
    icolor.simd = _mm_cvtps_epi32(_mm_mul_ps(fcolor.simd, _mm_set1_ps(255.0f)));
    return (SDL_Color) { icolor.x[0], icolor.x[1], icolor.x[2], icolor.x[3] };
}
float color_to_hue(SDL_Color color) {
    return wrap_degrees((atan2f(sqrtf(3) * (color.g - color.b), 2 * color.r - color.g - color.b)) * 180.0f / M_PI);
}

float wrap_degrees(float deg) {
    while (deg < 0)
        deg += 360.0f;
    while (deg >= 360.0f)
        deg -= 360.0f;
    return deg;
}

bool str_eq(char* s1, char* s2) {
    size_t i = 0;
    while (true) {
        if (s1[i] != s2[i])
            return false;

        if (s1[i] == 0)
            break;

        i += 1;
    }
    return true;
}

void push_generic_bodies(GenericBody* pusher, GenericBody* pushee, float pstr) {
    vec4 dist;
    dist.simd = _mm_sub_ps(pushee->position.simd, pusher->position.simd);

    float pusher_approx_size = fabsf(pusher->left_sensors.x[S2X] - pusher->right_sensors.x[S2X]);
    if (fabsf(dist.x[X]) > pusher_approx_size)
        return;

    float square_length = powf(dist.x[0], 2) + powf(dist.x[1], 2);
    float angle = atan2f(dist.x[1], dist.x[0]);
    float push_str = pstr / square_length;

    if (push_str < 0.05f)
        return;

    vec4 push;
    push.x[0] = cosf(angle) * push_str;
    push.x[1] = sinf(angle) * push_str;

    pushee->push_velocity.simd = _mm_add_ps(pushee->push_velocity.simd, push.simd);
    if (fabsf(pushee->push_velocity.x[0]) >= 5.0f)
        pushee->push_velocity.x[0] = 5.0f * SIGN_OF(pushee->push_velocity.x[0]);
    if (fabsf(pushee->push_velocity.x[1]) >= 5.0f)
        pushee->push_velocity.x[1] = 5.0f * SIGN_OF(pushee->push_velocity.x[1]);
}

void apply_push_velocity(GenericBody* body) {
    body->position.simd = _mm_add_ps(body->position.simd, body->push_velocity.simd);
    MOVE_TOWARDS(body->push_velocity.x[0], 0, 0.3f);
    MOVE_TOWARDS(body->push_velocity.x[1], 0, 0.3f);
}
