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
    float __declspec(align(16)) x[4];
} vec4;
typedef union vec4i {
    __m128i simd;
    int __declspec(align(16)) x[4];
    SDL_Rect rect;
} vec4i;

static __m128i _mm_mul_epi32_x4(__m128i a, __m128i b)
{
    __m128i tmp1 = _mm_mul_epu32(a,b); /* mul 2,0*/
    __m128i tmp2 = _mm_mul_epu32( _mm_srli_si128(a,4), _mm_srli_si128(b,4)); /* mul 3,1 */
    return _mm_unpacklo_epi32(_mm_shuffle_epi32(tmp1, _MM_SHUFFLE (0,0,2,0)), _mm_shuffle_epi32(tmp2, _MM_SHUFFLE (0,0,2,0))); /* shuffle results to [63..0] and pack */
}

#define SIGN_OF(x) ((0 < x) - (x < 0))
#define MOVE_TOWARDS(thing, target, by) \
    if (thing > target) { \
        thing -= by; \
        if (thing < target) \
            thing = target; \
    } \
    else if (thing < target) { \
        thing += by; \
        if (thing > target) \
            thing = target; \
    }

#endif // TYPES_H
