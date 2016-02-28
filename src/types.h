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

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#endif

#include <math.h>

#define IS_WHITESPACE(c) (c == ' ' || c == '\n' || c == '\t')

typedef unsigned char byte;
typedef struct TextureDimensions {
    SDL_Texture* tex;
    int width, height;
} TextureDimensions;

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

#define aligned_malloc(size) malloc(size)
#define aligned_free(ptr) free(ptr)

#ifdef _WIN32
#define ALIGN_16 __declspec(align(16))

#ifdef X86
#define aligned_malloc(size) _aligned_malloc(size, 16)
#define aligned_free(ptr) _aligned_free(ptr)
#endif

#else
#define ALIGN_16
#endif

typedef union vec4 {
    __m128 simd;
    float ALIGN_16 x[4];
} vec4;
typedef union vec4i {
    __m128i simd;
    int ALIGN_16 x[4];
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

typedef union vec2 {
    struct { float x, y; };
    float v[2];
} vec2;

typedef union mat22 {
    struct { float a11, a12,
                   a21, a22; };
    float a[4];
} mat22;

mat22 rotation_mat22(float angle);
vec2 mat_mul_22(mat22* mat, vec2* vec);
float dot(vec2* u, vec2* v);
float magnitude(vec2* u);

#endif // TYPES_H
