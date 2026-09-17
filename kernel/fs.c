#include "fs.h"
#include "ramdisk.h"

/* ---------------------------------------------------------------------------
 * On-disk layout (L12 §1-§2)
 * --------------------------------------------------------------------------*/
#define FS_MAGIC              0x53454E47u   /* 'SENG' */
#define FS_SB_BLOCK           0
#define FS_DIR_BLOCK          1
#define FS_BLOCK_BITMAP_BLK   2
#define FS_INODE_BITMAP_BLK   3
#define FS_INODE_TABLE_BLK    4
#define FS_DATA_START_BLK     5

#define FS_MAX_INODES         32
#define FS_MAX_FILES          128   /* 4096 / 32 bytes per dirent */
#define FS_MAX_OPEN           8
#define FS_DIRECT_BLOCKS      8     /* 8 * 4KB = 32 KB max file size */

typedef struct {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t total_inodes;
} superblock_t;

typedef struct {
    uint32_t used;
    uint32_t size;
    uint32_t blocks[FS_DIRECT_BLOCKS];
} inode_t;

typedef struct {
    char    name[28];
    int32_t inode;   /* -1 = free slot */
} dirent_raw_t;

/* Unions pad these structs out to exactly one block, so read/write can
 * always move a whole block without touching memory past the array. */
typedef union { superblock_t sb;  uint8_t raw[RD_BLOCK_SIZE]; } sb_block_t;
typedef union { inode_t entries[FS_MAX_INODES]; uint8_t raw[RD_BLOCK_SIZE]; } inode_table_t;

/* ---------------------------------------------------------------------------
 * In-memory mirror of the metadata (kept in sync via fs_sync())
 * --------------------------------------------------------------------------*/
static sb_block_t     sbblk;
static dirent_raw_t   directory[FS_MAX_FILES];
static inode_table_t  itab;
static uint8_t        block_bitmap[RD_BLOCK_SIZE];
static uint8_t        inode_bitmap[RD_BLOCK_SIZE];

typedef struct { int32_t inode; uint32_t offset; } open_file_t;
static open_file_t open_files[FS_MAX_OPEN];

/* ---------------------------------------------------------------------------
 * Tiny string helpers (no libc)
 * --------------------------------------------------------------------------*/

static void str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}
static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ---------------------------------------------------------------------------
 * Bitmap helpers
 * --------------------------------------------------------------------------*/
static void bitmap_set(uint8_t *bm, uint32_t i)   { bm[i / 8] |=  (uint8_t)(1 << (i % 8)); }
static void bitmap_clear(uint8_t *bm, uint32_t i) { bm[i / 8] &= (uint8_t)~(1 << (i % 8)); }
static int  bitmap_test(uint8_t *bm, uint32_t i)  { return bm[i / 8] & (1 << (i % 8)); }

static int alloc_block(void) {
    for (uint32_t b = FS_DATA_START_BLK; b < RD_TOTAL_BLOCKS; b++) {
        if (!bitmap_test(block_bitmap, b)) { bitmap_set(block_bitmap, b); return (int)b; }
    }
    return -1;
}
static int alloc_inode(void) {
    for (uint32_t i = 0; i < FS_MAX_INODES; i++) {
        if (!bitmap_test(inode_bitmap, i)) { bitmap_set(inode_bitmap, i); return (int)i; }
    }
    return -1;
}

static void fs_sync(void) {
    ramdisk_write(FS_SB_BLOCK, sbblk.raw);
    ramdisk_write(FS_DIR_BLOCK, directory);
    ramdisk_write(FS_BLOCK_BITMAP_BLK, block_bitmap);
    ramdisk_write(FS_INODE_BITMAP_BLK, inode_bitmap);
    ramdisk_write(FS_INODE_TABLE_BLK, itab.raw);
}

static int find_dirent(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (directory[i].inode >= 0 && str_eq(directory[i].name, name)) return i;
    return -1;
}
static int find_free_dirent(void) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (directory[i].inode < 0) return i;
    return -1;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------*/
void fs_init(void) {
    ramdisk_init();   /* zeroes the disk — always formats fresh (L12 §1) */

    sbblk.sb.magic        = FS_MAGIC;
    sbblk.sb.total_blocks = RD_TOTAL_BLOCKS;
    sbblk.sb.total_inodes = FS_MAX_INODES;

    for (int i = 0; i < FS_MAX_FILES; i++) directory[i].inode = -1;
    for (uint32_t i = 0; i < RD_BLOCK_SIZE; i++) { block_bitmap[i] = 0; inode_bitmap[i] = 0; }
    for (int i = 0; i < FS_MAX_INODES; i++) { itab.entries[i].used = 0; itab.entries[i].size = 0; }
    for (int i = 0; i < FS_MAX_OPEN; i++) open_files[i].inode = -1;

    /* Reserve the metadata blocks so the allocator never hands them out
     * as file data. (L12 §2) */
    for (uint32_t b = 0; b < FS_DATA_START_BLK; b++) bitmap_set(block_bitmap, b);

    fs_sync();
}

