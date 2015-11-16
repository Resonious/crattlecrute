#ifndef _TYPES_H
#define _TYPES_H

#include <xmmintrin.h>
#include <emmintrin.h>
#ifndef _WIN32
#define bool int
#define true 1
#define false 0
#endif
typedef unsigned char byte;

union vec4 {
    __m128 simd;
    float x[4];
};
union vec4i {
    __m128i simd;
    int x[4];
};
#endif