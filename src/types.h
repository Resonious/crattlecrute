#ifndef _TYPES_H
#define _TYPES_H

#include <xmmintrin.h>
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
#endif