#ifndef _USER_FS_TYPES_H
#define _USER_FS_TYPES_H

/**
 * Struct layouts and pure (no disk/kernel access) helper functions copied
 * from header/fs/ext2.h and header/driver/disk.h, since this freestanding
 * userland build can't include those kernel headers directly.
 *
 * @warning
 * Must be kept in sync by hand with header/fs/ext2.h / header/driver/disk.h
 * if those struct layouts ever change.
 */

#include <stdbool.h>
#include <stdint.h>

#define BLOCK_SIZE 512
#define DISK_SPACE 4194304u

#define EXT2_S_IFDIR 0x4000
#define EXT2_FT_DIR 2

struct EXT2DriverRequest {
    void *buf;
    char *name;
    uint8_t name_len;
    uint32_t parent_inode;
    uint32_t buffer_size;
    bool is_directory;
} __attribute__((packed));

struct EXT2Inode {
    uint16_t i_mode;
    uint32_t i_size;
    uint32_t i_blocks;
    uint32_t i_block[15];
} __attribute__((packed));

struct EXT2DirectoryEntry {
    uint32_t inode;
    uint16_t rec_len;
    uint16_t name_len;
    uint8_t file_type;
} __attribute__((packed));

struct BlockBuffer {
    uint8_t buf[BLOCK_SIZE];
} __attribute__((packed));

static inline char *get_entry_name(void *entry) {
    return (char *) entry + sizeof(struct EXT2DirectoryEntry);
}

static inline struct EXT2DirectoryEntry *
get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) ptr + offset);
}

static inline struct EXT2DirectoryEntry *
get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return get_directory_entry(entry, entry->rec_len);
}

static inline uint16_t get_entry_record_len(uint8_t name_len) {
    return (sizeof(struct EXT2DirectoryEntry) + name_len + 3) & ~3;
}

static inline uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *first_child = get_next_directory_entry(
        get_next_directory_entry(get_directory_entry(ptr, 0))
    );
    return (uint32_t) ((uint8_t *) first_child - (uint8_t *) ptr);
}

#endif
