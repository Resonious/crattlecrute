#ifndef _TYPES_H
#define _TYPES_H

#include <xmmintrin.h>
#include <emmintrin.h>
#ifndef _WIN32
#define bool int
#define true 1
#define false 0
#define min(x,y) (x < y ? x : y)
#define max(x,y) (x > y ? x : y)
#endif
typedef unsigned char byte;

#define BENCH_START Uint64 before = SDL_GetPerformanceCounter();
#define BENCH_END \
    Uint64 after = SDL_GetPerformanceCounter();\
    Uint64 ticks = after - before;\
    double seconds = (double)ticks / (double)SDL_GetPerformanceFrequency();\
    DebugBreak();

union vec4 {
    __m128 simd;
    float x[4];
};
union vec4i {
    __m128i simd;
    int x[4];
};
#endif
