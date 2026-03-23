#ifndef DASHTUP_FS_H
#define DASHTUP_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

char *fs_join2(const char *a, const char *b);
char *fs_join3(const char *a, const char *b, const char *c);
char *fs_parent_dir(const char *path);
bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
int fs_mkdir_p(const char *path, mode_t mode);
int fs_remove_tree(const char *path);
int fs_write_file(const char *path, const unsigned char *data, size_t size, mode_t mode);
int fs_read_file(const char *path, unsigned char **data, size_t *size);
int fs_move_replace(const char *from, const char *to);
int fs_remove_file_if_exists(const char *path);
int fs_chown_tree(const char *path, uid_t uid, gid_t gid);

#endif
