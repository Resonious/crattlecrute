#if defined(DATA_TESTS) || defined(__INTELLISENSE__)
#include "data.h"
#include <string.h>

#define DATATEST_NEW_BUFFER() { .bytes = memory, .capacity = 2048, .pos = 0 }
#define EXPECT(cond) SDL_assert_always(cond); if (!(cond)) return false;

static bool test_can_write_and_read_an_unannotated_float(byte* memory) {
    float to_write = 1.205000043f;

    AbdBuffer buffer = DATATEST_NEW_BUFFER();
    abd_transfer(ABD_WRITE, ABDT_FLOAT, &buffer, &to_write, NULL);
    buffer.pos = 0;

    float to_read;
    abd_transfer(ABD_READ, ABDT_FLOAT, &buffer, &to_read, NULL);

    EXPECT(to_write == to_read);
    return true;
}

static bool test_can_write_and_read_an_annotated_float(byte* memory) {
    float to_write = 1.205000043f;

    AbdBuffer buffer = DATATEST_NEW_BUFFER();
    abd_transfer(ABD_WRITE, ABDT_FLOAT, &buffer, &to_write, "Here's my float");
    buffer.pos = 0;

    byte read_type;
    char* read_annotation;
    abd_read_field(&buffer, &read_type, &read_annotation);

    EXPECT(read_type == ABDT_FLOAT);
    EXPECT(read_annotation != NULL);
    EXPECT(str_eq(read_annotation, "Here's my float"));
    return true;
}

static bool test_can_write_and_read_a_bunch_of_stuff(byte* memory) {
    AbdBuffer buffer = DATATEST_NEW_BUFFER();
    float  f1 = 0.1f, f2 = 0.2f;
    Sint32 i1 = 1,    i2 = 2;

    abd_transfer(ABD_WRITE, ABDT_FLOAT, &buffer, &f1, "First float");
    abd_transfer(ABD_WRITE, ABDT_FLOAT, &buffer, &f2, NULL);
    abd_transfer(ABD_WRITE, ABDT_S32,   &buffer, &i1, "Now int");
    abd_transfer(ABD_WRITE, ABDT_S32,   &buffer, &i2, "Now int");

    buffer.pos = 0;
    float rf1, rf2;
    Sint32 ri1, ri2;
    abd_transfer(ABD_READ, ABDT_FLOAT, &buffer, &rf1, NULL);
    abd_transfer(ABD_READ, ABDT_FLOAT, &buffer, &rf2, NULL);
    abd_transfer(ABD_READ, ABDT_S32,   &buffer, &ri1, NULL);
    abd_transfer(ABD_READ, ABDT_S32,   &buffer, &ri2, NULL);

    EXPECT(f1 == rf1);
    EXPECT(f2 == rf2);
    EXPECT(i1 == ri1);
    EXPECT(i2 == ri2);

    return true;
}

static bool test_sections_work(byte* memory) {
    float to_write = 1.205000043f;

    AbdBuffer buffer = DATATEST_NEW_BUFFER();
    abd_transfer(ABD_WRITE, ABDT_FLOAT, &buffer, &to_write, NULL);
    abd_section(ABD_WRITE, &buffer, "Test Section");
    buffer.pos = 0;

    float to_read;
    abd_transfer(ABD_READ, ABDT_FLOAT, &buffer, &to_read, NULL);

    byte read_type;
    char* read_annotation;
    char read_sect[32];
    abd_read_field(&buffer, &read_type, &read_annotation);
    abd_read_string(&buffer, read_sect);

    EXPECT(str_eq(read_sect, "Test Section"));
    return true;
}

static bool test_inspect_works(byte* memory) {
    float fl = 1.205000043f;
    int in = 4;
    vec2 v2 = {10.5f, 0.2f};
    vec4 v4;
    v4.simd = _mm_set_ps(4.1, 3.2f, 2.3f, 1.4f);
    SDL_Color col = {255, 0, 0, 255};
    bool boo = true;
    unsigned int uin = 120000;

    AbdBuffer buffer = DATATEST_NEW_BUFFER();
    abd_transfer(ABD_WRITE, ABDT_FLOAT, &buffer, &fl, "the float");
    abd_transfer(ABD_WRITE, ABDT_S32, &buffer, &in, NULL);
    abd_section(ABD_WRITE, &buffer, "Test Section");
    abd_transfer(ABD_WRITE, ABDT_S32, &buffer, &in, "This is the same int");
    abd_transfer(ABD_WRITE, ABDT_VEC2, &buffer, &v2, "a vec2");
    abd_transfer(ABD_WRITE, ABDT_VEC4, &buffer, &v4, NULL);
    abd_transfer(ABD_WRITE, ABDT_BOOL, &buffer, &boo, "True (I think)");
    abd_transfer(ABD_WRITE, ABDT_VEC4, &buffer, &v4, NULL);
    abd_section(ABD_WRITE, &buffer, "New Types");
    abd_transfer(ABD_WRITE, ABDT_COLOR, &buffer, &col, NULL);
    abd_transfer(ABD_WRITE, ABDT_BOOL, &buffer, &boo, "True"); boo = false;
    abd_transfer(ABD_WRITE, ABDT_BOOL, &buffer, &boo, "False");
    abd_transfer(ABD_WRITE, ABDT_STRING, &buffer, "A string value", "that is a string");
    abd_transfer(ABD_WRITE, ABDT_U32, &buffer, &uin, NULL);
    buffer.capacity = buffer.pos;
    buffer.pos = 0;

    bool r = abd_inspect(&buffer, stdout);
    EXPECT(r);
    EXPECT(buffer.pos == 0);

    return true;
}

#define RUN_TEST(name) if (name(memory)) printf(#name" passed!\n"); else printf(#name" FAILED.");

void run_data_tests() {
    byte* memory = malloc(2048);
    RUN_TEST(test_can_write_and_read_an_unannotated_float);
    RUN_TEST(test_can_write_and_read_an_annotated_float);
    RUN_TEST(test_can_write_and_read_a_bunch_of_stuff);
    RUN_TEST(test_sections_work);
    RUN_TEST(test_inspect_works);
}

#endif