#ifndef DATA_H
#define DATA_H

#include "types.h"

#define ABD_READ 0
#define ABD_WRITE 1

typedef struct AbdBuffer {
    int pos;
    int capacity;
    byte* bytes;
} AbdBuffer;

#define ABDF_ANNOTATED (1 << 7)
#define ABD_TYPE_MASK (~(ABDF_ANNOTATED))
enum AbdType {
    ABDT_FLOAT,
    ABDT_S32,
    ABD_TYPE_COUNT,
    ABDT_SECTION
};

typedef void(*DataFunc)(AbdBuffer*, void*);
extern DataFunc abd_data_write[];
extern DataFunc abd_data_read[];

void abd_transfer(int rw, byte type, AbdBuffer* buf, void* data, char* write_annotation);
void abd_section(int rw, AbdBuffer* buf, char* section_label);
void abd_read_field(AbdBuffer* buf, byte* type, char** annotation);

void abd_write_string(AbdBuffer* buf, char* string);
void abd_read_string(AbdBuffer* buf, void* dest);

#define abd_transfer_float(rw, buf, data, write_annotation) abd_transfer((rw), ABDT_FLOAT, (buf), (data), (write_annotation))
#define abd_transfer_s32(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_S32, (buf), (data), (write_annotation))

#endif
