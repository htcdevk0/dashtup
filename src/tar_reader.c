#include "tar_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static unsigned long tar_octal_to_ulong(const char *text, size_t width) {
    unsigned long value = 0;
    for (size_t i = 0; i < width; ++i) {
        if (text[i] == '\0' || text[i] == ' ') {
            break;
        }
        if (text[i] >= '0' && text[i] <= '7') {
            value = (value << 3) + (unsigned long)(text[i] - '0');
        }
    }
    return value;
}

static int tar_is_zero_block(const unsigned char *block) {
    for (size_t i = 0; i < 512; ++i) {
        if (block[i] != 0) {
            return 0;
        }
    }
    return 1;
}

void tar_reader_init(tar_reader_t *reader, const unsigned char *data, size_t size) {
    reader->data = data;
    reader->size = size;
    reader->offset = 0;
}

bool tar_reader_next(tar_reader_t *reader, tar_entry_t *entry) {
    while (reader->offset + 512 <= reader->size) {
        const struct tar_header *hdr = (const struct tar_header *)(reader->data + reader->offset);
        unsigned long file_size;
        size_t payload_offset;
        size_t padded_size;
        static char full_name[256];

        if (tar_is_zero_block((const unsigned char *)hdr)) {
            return false;
        }

        full_name[0] = '\0';
        if (hdr->prefix[0] != '\0') {
            snprintf(full_name, sizeof(full_name), "%.*s/%.*s",
                     (int)sizeof(hdr->prefix), hdr->prefix,
                     (int)sizeof(hdr->name), hdr->name);
        } else {
            snprintf(full_name, sizeof(full_name), "%.*s",
                     (int)sizeof(hdr->name), hdr->name);
        }

        file_size = tar_octal_to_ulong(hdr->size, sizeof(hdr->size));
        payload_offset = reader->offset + 512;
        padded_size = ((size_t)file_size + 511u) & ~511u;
        reader->offset = payload_offset + padded_size;

        entry->name = full_name;
        entry->data = reader->data + payload_offset;
        entry->size = (size_t)file_size;
        entry->mode = (mode_t)(tar_octal_to_ulong(hdr->mode, sizeof(hdr->mode)) & 0777u);
        entry->typeflag = hdr->typeflag == '\0' ? '0' : hdr->typeflag;
        return true;
    }

    return false;
}
