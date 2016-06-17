#include "data.h"
#include "character.h"
#include <inttypes.h>

#define abd_write_field_header(buf, type, annotation) (buf)->bytes[(buf)->pos++] = (type) | ((annotation) ? ABDF_ANNOTATED : 0);

void write_1_byte(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 1);
    buf->pos += 1;
}

void read_1_byte(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 1);
    buf->pos += 1;
}

void write_4_bytes(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 4);
    buf->pos += 4;
}

void read_4_bytes(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 4);
    buf->pos += 4;
}

void write_8_bytes(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 8);
    buf->pos += 8;
}

void read_8_bytes(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 8);
    buf->pos += 8;
}

void write_16_bytes(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 16);
    buf->pos += 16;
}

void read_16_bytes(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 16);
    buf->pos += 16;
}

void write_4_floats(AbdBuffer* buf, void* data) {
    SDL_assert((size_t)data % 16 == 0);
    float* v4       = (float*)data;
    float* buf_dest = (float*)buf->bytes + buf->pos;
    buf_dest[0] = v4[0];
    buf_dest[1] = v4[1];
    buf_dest[2] = v4[2];
    buf_dest[3] = v4[3];
    buf->pos += 16;
}

void read_4_floats(AbdBuffer* buf, void* data) {
    SDL_assert((size_t)data % 16 == 0);
    float* v4      = (float*)data;
    float* buf_src = (float*)buf->bytes + buf->pos;
    v4[0] = buf_src[0];
    v4[1] = buf_src[1];
    v4[2] = buf_src[2];
    v4[3] = buf_src[3];
    buf->pos += 16;
}

void abd_write_vec4(AbdBuffer* buf, void* vdata) {
    __m128* data = (__m128*)vdata;
    _mm_storeu_ps((float*)(buf->bytes + buf->pos), *data);
    buf->pos += 16;
}

void abd_read_vec4(AbdBuffer* buf, void* vdest) {
    __m128* dest = (__m128*)vdest;
    *dest = _mm_loadu_ps((float*)(buf->bytes + buf->pos));
    buf->pos += 16;
}

void abd_write_string(AbdBuffer* buf, void* str) {
    char* string = (char*)str;
    size_t str_size = strlen(string);
    if (str_size >= 256) {
        memcpy(buf->bytes + buf->pos, string, 256);
        buf->bytes[buf->pos + 256] = '\0';
        buf->pos += 256;
    }
    else {
        size_t actual_size = str_size + 1;
        byte bsize = buf->bytes[buf->pos++] = (byte)actual_size;
        memcpy(buf->bytes + buf->pos, string, actual_size);
        buf->pos += bsize;
    }
}

void abd_read_string(AbdBuffer* buf, void* dest) {
    byte length = buf->bytes[buf->pos++];
    if (dest)
        memcpy(dest, buf->bytes + buf->pos, length);
    buf->pos += length;
}

void inspect_float(AbdBuffer* buf, byte type, FILE* f) {
    float fl;
    abd_data_read[type](buf, &fl);
    fprintf(f, "%f", fl);
}

void inspect_integer_type(AbdBuffer* buf, byte type, FILE* f) {
    Sint64 i = 0;
    abd_data_read[type](buf, &i);
    fprintf(f, "%"PRIi64, i);
}

void inspect_unsigned_integer_type(AbdBuffer* buf, byte type, FILE* f) {
    Uint64 i = 0;
    abd_data_read[type](buf, &i);
    fprintf(f, "%"PRIu64, i);
}

void inspect_vec2(AbdBuffer* buf, byte type, FILE* f) {
    vec2 v;
    abd_data_read[type](buf, &v);
    fprintf(f, "(%f, %f)", v.x, v.y);
}

void inspect_vec4(AbdBuffer* buf, byte type, FILE* f) {
    vec4 v;
    abd_data_read[type](buf, &v);
    fprintf(f, "(%f, %f, %f, %f)", v.x[0], v.x[1], v.x[2], v.x[3]);
}

void inspect_color(AbdBuffer* buf, byte type, FILE* f) {
    SDL_Color c;
    abd_data_read[type](buf, &c);
    fprintf(f, "#%06x", *(Uint32*)(&c));
}

void inspect_bool(AbdBuffer* buf, byte type, FILE* f) {
    bool b;
    abd_data_read[type](buf, &b);
    if (b)
        fprintf(f, "true");
    else
        fprintf(f, "false");
}

