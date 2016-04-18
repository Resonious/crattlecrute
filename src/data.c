#include "data.h"
#include "character.h"

#define abd_write_field_header(buf, type, annotation) (buf)->bytes[(buf)->pos++] = (type) | ((annotation) ? ABDF_ANNOTATED : 0);

void abd_write_string(AbdBuffer* buf, char* string) {
    size_t str_size = strlen(string);
    if (str_size >= 256) {
        memcpy(buf->bytes + buf->pos, string, 256);
        buf->bytes[buf->pos + 256] = '\0';
        buf->pos += 256;
    }
    else {
        size_t actual_size = str_size + 1;
        buf->bytes[buf->pos++] = actual_size;
        memcpy(buf->bytes + buf->pos, string, actual_size);
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

void abd_read_string(AbdBuffer* buf, void* dest) {
    byte length = buf->bytes[buf->pos++];
    if (dest)
        memcpy(dest, buf->bytes + buf->pos, length);
    buf->pos += length;
}

void inspect_float(AbdBuffer* buf, byte type) {
    float f;
    abd_data_read[type](buf, &f);
    printf("%f", f);
}

void inspect_integer_type(AbdBuffer* buf, byte type) {
    Sint64 i = 0;
    abd_data_read[type](buf, &i);
    printf("%i", i);
}

DataFunc abd_data_write[] = {
    write_4_bytes, // ABDT_FLOAT
    write_4_bytes  // ABDT_S32
};

DataFunc abd_data_read[] = {
    read_4_bytes, // ABDT_FLOAT
    read_4_bytes  // ABDT_S32
};

DataInspectFunc abd_data_inspect[] = {
    inspect_float,       // ABDT_FLOAT
    inspect_integer_type // ABDT_S32
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

bool abd_inspect(AbdBuffer* buf) {
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
            printf("%s: ", abd_type_str(type));

        if (type < ABD_TYPE_COUNT)
            abd_data_inspect[type](buf, type);
        else if (type == ABDT_SECTION) {
            char section_text[512];
            abd_read_string(buf, section_text);
            printf("==== %s ====", section_text);
        }
        else {
            printf("Cannot inspect type: %i\nExiting inspection.\n", type);
            r = false;
            goto Done;
        }

        if (annotation) {
            printf(" -- \"%s\"", annotation);
        }
        printf("\n");
    }

    Done:
    buf->pos = old_pos;
    return r;
}
