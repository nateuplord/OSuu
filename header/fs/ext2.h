#ifndef _EXT2_H
#define _EXT2_H

#include "driver/disk.h"
// #include "stdlib/stdtype.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* FS constants */

/// legacy from FAT32 filesystem IF2130 OS
#define BOOT_SECTOR 0

/// 4MB disk space (because our disk or storage.bin is 4MB)
#define DISK_SPACE 4194304u

/// this indicating that the filesystem used by OS is ext2
#define EXT2_SUPER_MAGIC 0xEF53

/// size of inode
#define INODE_SIZE sizeof(struct EXT2Inode)

/// number of inode per block (512)
#define INODES_PER_TABLE (BLOCK_SIZE / INODE_SIZE)

/// number of groups in the filesystem
#define GROUPS_COUNT (BLOCK_SIZE / sizeof(struct EXT2BlockGroupDescriptor) / 2u)

/// number of blocks per group
#define BLOCKS_PER_GROUP (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT)
#define INODES_TABLE_BLOCK_COUNT 16u

/// number of inodes per group
#define INODES_PER_GROUP (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT)

/**
 * Inodes constants
 * @see [nongnu.org](https://www.nongnu.org/ext2-doc/ext2.html#inode-table)
 */

/// regular file
#define EXT2_S_IFREG 0x8000

/// directory
#define EXT2_S_IFDIR 0x4000

/* File type constant */
/**
 * @see [nongnu.org](https://www.nongnu.org/ext2-doc/ext2.html#linked-directories):
 * - Table 4.2. Defined Inode File Type Values
 */

/// Unknown File Type
#define EXT2_FT_UNKNOWN 0

/// Regular File
#define EXT2_FT_REG_FILE 1

/// Directory
#define EXT2_FT_DIR 2

/// Character Special File
#define EXT2_FT_NEXT 3

/**
 * EXT2DriverRequest
 * @details Derived and modified from FAT32DriverRequest legacy IF2130 OS
 */
struct EXT2DriverRequest {
    void *buf;
    char *name;
    uint8_t name_len;
    uint32_t parent_inode;
    uint32_t buffer_size;
    bool is_directory;
} __attribute__((packed));

/**
 * EXT2Superblock
 * @see [nongnu.org](https://www.nongnu.org/ext2-doc/ext2.html#superblock)
 */
struct EXT2Superblock {
    /**
     * Total number of inodes,
     * both used and free, in the file system
     */
    uint32_t s_inodes_count;

    /**
     * Total number of blocks in the system,
     * including all used, free and reserved
     */
    uint32_t s_blocks_count;

    /**
     * Total number of blocks
     * reserved for the usage of the super user.
     *
     * @note
     * Maybe not used because there is no superuser in our system.
     */
    uint32_t s_r_blocks_count;

    /**
     * Total number of free blocks,
     * including the number of reserved blocks.
     */
    uint32_t s_free_blocks_count;

    /**
     * Total number of free inodes.
     * @details This is a sum of all free inodes of all the block groups.
     */
    uint32_t s_free_inodes_count;

    /**
     * Indicates the first data block.
     *
     * @details
     * Represented by the ID of the block containing
     * the superblock structure.
     */
    uint32_t s_first_data_block;

    /**
     * First inode that can be used.
     * @note Set this to 1, indicating root inode (maybe)
     */
    uint32_t s_first_ino;

    /**
     * Total number of blocks per group.
     *
     * @details
     * This value in combination with `s_first_data_block` can be used
     * to determine the block groups boundaries.
     *
     * Due to volume size boundaries, the last block group might have
     * a smaller number of blocks than what is specified in this field.
     */
    uint32_t s_blocks_per_group;

    /**
     * Total number of fragments per group.
     * @details It is also used to determine the size of the block bitmap of each block group.
     */
    uint32_t s_frags_per_group;

