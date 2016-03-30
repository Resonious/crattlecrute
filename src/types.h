#ifndef TYPES_H
#define TYPES_H

#include <xmmintrin.h>
#include <emmintrin.h>

#ifdef __FreeBSD__
#include "SDL2/SDL.h"
#else
#include "SDL.h"
#endif

#ifndef _WIN32
#define bool unsigned char
#define true 1
#define false 0
#define min(x,y) ((x) < (y) ? (x) : (y))
#define max(x,y) ((x) > (y) ? (x) : (y))

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#endif

#include <math.h>

#ifdef _WIN32
#define DIR_SEP "\\"
#else
#define DIR_SEP "/"
#endif
#define IS_WHITESPACE(c) (c == ' ' || c == '\n' || c == '\t')

#define SECONDS * 60
#define SECOND * 60
#define MINUTES * 3600
#define MINUTE * 3600
#define HOURS * 216000
#define HOUR * 216000

#define CHARACTER_NAME_LENGTH 32
#define MAX_CHARACTERS 10

typedef unsigned char byte;
typedef struct TextureDimensions {
    SDL_Texture* tex;
    int width, height;
} TextureDimensions;

extern Uint64 ticks_per_second;

#ifdef _DEBUG
#include <mruby.h>
#include <mruby/hash.h>
extern mrb_state* _bench_mrb;
extern mrb_value _bench_hash;
#define BENCH_START(var) Uint64 var ## _before = SDL_GetPerformanceCounter();
// Break after this and check out <var>_seconds to see elapsed time.
#define BENCH_END(var) \
    Uint64 var ## _after = SDL_GetPerformanceCounter();\
    Uint64 var ## _ticks = var ## _after - var ## _before;\
    double var ## _seconds = (double)var ## _ticks / (double)ticks_per_second;\
    if (_bench_mrb && !mrb_nil_p(_bench_hash))\
      mrb_hash_set(_bench_mrb, _bench_hash, mrb_symbol_value(mrb_intern_lit(_bench_mrb, #var)), mrb_float_value(_bench_mrb, var ## _seconds))
#else
#define BENCH_START(x)
#define BENCH_END(x)
#endif

#ifdef _WIN32
static void* aligned_malloc(size_t size) { return _aligned_malloc(size, 16); }
#define aligned_free _aligned_free
#else
static void* aligned_malloc(size_t size) {
    void* ptr;
    int result = posix_memalign(&ptr, 16, size);
    SDL_assert(result == 0);
    return ptr;
}
#define aligned_free free
#endif

#define ALIGN_16

void wait_for_then_use_lock(SDL_mutex* mutex);
void write_to_buffer(byte* buffer, void* src, int* pos, int size);
void read_from_buffer(byte* buffer, void* dest, int* pos, int size);

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

#define SIGN_OF(x) ((0 < (x)) - ((x) < 0))
#define MOVE_TOWARDS(thing, target, by) \
    if ((thing) > (target)) { \
        thing -= by; \
        if ((thing) < (target)) \
            (thing) = (target); \
    } \
    else if ((thing) < (target)) { \
        (thing) += (by); \
        if ((thing) > (target)) \
            (thing) = (target); \
    }

typedef union vec2i {
    struct { int x, y; };
    int v[2];
} vec2i;

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
float v2_dot(vec2* u, vec2* v);
float v2_magnitude(vec2* u);
vec2 v2_add(vec2 u, vec2 v);
vec2 v2_sub(vec2 u, vec2 v);
vec2 v2_mul(float s, vec2 u);
void v2_addeq(vec2* u, vec2 v);
#define v2_add_to v2_addeq

typedef struct GenericBody {
    // (x[0] left to right, x[1] down to up)
    vec4 position;
    // (x[0] left to right, x[1] down to up)
    vec4 old_position;

    // (x[0], x[1])  (x[2], x[3])
    vec4i top_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i bottom_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i left_sensors;
    // (x[0], x[1])  (x[2], x[3])
    vec4i right_sensors;

    bool left_hit, right_hit, grounded, hit_ceiling, hit_wall;
    // In degrees
    float ground_angle;
} GenericBody;

struct Map;
struct MobCommon;
typedef void(*Callback)(void*);
typedef void(*DataCallback)(void*, void*);
typedef void(*SetItemFunc)(void*, struct Character*, struct Game*, int, int);
typedef void(*SpawnMobFunc)(void*, struct Map*, struct Game*, int, vec2, void*, DataCallback);
typedef void(*DespawnMobFunc)(void*, struct Map*, struct Game*, struct MobCommon*);

#endif // TYPES_H
