#include "data.h"
#include "character.h"

#define abd_write_field_header(buf, type, annotation) (buf)->bytes[(buf)->pos++] = (type) | ((annotation) ? ABDF_ANNOTATED : 0);

void abd_write_annotation(AbdBuffer* buf, char* annotation) {
    if (annotation == NULL)
        return;

    size_t str_size = strlen(annotation);
    if (str_size >= 256) {
        memcpy(buf->bytes + buf->pos, annotation, 256);
        buf->bytes[buf->pos + 256] = '\0';
        buf->pos += 256;
    }
    else {
        size_t actual_size = str_size + 1;
        buf->bytes[buf->pos++] = actual_size;
        memcpy(buf->bytes + buf->pos, annotation, actual_size);
        buf->pos += actual_size;
    }
}

void write_4_bytes(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 4);
    buf->pos += 4;
}

void read_4_bytes(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 4);
    buf->pos += 4;
}

DataFunc abd_data_write[] = {
    write_4_bytes, // ABDT_FLOAT
    write_4_bytes  // ABDT_S32
};

DataFunc abd_data_read[] = {
    read_4_bytes, // ABDT_FLOAT
    read_4_bytes  // ABDT_S32
};

void abd_transfer(int rw, byte type, AbdBuffer* buf, void* data, char* write_annotation) {
    SDL_assert(type < ABD_TYPE_COUNT);

    switch (rw) {
    case ABD_READ: {
        byte read_type;
        abd_read_field(buf, &read_type, NULL);

        abd_data_read[type](buf, data);
    } break;

    case ABD_WRITE: {
        abd_write_field_header(buf, type, write_annotation);
        if (write_annotation) abd_write_annotation(buf, write_annotation);

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
