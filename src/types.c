#include "types.h"
#include <stdlib.h>
#include <math.h>

void wait_for_then_use_lock(SDL_mutex* mutex) {
    /*
    while (SDL_AtomicGet(lock)) {}
    SDL_AtomicSet(lock, true);
    */
    if (SDL_LockMutex(mutex) != 0)
        exit(137);
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