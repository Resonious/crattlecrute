#include "types.h"
#include <math.h>

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

float dot(vec2* u, vec2* v) {
    return u->x * v->x + u->y * v->y;
}

float magnitude(vec2* u) {
    return sqrtf(u->x * u->x + u->y * u->y);
}