    /**
     * Total number of inodes per group.
     *
     * @details
     * This is also used to determine the size of the inode bitmap of each block group.
     *
     * @note
     * You cannot have more than (block size in bytes * 8) inodes per group
     * as the inode bitmap must fit within a single block. This value must be a
     * perfect multiple of the number of inodes that can fit in a block
     *
     * > `((1024 << s_log_block_size) / s_inode_size)`.
     */
    uint32_t s_inodes_per_group;

    /**
     * File system type.
     * @details For ext2, this value is `0xEF53`.
     */
    uint16_t s_magic;

    /// Number of blocks to pre-allocate for files
    uint8_t s_prealloc_blocks;

    /// Number of blocks to pre-allocate for directories.
    uint8_t s_prealloc_dir_blocks;

} __attribute__((packed));

/**
 * @see [nongnu.org](https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table)
 */
struct EXT2BlockGroupDescriptor {
    /**
     * ID of the first block of the block bitmap for the represented group.
     *
     * @details
     * The actual block bitmap is located within its own allocated
     * blocks starting at the block ID specified by this value.
     */
    uint32_t bg_block_bitmap;

    /// ID of the first block of the inode bitmap for the represented group
    uint32_t bg_inode_bitmap;

    uint32_t bg_inode_table;

    /// Total number of free blocks for the represented group.
    uint16_t bg_free_blocks_count;

    /// Total number of free inodes for the represented group.
    uint16_t bg_free_inodes_count;

    /// Number of inodes allocated to directories for the represented group.
    uint16_t bg_used_dirs_count;

    /// Padding for the structure on a 32bit boundary.
    uint16_t bg_pad;

    /// Reserved space for future revisions.
    uint32_t bg_reserved[3];
} __attribute__((packed));

/**
 * @note Can be changed with fixed size array
 * @see [nongnu.org](https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table)
 */
struct EXT2BlockGroupDescriptorTable {
    struct EXT2BlockGroupDescriptor table[GROUPS_COUNT];
};

/**
 * EXT2Inode
 *
 * @details
 * "Inode" stands for index node.
 *
 * It is a data structure in a Unix-style file system
 * that describes a file-system object such as a file or a directory.
 */
struct EXT2Inode {
    /// File type and the access rights.
    uint16_t i_mode;

    /// Size of the file in bytes.
    uint32_t i_size;

    /// Number of blocks used by the file.
    uint32_t i_blocks;

    /**
     * Numbers pointing to the blocks containing the data for this inode
     *
     * @details
     * - The first 12 blocks are direct blocks.
     * - The 13th entry in this array is the block number of the first indirect
     * block which is a block containing an array of block ID containing the data
     * Therefore, the 13th block of the file will be the first block ID contained
     * in the indirect block. With a 1 KiB block size, blocks 13 to 268 of the file
     * data are contained in this indirect block.
     * - The 14th entry in this array is the block number of the first
     * doubly-indirect block
     * - The 15th entry in this array is the block number of the triply-indirect
     * block
     *
     * @note
     * Maybe this video will help:
     * - https://www.youtube.com/watch?v=tMVj22EWg6A
     *
     */
    uint32_t i_block[15];

} __attribute__((packed));

/**
 * @note Can be changed with a fixed-sized array.
 */
struct EXT2InodeTable {
    struct EXT2Inode table[INODES_PER_GROUP];
};

/**
 * EXT2DirectoryEntry
 *
 * @details
 * Linked List Directory
 *
 * @see
 * [nongnu.org](https://www.nongnu.org/ext2-doc/ext2.html#linked-directories)
 */
struct EXT2DirectoryEntry {
    /**
     * Inode number of the file entry.
     * @note A value of 0 indicate that the entry is not used.
     */
    uint32_t inode;

