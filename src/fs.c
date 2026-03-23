#include "fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

char *fs_join2(const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    bool need_slash = len_a > 0 && a[len_a - 1] != '/';
    char *out = malloc(len_a + len_b + (need_slash ? 2 : 1));
    if (!out) {
        return NULL;
    }
    strcpy(out, a);
    if (need_slash) {
        strcat(out, "/");
    }
    strcat(out, b);
    return out;
}

char *fs_join3(const char *a, const char *b, const char *c) {
    char *tmp = fs_join2(a, b);
    char *out;
    if (!tmp) {
        return NULL;
    }
    out = fs_join2(tmp, c);
    free(tmp);
    return out;
}

char *fs_parent_dir(const char *path) {
    char *copy = strdup(path);
    char *slash;
    if (!copy) {
        return NULL;
    }

    slash = strrchr(copy, '/');
    if (!slash) {
        copy[0] = '.';
        copy[1] = '\0';
        return copy;
    }

    if (slash == copy) {
        slash[1] = '\0';
        return copy;
    }

    *slash = '\0';
    return copy;
}

bool fs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool fs_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int fs_mkdir_p(const char *path, mode_t mode) {
    char *copy;
    size_t len;

    if (!path || path[0] == '\0') {
        return 0;
    }

    copy = strdup(path);
    if (!copy) {
        errno = ENOMEM;
        return -1;
    }

    len = strlen(copy);
    if (len > 1 && copy[len - 1] == '/') {
        copy[len - 1] = '\0';
    }

    for (char *p = copy + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(copy, mode) != 0 && errno != EEXIST) {
                free(copy);
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(copy, mode) != 0 && errno != EEXIST) {
        free(copy);
        return -1;
    }

    free(copy);
    return 0;
}

static int fs_remove_tree_internal(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        struct dirent *entry;
        if (!dir) {
            return -1;
        }

        while ((entry = readdir(dir)) != NULL) {
            char *child;
            int rc;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            child = fs_join2(path, entry->d_name);
            if (!child) {
                closedir(dir);
                errno = ENOMEM;
                return -1;
            }
            rc = fs_remove_tree_internal(child);
            free(child);
            if (rc != 0) {
                closedir(dir);
                return -1;
            }
        }

        closedir(dir);
        return rmdir(path);
    }

    return unlink(path);
}

int fs_remove_tree(const char *path) {
    return fs_remove_tree_internal(path);
}

int fs_write_file(const char *path, const unsigned char *data, size_t size, mode_t mode) {
    char *parent = fs_parent_dir(path);
    int fd;
    size_t written = 0;

    if (!parent) {
        errno = ENOMEM;
        return -1;
    }

    if (fs_mkdir_p(parent, 0755) != 0) {
        free(parent);
        return -1;
    }
    free(parent);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        return -1;
    }

    while (written < size) {
        ssize_t rc = write(fd, data + written, size - written);
        if (rc < 0) {
            close(fd);
            return -1;
        }
        written += (size_t)rc;
    }

    if (fchmod(fd, mode) != 0) {
        close(fd);
        return -1;
    }

    if (close(fd) != 0) {
        return -1;
    }

    return 0;
}

int fs_read_file(const char *path, unsigned char **data, size_t *size) {
    struct stat st;
    FILE *fp;
    unsigned char *buffer;

    if (stat(path, &st) != 0) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    buffer = malloc((size_t)st.st_size);
    if (!buffer) {
        fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    if (st.st_size > 0 && fread(buffer, 1, (size_t)st.st_size, fp) != (size_t)st.st_size) {
        free(buffer);
        fclose(fp);
        errno = EIO;
        return -1;
    }

    fclose(fp);
    *data = buffer;
    *size = (size_t)st.st_size;
    return 0;
}

int fs_move_replace(const char *from, const char *to) {
    if (rename(from, to) == 0) {
        return 0;
    }

    if (errno == EXDEV) {
        unsigned char *data = NULL;
        size_t size = 0;
        struct stat st;
        if (stat(from, &st) != 0) {
            return -1;
        }
        if (fs_read_file(from, &data, &size) != 0) {
            return -1;
        }
        if (fs_write_file(to, data, size, st.st_mode & 0777) != 0) {
            free(data);
            return -1;
        }
        free(data);
        return unlink(from);
    }

    return -1;
}

int fs_remove_file_if_exists(const char *path) {
    if (unlink(path) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    return 0;
}


static int fs_chown_tree_internal(const char *path, uid_t uid, gid_t gid) {
    struct stat st;

    if (lstat(path, &st) != 0) {
        return -1;
    }

    if (S_ISLNK(st.st_mode)) {
        return 0;
    }

    if (chown(path, uid, gid) != 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        struct dirent *entry;

        if (!dir) {
            return -1;
        }

        while ((entry = readdir(dir)) != NULL) {
            char *child;
            int rc;

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            child = fs_join2(path, entry->d_name);
            if (!child) {
                closedir(dir);
                errno = ENOMEM;
                return -1;
            }

            rc = fs_chown_tree_internal(child, uid, gid);
            free(child);
            if (rc != 0) {
                closedir(dir);
                return -1;
            }
        }

        closedir(dir);
    }

    return 0;
}

int fs_chown_tree(const char *path, uid_t uid, gid_t gid) {
    return fs_chown_tree_internal(path, uid, gid);
}
