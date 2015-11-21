#ifndef TYPES_H
#define TYPES_H

#include <xmmintrin.h>
#include <emmintrin.h>
#include "SDL.h"
#ifndef _WIN32
#define bool int
#define true 1
#define false 0
#define min(x,y) (x < y ? x : y)
#define max(x,y) (x > y ? x : y)
#endif
typedef unsigned char byte;

extern Uint64 ticks_per_second;

#ifdef _DEBUG
#define BENCH_START(var) Uint64 var ## _before = SDL_GetPerformanceCounter();
// Break after this and check out <var>_seconds to see elapsed time.
#define BENCH_END(var) \
    Uint64 var ## _after = SDL_GetPerformanceCounter();\
    Uint64 var ## _ticks = var ## _after - var ## _before;\
    double var ## _seconds = (double)var ## _ticks / (double)ticks_per_second;
#else
#define BENCH_START(x)
#define BENCH_END(x)
#endif

typedef union vec4 {
    __m128 simd;
    float x[4];
} vec4;
typedef union vec4i {
    __m128i simd;
    int x[4];
    SDL_Rect rect;
} vec4i;
#endif
