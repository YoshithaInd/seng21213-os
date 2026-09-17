#ifndef FS_H
#define FS_H
#include "../include/types.h"

typedef struct {
    char     name[28];
    uint32_t size;
} fs_dirent_t;

void     fs_init(void);
int      fs_open(const char *name, int create);   /* returns fd, or -1 */
int      fs_read(int fd, void *buf, uint32_t size);
int      fs_write(int fd, const void *buf, uint32_t size);
void     fs_close(int fd);
int      fs_unlink(const char *name);
int      fs_seek(int fd, uint32_t offset);
uint32_t fs_size(int fd);
int      fs_list(fs_dirent_t *out, int max);

#endif