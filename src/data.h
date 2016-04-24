// ABD stands for Annotated Binary Data/Document (whatever).

#ifndef DATA_H
#define DATA_H

#include "types.h"

#define ABD_READ 0
#define ABD_WRITE 1

// This is interchangeable with DataChunk in game.h - with size swapped for pos.
typedef struct AbdBuffer {
    int pos;
    int capacity;
    byte* bytes;
} AbdBuffer;

#define buf_new(bbytes, bpos) (AbdBuffer){.pos=(bpos), .capacity=INT_MAX, .bytes=(bbytes)}
#define buf_new_cap(bbytes, bpos, bcap) (AbdBuffer){.pos=(bpos), .capacity=(bcap), .bytes=(bbytes)}
#define DATA_CHUNK_TO_BUF(rw, chunk, bufname, write_capacity)\
    if ((rw) == ABD_WRITE) {\
        set_data_chunk_cap((chunk), (write_capacity));\
        (chunk)->size = 0;\
    }\
    AbdBuffer* bufname = (AbdBuffer*)(chunk);\

#define ABDF_ANNOTATED (1 << 7)
#define ABD_TYPE_MASK (~(ABDF_ANNOTATED))
enum AbdType {
    ABDT_FLOAT,
    ABDT_VEC2,
    ABDT_VEC4,
    ABDT_S32,
    ABDT_S64,
    ABDT_U32,
    ABDT_U64,
    ABDT_COLOR,
    ABDT_BOOL,
    ABDT_STRING,
    ABD_TYPE_COUNT,
    ABDT_SECTION
};

static const char* abd_type_str(byte type) {
    switch (type) {
    case ABDT_FLOAT:   return "Float";
    case ABDT_VEC2:    return "Vec2";
    case ABDT_VEC4:    return "Vec4";
    case ABDT_S32:     return "Sint32";
    case ABDT_S64:     return "Sint64";
    case ABDT_U32:     return "Uint32";
    case ABDT_U64:     return "Uint64";
    case ABDT_COLOR:   return "RGB Color";
    case ABDT_BOOL:    return "Boolean";
    case ABDT_STRING:  return "String";
    case ABDT_SECTION: return "(Section)";
    default:
        return "ERROR";
    }
}

typedef void(*DataFunc)(AbdBuffer*, void*);
typedef void(*DataInspectFunc)(AbdBuffer*, byte, FILE* f);
extern DataFunc abd_data_write[];
extern DataFunc abd_data_read[];
extern DataInspectFunc abd_data_inspect[];

void abd_transfer(int rw, byte type, AbdBuffer* buf, void* data, char* write_annotation);
void abd_section(int rw, AbdBuffer* buf, char* section_label);
void abd_read_field(AbdBuffer* buf, byte* type, char** annotation);
bool abd_inspect(AbdBuffer* buf, FILE* f);

void abd_write_string(AbdBuffer* buf, void* string);
void abd_read_string(AbdBuffer* buf, void* dest);

#define data_section abd_section

#define data_float_a(rw, buf, data, write_annotation)  abd_transfer((rw), ABDT_FLOAT,  (buf), (data), (write_annotation))
#define data_vec2_a(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_VEC2,   (buf), (data), (write_annotation))
#define data_vec4_a(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_VEC4,   (buf), (data), (write_annotation))
#define data_s32_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_S32,    (buf), (data), (write_annotation))
#define data_s64_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_S64,    (buf), (data), (write_annotation))
#define data_u32_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_U32,    (buf), (data), (write_annotation))
#define data_u64_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_U64,    (buf), (data), (write_annotation))
#define data_color_a(rw, buf, data, write_annotation)  abd_transfer((rw), ABDT_COLOR,  (buf), (data), (write_annotation))
#define data_bool_a(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_BOOL,   (buf), (data), (write_annotation))
#define data_string_a(rw, buf, data, write_annotation) abd_transfer((rw), ABDT_STRING, (buf), (data), (write_annotation))

#ifdef _DEBUG
#define IDENT_ANNOTATION(ident) #ident
#else
#define IDENT_ANNOTATION(ident) NULL
#endif

#define data_float(rw, buf, data)  abd_transfer((rw), ABDT_FLOAT,  (buf), (data), IDENT_ANNOTATION(data))
#define data_vec2(rw, buf, data)   abd_transfer((rw), ABDT_VEC2,   (buf), (data), IDENT_ANNOTATION(data))
#define data_vec4(rw, buf, data)   abd_transfer((rw), ABDT_VEC4,   (buf), (data), IDENT_ANNOTATION(data))
#define data_s32(rw, buf, data)    abd_transfer((rw), ABDT_S32,    (buf), (data), IDENT_ANNOTATION(data))
#define data_s64(rw, buf, data)    abd_transfer((rw), ABDT_S64,    (buf), (data), IDENT_ANNOTATION(data))
#define data_u32(rw, buf, data)    abd_transfer((rw), ABDT_U32,    (buf), (data), IDENT_ANNOTATION(data))
#define data_u64(rw, buf, data)    abd_transfer((rw), ABDT_U64,    (buf), (data), IDENT_ANNOTATION(data))
#define data_color(rw, buf, data)  abd_transfer((rw), ABDT_COLOR,  (buf), (data), IDENT_ANNOTATION(data))
#define data_bool(rw, buf, data)   abd_transfer((rw), ABDT_BOOL,   (buf), (data), IDENT_ANNOTATION(data))
#define data_string(rw, buf, data) abd_transfer((rw), ABDT_STRING, (buf), (data), IDENT_ANNOTATION(data))

#endif