void inspect_string(AbdBuffer* buf, byte type, FILE* f) {
    char string[256];
    abd_read_string(buf, string);
    fprintf(f, "\"%s\"", string);
}

DataFunc abd_data_write[] = {
    write_4_bytes,   // ABDT_FLOAT
    write_8_bytes,   // ABDT_VEC2
    abd_write_vec4,  // ABDT_VEC4
    write_4_bytes,   // ABDT_S32
    write_8_bytes,   // ABDT_S64
    write_4_bytes,   // ABDT_U32
    write_8_bytes,   // ABDT_U64
    write_4_bytes,   // ABDT_COLOR
    write_1_byte,    // ABDT_BOOL
    abd_write_string // ABDT_STRING
};

DataFunc abd_data_read[] = {
    read_4_bytes,   // ABDT_FLOAT
    read_8_bytes,   // ABDT_VEC2
    abd_read_vec4,  // ABDT_VEC4
    read_4_bytes,   // ABDT_S32
    read_8_bytes,   // ABDT_S64
    read_4_bytes,   // ABDT_U32
    read_8_bytes,   // ABDT_U64
    read_4_bytes,   // ABDT_COLOR
    read_1_byte,    // ABDT_BOOL
    abd_read_string // ABDT_STRING
};

DataInspectFunc abd_data_inspect[] = {
    inspect_float,        // ABDT_FLOAT
    inspect_vec2,         // ABDT_VEC2
    inspect_vec4,         // ABDT_VEC4
    inspect_integer_type, // ABDT_S32
    inspect_integer_type, // ABDT_S64
    inspect_unsigned_integer_type, // ABDT_U32
    inspect_unsigned_integer_type, // ABDT_U64
    inspect_color,        // ABDT_COLOR
    inspect_bool,         // ABDT_BOOL
    inspect_string        // ABDT_STRING
};

void abd_section(int rw, AbdBuffer* buf, char* section_label) {
    switch (rw) {
    case ABD_READ: {
        byte read_type;
        abd_read_field(buf, &read_type, NULL);
        SDL_assert(read_type == ABDT_SECTION);
        abd_read_string(buf, NULL);

    } break;

    case ABD_WRITE: {
        SDL_assert(section_label != NULL);
        abd_write_field_header(buf, ABDT_SECTION, false);
        abd_write_string(buf, section_label);

    } break;
    }
}

void abd_transfer(int rw, byte type, AbdBuffer* buf, void* data, char* write_annotation) {
    SDL_assert(type < ABD_TYPE_COUNT);

    switch (rw) {
    case ABD_READ: {
        byte read_type;
        abd_read_field(buf, &read_type, NULL);
        SDL_assert(read_type != ABDT_SECTION);
        SDL_assert(read_type == type);

        abd_data_read[type](buf, data);
    } break;

    case ABD_WRITE: {
        abd_write_field_header(buf, type, write_annotation);
        if (write_annotation) abd_write_string(buf, write_annotation);

        abd_data_write[type](buf, data);
    } break;
    }
}

void abd_read_field(AbdBuffer* buf, byte* type, char** annotation) {
    byte head = buf->bytes[buf->pos++];
    byte read_type = head & ABD_TYPE_MASK;

    if (head & ABDF_ANNOTATED) {
        byte annotation_length = buf->bytes[buf->pos++];
        if (annotation)
            *annotation = buf->bytes + buf->pos;
        buf->pos += annotation_length;
    }
    else if (annotation)
        *annotation = NULL;

    *type = read_type;
}

bool abd_inspect(AbdBuffer* buf, FILE* f) {
    int r = true;
    int old_pos = buf->pos;
    int limit;
    if (old_pos)
        limit = old_pos;
    else
        limit = buf->capacity;

    buf->pos = 0;

    while (buf->pos < limit) {
        byte type;
        char* annotation;
        abd_read_field(buf, &type, &annotation);

        if (type != ABDT_SECTION)
            fprintf(f, "%s: ", abd_type_str(type));

        if (type < ABD_TYPE_COUNT)
            abd_data_inspect[type](buf, type, f);
        else if (type == ABDT_SECTION) {
            char section_text[512];
            abd_read_string(buf, section_text);
            fprintf(f, "==== %s ====", section_text);
        }
        else {
            fprintf(f, "Cannot inspect type: %i\nExiting inspection.\n", type);
            r = false;
            goto Done;
        }

        if (annotation) {
            fprintf(f, " -- \"%s\"", annotation);
        }
        fprintf(f, "\n");
    }

    Done:
    buf->pos = old_pos;
    return r;
}