    /**
     * Displacement to the next directory entry from the start of the current directory entry.
     *
     * @warning
     * This field must have a value at least equal to the length of the current record.
     *
     * The directory entries must be aligned on 4 bytes boundaries and
     * there cannot be any directory entry spanning multiple data blocks.
     *
     * If an entry cannot completely fit in one block,
     * it must be pushed to the next data block and
     * the `rec_len` of the previous entry properly adjusted.
     */
    uint16_t rec_len;

    /// Length of the file name
    uint16_t name_len;

    /// File type
    uint8_t file_type;

} __attribute__((packed));

/**
 * Get the name of the entry
 *
 * @param entry
 * The directory entry
 *
 * @return
 * The name of the entry
 */
char *get_entry_name(void *entry);

/**
 * Get the directory entry from the buffer
 *
 * @param ptr
 * The buffer that contains the directory table
 *
 * @param offset
 * The offset of the entry
 *
 * @return
 * The directory entry
 */
struct EXT2DirectoryEntry *get_directory_entry(
    void *ptr,
    uint32_t offset
);

/**
 * Get the next directory entry from the current entry
 *
 * @param entry
 * The current entry
 *
 * @return
 * The next directory entry
 */
struct EXT2DirectoryEntry *
get_next_directory_entry(struct EXT2DirectoryEntry *entry);

/**
 * Get the record length of the entry
 *
 * @param name_len
 * The length of the name of the entry
 *
 * @return
 * The record length of the entry
 */
uint16_t get_entry_record_len(uint8_t name_len);

/**
 * Get the offset of the first child of the directory
 *
 * @param ptr
 * The buffer that contains the directory table
 *
 * @return
 * The offset of the first child of the directory
 */
uint32_t get_dir_first_child_offset(void *ptr);

/**
 * Get `bgd` index from inode
 * @details Inode follows one-indexing.
 *
 * @param inode
 * 1 to `INODES_PER_GROUP * GROUP_COUNT`
 *
 * @return
 * `bgd` index (0 to `GROUP_COUNT - 1`)
 */
uint32_t inode_to_bgd(uint32_t inode);

/**
 * Get inode local index in the corrresponding `bgd`
 *
 * @param inode
 * 1 to `INODES_PER_GROUP * GROUP_COUNT`
 *
 * @return
 * Local index
 */
uint32_t inode_to_local(uint32_t inode);

/**
 * Create a new directory using the given node
 *
 * @details
 * First item of directory table is its node location (name will be .).
 * Second item of directory is its parent location (name will be ..).
 *
 * @param node
 * Pointer to an inode
 *
 * @param inode
 * Allocated inode
 *
 * @param parent_inode
 * Inode of parent directory (if root directory, the parent is itself)
 */
void init_directory_table(
    struct EXT2Inode *node,
    uint32_t inode,
    uint32_t parent_inode
);

/**
 * Check whether the filesystem signature is missing or not in the boot sector
 *
 * @return
 * True if `memcmp(boot_sector, fs_signature)` is false.
 */
bool is_empty_storage(void);

/**
 * Create a new EXT2 filesystem.
 *
 * @details
 * Will do these steps:
 * 1. Write `fs_signature` into boot sector,
 * 2. Initialize super block, `bgd` table, block and inode bitmap, and
 * 3. Create the root directory.
 */
void create_ext2(void);

/**
 * Initialize file system driver state.
 *
 * @details
 * If `is_empty_storage()` is true, then `create_ext2()`.
 * Else, read and cache super block (located at block 1) and
 * `bgd` table (located at block 2) into state.
 */
void initialize_filesystem_ext2(void);

/**
 * Check whether a directory table has children or not.
 *
 * @param inode
 * Inode of a directory table
 *
 * @return
 * True if `first_child_entry->inode = 0`
 */
bool is_directory_empty(uint32_t inode);

/**
 * Read a folder / directory.
 *
 * @param request
 * A buffer that points to a `EXT2Directory` struct
 *
 * @return
 * Error codes:
 * - `0`: Success
 * - `1`: Not a folder
 * - `2`: Not found
 * - `3`: Invalid parent folder
 * - `-1`: Unknown
 */