int fs_open(const char *name, int create) {
    int d = find_dirent(name);
    int inode_idx;

    if (d < 0) {
        if (!create) return -1;
        int free_d = find_free_dirent();
        int free_i = alloc_inode();
        if (free_d < 0 || free_i < 0) return -1;

        str_copy(directory[free_d].name, name, sizeof(directory[free_d].name));
        directory[free_d].inode = free_i;
        itab.entries[free_i].used = 1;
        itab.entries[free_i].size = 0;
        for (int k = 0; k < FS_DIRECT_BLOCKS; k++) itab.entries[free_i].blocks[k] = 0;

        fs_sync();
        inode_idx = free_i;
    } else {
        inode_idx = directory[d].inode;
    }

    for (int fd = 0; fd < FS_MAX_OPEN; fd++) {
        if (open_files[fd].inode < 0) {
            open_files[fd].inode  = inode_idx;
            open_files[fd].offset = 0;
            return fd;
        }
    }
    return -1;   /* too many open files */
}

int fs_read(int fd, void *buf, uint32_t size) {
    if (fd < 0 || fd >= FS_MAX_OPEN || open_files[fd].inode < 0) return -1;
    inode_t *ino = &itab.entries[open_files[fd].inode];

    uint32_t remaining = (open_files[fd].offset < ino->size) ? (ino->size - open_files[fd].offset) : 0;
    uint32_t to_read    = (size < remaining) ? size : remaining;

    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0;
    static uint8_t block_buf[RD_BLOCK_SIZE];   /* static: avoid a 4KB stack frame */

    while (done < to_read) {
        uint32_t pos   = open_files[fd].offset + done;
        uint32_t bidx  = pos / RD_BLOCK_SIZE;
        uint32_t boff  = pos % RD_BLOCK_SIZE;
        if (bidx >= FS_DIRECT_BLOCKS || ino->blocks[bidx] == 0) break;

        ramdisk_read(ino->blocks[bidx], block_buf);
        uint32_t chunk = RD_BLOCK_SIZE - boff;
        if (chunk > to_read - done) chunk = to_read - done;
        for (uint32_t i = 0; i < chunk; i++) dst[done + i] = block_buf[boff + i];
        done += chunk;
    }

    open_files[fd].offset += done;
    return (int)done;
}

int fs_write(int fd, const void *buf, uint32_t size) {
    if (fd < 0 || fd >= FS_MAX_OPEN || open_files[fd].inode < 0) return -1;
    inode_t *ino = &itab.entries[open_files[fd].inode];

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t done = 0;
    static uint8_t block_buf[RD_BLOCK_SIZE];

    while (done < size) {
        uint32_t pos  = open_files[fd].offset + done;
        uint32_t bidx = pos / RD_BLOCK_SIZE;
        uint32_t boff = pos % RD_BLOCK_SIZE;
        if (bidx >= FS_DIRECT_BLOCKS) break;   /* hit 32 KB max file size */

        if (ino->blocks[bidx] == 0) {
            int nb = alloc_block();
            if (nb < 0) break;   /* RAM disk full */
            ino->blocks[bidx] = (uint32_t)nb;
            for (uint32_t i = 0; i < RD_BLOCK_SIZE; i++) block_buf[i] = 0;
            ramdisk_write(ino->blocks[bidx], block_buf);
        }

        ramdisk_read(ino->blocks[bidx], block_buf);
        uint32_t chunk = RD_BLOCK_SIZE - boff;
        if (chunk > size - done) chunk = size - done;
        for (uint32_t i = 0; i < chunk; i++) block_buf[boff + i] = src[done + i];
        ramdisk_write(ino->blocks[bidx], block_buf);
        done += chunk;
    }

    open_files[fd].offset += done;
    if (open_files[fd].offset > ino->size) ino->size = open_files[fd].offset;

    fs_sync();
    return (int)done;
}

void fs_close(int fd) {
    if (fd >= 0 && fd < FS_MAX_OPEN) open_files[fd].inode = -1;
}

int fs_unlink(const char *name) {
    int d = find_dirent(name);
    if (d < 0) return -1;
    inode_t *ino = &itab.entries[directory[d].inode];

    for (int i = 0; i < FS_DIRECT_BLOCKS; i++) {
        if (ino->blocks[i] != 0) { bitmap_clear(block_bitmap, ino->blocks[i]); ino->blocks[i] = 0; }
    }
    bitmap_clear(inode_bitmap, (uint32_t)directory[d].inode);
    ino->used = 0;
    ino->size = 0;

    directory[d].inode  = -1;
    directory[d].name[0] = '\0';

    fs_sync();
    return 0;
}

int fs_seek(int fd, uint32_t offset) {
    if (fd < 0 || fd >= FS_MAX_OPEN || open_files[fd].inode < 0) return -1;
    open_files[fd].offset = offset;
    return 0;
}

uint32_t fs_size(int fd) {
    if (fd < 0 || fd >= FS_MAX_OPEN || open_files[fd].inode < 0) return 0;
    return itab.entries[open_files[fd].inode].size;
}

int fs_list(fs_dirent_t *out, int max) {
    int count = 0;
    for (int i = 0; i < FS_MAX_FILES && count < max; i++) {
        if (directory[i].inode >= 0) {
            str_copy(out[count].name, directory[i].name, sizeof(out[count].name));
            out[count].size = itab.entries[directory[i].inode].size;
            count++;
        }
    }
    return count;
}