#ifndef DASHTUP_TAR_READER_H
#define DASHTUP_TAR_READER_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t size;
    mode_t mode;
    char typeflag;
} tar_entry_t;

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t offset;
} tar_reader_t;

void tar_reader_init(tar_reader_t *reader, const unsigned char *data, size_t size);
bool tar_reader_next(tar_reader_t *reader, tar_entry_t *entry);

#endif