int8_t read_directory(struct EXT2DriverRequest *request);

/**
 * Read a file.
 * @details All attribute will be used except `is_dir` for read, `buffer_size` will limit reading count
 *
 * @param *request
 * A buffer that points to a `EXT2Directory` struct
 *
 * @return
 * Error codes:
 * - `0`: Success
 * - `1`: Not a file
 * - `2`: Not enough buffer
 * - `3`: Not found
 * - `4`: Invalid parent folder
 * - `-1`: Unknown
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * Write a file or a folder to file system.
 *
 * @details
 * All attribute will be used for write except `is_dir`.
 * `buffer_size == 0` then create a folder / directory.
 *
 * @note
 * It is possible for a file with the same name as a folder to exist.
 *
 * @param *request
 * A buffer that points to a `EXT2Directory` struct
 *
 * @return
 * Error codes:
 * - `0`: Success
 * - `1`: File/folder already exist
 * - `2`: Invalid parent folder
 * - `-1`: Unknown
 */
int8_t write(struct EXT2DriverRequest *request);

/**
 * Delete a file or empty directory in file system
 *
 * @details
 * `buf` and `buffer_size` is unused.
 * `is_dir == true` means delete folder
 *
 * @note
 * It is possible for a file with the same name as a folder to exist.
 *
 * @param request
 * A buffer that points to a `EXT2Directory` struct
 *
 * @return
 * Error codes:
 * - `0`: Success
 * - `1`: Not found
 * - `2`: Folder is not empty
 * - `3`: Invalid parent folder
 * - `-1`: Unknown
 */
int8_t delete(struct EXT2DriverRequest request);

/**
 * Get a free inode from the disk, assuming it's always available.
 *
 * @return
 * New inode
 */
uint32_t allocate_node(void);

/**
 * De-allocate node from the disk
 * @note Will also de-allocate its used blocks
 * and all of the blocks of in its indirect blocks if necessary.
 *
 * @param inode
 * De-allocated inode
 */
void deallocate_node(uint32_t inode);

/**
 * De-allocate node blocks
 *
 * @param loc
 * `node->block`
 *
 * @param blocks
 * Number of blocks
 */
void deallocate_blocks(
    void *loc,
    uint32_t blocks
);

/**
 * De-allocate block from the disk
 *
 * @param locations
 * Block locations
 *
 * @param blocks
 * Number of blocks
 *
 * @param bitmap
 * Block bitmap
 *
 * @param depth
 * Depth of the block
 *
 * @param last_bgd
 * Last used `bgd`
 *
 * @param bgd_loaded
 * Whether `bgd` is loaded or not
 *
 * @return
 * New last `bgd`
 */
uint32_t deallocate_block(
    uint32_t *locations,
    uint32_t blocks,
    struct BlockBuffer *bitmap,
    uint32_t depth,
    uint32_t *last_bgd,
    bool bgd_loaded
);

/**
 * Write `node->block` in the given node.
 *
 * @details
 * Will allocate at least `node->blocks` number of blocks.
 *
 * @note
 * If the first 12 items of `node-> block` is not enough,
 * indirect blocks will be used.
 *
 * > Only implement until doubly indirect block.
 * > If you want to implement triply indirect block,
 * > increase the storage size to at least 256 MB.
 *
 * @param ptr
 * Write buffer
 *
 * @param node
 * Pointer to the node
 *
 * @param preffered_bgd
 * It is located at the node inode `bgd`
 */
void allocate_node_blocks(
    void *ptr,
    struct EXT2Inode *node,
    uint32_t prefered_bgd
);

/**
 * Update the node to the disk.
 *
 * @param node
 * Pointer to a node
 *
 * @param inode
 * Location of the node
 */
void sync_node(
    struct EXT2Inode *node,
    uint32_t inode
);

#endif
