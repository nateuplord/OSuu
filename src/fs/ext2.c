#include "fs/ext2.h"
#include "driver/disk.h"
#include "stdlib/string.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

const uint8_t fs_signature[BLOCK_SIZE] = "osu! is an open "
                                         "source rhythm   "
                                         "game originally "
                                         "created and     "
                                         "self-published  "
                                         "by Australian   "
                                         "developer Dean  "
                                         "Herbert as a    "
                                         "freeware.       "
                                         "                "
                                         "It was released "
                                         "for Microsoft   "
                                         "Windows on 16   "
                                         "September 2007, "
                                         "with later ports"
                                         "to macOS, Linux,"
                                         "Android, and    "
                                         "iOS.          \n";

bool is_empty_storage() {
    struct BlockBuffer buf;
    read_blocks(&buf, BOOT_SECTOR, 1);
    return memcmp(buf.buf, fs_signature, BLOCK_SIZE) != 0;
}

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

char *get_entry_name(void *entry) {
    return (char *) entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_directory_entry(
    void *ptr,
    uint32_t offset
) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) ptr + offset);
}

struct EXT2DirectoryEntry *
get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return get_directory_entry(entry, entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    return (sizeof(struct EXT2DirectoryEntry) + name_len + 3) & ~3;
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *first_child = get_next_directory_entry(
        get_next_directory_entry(get_directory_entry(ptr, 0))
    );

    // first_child is after . and ..
    return (uint32_t) ((uint8_t *) first_child - (uint8_t *) ptr);
}

void init_directory_table(
    struct EXT2Inode *node,
    uint32_t inode,
    uint32_t parent_inode
) {
    struct BlockBuffer buf;
    struct EXT2DirectoryEntry *dot = get_directory_entry(buf.buf, 0);

    dot->inode = inode;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->rec_len = get_entry_record_len(1);

    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);

    dotdot->inode = parent_inode;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->rec_len = BLOCK_SIZE - dot->rec_len;

    memcpy(get_entry_name(dot), ".", 1);
    memcpy(get_entry_name(dotdot), "..", 2);

    write_blocks(&buf, node->i_block[0], 1);
    node->i_size = BLOCK_SIZE;
    node->i_blocks = 1;
}

void create_ext2(void) {
    // init with fs_signature
    write_blocks(fs_signature, BOOT_SECTOR, 1);

    // init superblock and populate it
    struct EXT2Superblock sb;
    memset(&sb, 0, sizeof(sb));

    sb.s_inodes_count = GROUPS_COUNT * INODES_PER_GROUP;
    sb.s_blocks_count = GROUPS_COUNT * BLOCKS_PER_GROUP;

    sb.s_free_inodes_count = sb.s_inodes_count - 1;
    sb.s_free_blocks_count = sb.s_blocks_count - 3 - (18 * GROUPS_COUNT) - 1;

    sb.s_first_ino = 1;
    sb.s_first_data_block = 3;

    sb.s_blocks_per_group = BLOCKS_PER_GROUP;
    sb.s_inodes_per_group = INODES_PER_GROUP;

    sb.s_magic = EXT2_SUPER_MAGIC;

    // init block group descriptor table and populate it
    struct EXT2BlockGroupDescriptorTable bgdt;
    memset(&bgdt, 0, sizeof(bgdt));

    for (uint32_t group_index = 0; group_index < GROUPS_COUNT; group_index++) {
        int group_start = group_index * BLOCKS_PER_GROUP;
        int overhead = (group_index == 0) ? 3 : 0;

        bgdt.table[group_index].bg_block_bitmap = group_start + overhead;

        bgdt.table[group_index].bg_inode_bitmap =
            bgdt.table[group_index].bg_block_bitmap + 1;

        bgdt.table[group_index].bg_inode_table =
            bgdt.table[group_index].bg_inode_bitmap + 1;

        bgdt.table[group_index].bg_free_blocks_count =
            BLOCKS_PER_GROUP - 18 - overhead -
            (group_index == inode_to_bgd(1) ? 1 : 0);

        bgdt.table[group_index].bg_free_inodes_count =
            INODES_PER_GROUP - (group_index == inode_to_bgd(1) ? 1 : 0);

        bgdt.table[group_index].bg_used_dirs_count =
            (group_index == inode_to_bgd(1) ? 1 : 0);

        // build the bitmaps (blocks and inode)
        struct BlockBuffer block_bitmap_buf;
        memset(&block_bitmap_buf, 0, sizeof(block_bitmap_buf));

        int used_blocks = overhead + 18 + (group_index == 0 ? 1 : 0);
        for (int i = 0; i < used_blocks; i++) {
            int byte_index = i / 8;
            int bit_in_byte = i % 8;
            block_bitmap_buf.buf[byte_index] |= (1 << bit_in_byte);
        }

        write_blocks(
            &block_bitmap_buf, bgdt.table[group_index].bg_block_bitmap, 1
        );

        struct BlockBuffer inode_bitmap_buf;
        memset(&inode_bitmap_buf, 0, sizeof(inode_bitmap_buf));

        if (group_index == 0) {
            inode_bitmap_buf.buf[0] |= (1 << 0);
        }

        write_blocks(
            &inode_bitmap_buf, bgdt.table[group_index].bg_inode_bitmap, 1
        );
    }

    // Write superblock and group descriptor table to disk
    struct BlockBuffer buf;
    memset(&buf, 0, sizeof(buf));
    memcpy(buf.buf, &sb, sizeof(sb));
    write_blocks(&buf, 1, 1);

    memset(&buf, 0, sizeof(buf));
    memcpy(buf.buf, &bgdt, sizeof(bgdt));
    write_blocks(&buf, 2, 1);

    // root inode
    struct EXT2Inode root_node;
    memset(&root_node, 0, sizeof(root_node));

    root_node.i_mode = EXT2_S_IFDIR;

    root_node.i_block[0] =
        bgdt.table[0].bg_inode_table + INODES_TABLE_BLOCK_COUNT;

    init_directory_table(&root_node, 1, 1);
    sync_node(&root_node, 1);
}

void sync_node(
    struct EXT2Inode *node,
    uint32_t inode
) {
    uint32_t bgd_index = inode_to_bgd(inode);
    uint32_t local_index = inode_to_local(inode);

    struct BlockBuffer buf;
    read_blocks(&buf, 2, 1);

    struct EXT2BlockGroupDescriptorTable *bgdt_ptr =
        (struct EXT2BlockGroupDescriptorTable *) buf.buf;

    uint32_t inode_table_start = bgdt_ptr->table[bgd_index].bg_inode_table;

    uint32_t block_offset = local_index / INODES_PER_TABLE;
    uint32_t slot_in_block = local_index % INODES_PER_TABLE;
    uint32_t target_block = inode_table_start + block_offset;

    struct BlockBuffer otherbuf;
    read_blocks(&otherbuf, target_block, 1);

    struct EXT2Inode *inode_array = (struct EXT2Inode *) otherbuf.buf;
    inode_array[slot_in_block] = *node;

    write_blocks(&otherbuf, target_block, 1);
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    }
}

// Allocation
uint32_t allocate_node(void) {
    struct BlockBuffer bgdt_buf;
    read_blocks(&bgdt_buf, 2, 1); // bgdt

    struct EXT2BlockGroupDescriptorTable *bgdt =
        (struct EXT2BlockGroupDescriptorTable *) bgdt_buf.buf;

    struct BlockBuffer sb_buf;
    read_blocks(&sb_buf, 1, 1); // superblock

    struct EXT2Superblock *sb = (struct EXT2Superblock *) sb_buf.buf;

    for (uint32_t group_index = 0; group_index < GROUPS_COUNT; group_index++) {
        struct BlockBuffer inode_bitmap_buf;

        read_blocks(
            &inode_bitmap_buf, bgdt->table[group_index].bg_inode_bitmap, 1
        );

        for (
            uint32_t local_index = 0; local_index < INODES_PER_GROUP;
            local_index++
        ) {
            int byte_index = local_index / 8;
            int bit_in_byte = local_index % 8;

            if ((inode_bitmap_buf.buf[byte_index] & (1 << bit_in_byte)) == 0) {
                // free bit
                inode_bitmap_buf.buf[byte_index] |= (1 << bit_in_byte);
                write_blocks(
                    &inode_bitmap_buf, bgdt->table[group_index].bg_inode_bitmap,
                    1
                );

                bgdt->table[group_index].bg_free_inodes_count--;

                struct BlockBuffer buf;
                memset(&buf, 0, sizeof(buf));
                memcpy(buf.buf, bgdt, sizeof(*bgdt));
                write_blocks(&buf, 2, 1);

                sb->s_free_inodes_count--;
                memset(&buf, 0, sizeof(buf));
                memcpy(buf.buf, sb, sizeof(*sb));
                write_blocks(&buf, 1, 1);

                uint32_t inode =
                    group_index * INODES_PER_GROUP + local_index + 1;

                return inode;
            }
        }
    }
    return 0;
}

uint32_t find_one_free_block(uint32_t prefered_bgd) {
    struct BlockBuffer bgdt_buf;
    read_blocks(&bgdt_buf, 2, 1); // bgdt

    struct EXT2BlockGroupDescriptorTable *bgdt =
        (struct EXT2BlockGroupDescriptorTable *) bgdt_buf.buf;

    struct BlockBuffer sb_buf;
    read_blocks(&sb_buf, 1, 1); // superblock

    struct EXT2Superblock *sb = (struct EXT2Superblock *) sb_buf.buf;

    for (uint32_t offset = 0; offset < GROUPS_COUNT; offset++) {
        uint32_t group_index = (prefered_bgd + offset) % GROUPS_COUNT;
        struct BlockBuffer block_bitmap_buf;

        read_blocks(
            &block_bitmap_buf, bgdt->table[group_index].bg_block_bitmap, 1
        );

        for (
            uint32_t local_index = 0; local_index < BLOCKS_PER_GROUP;
            local_index++
        ) {
            int byte_index = local_index / 8;
            int bit_in_byte = local_index % 8;

            if ((block_bitmap_buf.buf[byte_index] & (1 << bit_in_byte)) == 0) {
                // free bit
                block_bitmap_buf.buf[byte_index] |= (1 << bit_in_byte);

                write_blocks(
                    &block_bitmap_buf, bgdt->table[group_index].bg_block_bitmap,
                    1
                );

                bgdt->table[group_index].bg_free_blocks_count--;

                struct BlockBuffer buf;
                memset(&buf, 0, sizeof(buf));
                memcpy(buf.buf, bgdt, sizeof(*bgdt));
                write_blocks(&buf, 2, 1);

                sb->s_free_blocks_count--;
                memset(&buf, 0, sizeof(buf));
                memcpy(buf.buf, sb, sizeof(*sb));
                write_blocks(&buf, 1, 1);

                return group_index * BLOCKS_PER_GROUP + local_index;
            }
        }
    }
    return 0;
}

void allocate_node_blocks(
    void *ptr,
    struct EXT2Inode *node,
    uint32_t prefered_bgd
) {
    uint32_t direct_count = (node->i_blocks < 12) ? node->i_blocks : 12;

    for (uint32_t i = 0; i < direct_count; i++) {
        uint32_t block_num = find_one_free_block(prefered_bgd);
        node->i_block[i] = block_num;
        write_blocks((uint8_t *) ptr + i * BLOCK_SIZE, node->i_block[i], 1);
    }

    uint32_t written = direct_count;
    uint32_t remaining = node->i_blocks - direct_count;

    if (remaining > 0) {
        uint32_t indirect_block_num = find_one_free_block(prefered_bgd);
        node->i_block[12] = indirect_block_num;

        struct BlockBuffer indirect_buf;
        memset(&indirect_buf, 0, sizeof(indirect_buf));

        uint32_t singly_count = (remaining < 128) ? remaining : 128;
        for (uint32_t j = 0; j < singly_count; j++) {
            uint32_t block_num = find_one_free_block(prefered_bgd);
            ((uint32_t *) indirect_buf.buf)[j] = block_num;

            write_blocks((uint8_t *) ptr + written * BLOCK_SIZE, block_num, 1);
            written++;
        }
        write_blocks(&indirect_buf, indirect_block_num, 1);
        remaining -= singly_count;
    }

    if (remaining > 0) {
        uint32_t doubly_block_num = find_one_free_block(prefered_bgd);
        node->i_block[13] = doubly_block_num;

        struct BlockBuffer doubly_buf;
        memset(&doubly_buf, 0, sizeof(doubly_buf));

        uint32_t num_singly_needed = (remaining + 127) / 128;
        for (uint32_t k = 0; k < num_singly_needed; k++) {
            uint32_t singly_block_num = find_one_free_block(prefered_bgd);
            ((uint32_t *) doubly_buf.buf)[k] = singly_block_num;

            struct BlockBuffer singly_buf;
            memset(&singly_buf, 0, sizeof(singly_buf));

            uint32_t this_count = (remaining < 128) ? remaining : 128;
            for (uint32_t j = 0; j < this_count; j++) {
                uint32_t block_num = find_one_free_block(prefered_bgd);
                ((uint32_t *) singly_buf.buf)[j] = block_num;

                write_blocks(
                    (uint8_t *) ptr + written * BLOCK_SIZE, block_num, 1
                );

                written++;
            }
            write_blocks(&singly_buf, singly_block_num, 1);
            remaining -= this_count;
        }
        write_blocks(&doubly_buf, doubly_block_num, 1);
    }
}

void get_node(
    struct EXT2Inode *node,
    uint32_t inode
) {
    uint32_t bgd_index = inode_to_bgd(inode);
    uint32_t local_index = inode_to_local(inode);

    struct BlockBuffer buf;
    read_blocks(&buf, 2, 1);

    struct EXT2BlockGroupDescriptorTable *bgdt_ptr =
        (struct EXT2BlockGroupDescriptorTable *) buf.buf;

    uint32_t inode_table_start = bgdt_ptr->table[bgd_index].bg_inode_table;
    uint32_t block_offset = local_index / INODES_PER_TABLE;
    uint32_t slot_in_block = local_index % INODES_PER_TABLE;
    uint32_t target_block = inode_table_start + block_offset;

    struct BlockBuffer otherbuf;
    read_blocks(&otherbuf, target_block, 1);

    struct EXT2Inode *inode_array = (struct EXT2Inode *) otherbuf.buf;
    *node = inode_array[slot_in_block];
}

void deallocate_node(uint32_t inode) {
    struct EXT2Inode node;
    get_node(&node, inode);

    deallocate_blocks(node.i_block, node.i_blocks);

    uint32_t bgd_index = inode_to_bgd(inode);
    uint32_t local_index = inode_to_local(inode);

    struct BlockBuffer buf;
    read_blocks(&buf, 2, 1);

    struct EXT2BlockGroupDescriptorTable *bgdt =
        (struct EXT2BlockGroupDescriptorTable *) buf.buf;

    struct BlockBuffer inode_bitmap_buf;
    int byte_index = local_index / 8;

    read_blocks(&inode_bitmap_buf, bgdt->table[bgd_index].bg_inode_bitmap, 1);

    inode_bitmap_buf.buf[byte_index] &= ~(1 << (local_index % 8));
    write_blocks(&inode_bitmap_buf, bgdt->table[bgd_index].bg_inode_bitmap, 1);

    struct BlockBuffer writebuf;
    bgdt->table[bgd_index].bg_free_inodes_count++;

    memset(&writebuf, 0, sizeof(writebuf));
    memcpy(writebuf.buf, bgdt, sizeof(*bgdt));
    write_blocks(&writebuf, 2, 1);

    struct BlockBuffer sb_buf;
    read_blocks(&sb_buf, 1, 1); // superblock

    struct EXT2Superblock *sb = (struct EXT2Superblock *) sb_buf.buf;
    sb->s_free_inodes_count++;

    memset(&writebuf, 0, sizeof(writebuf));
    memcpy(writebuf.buf, sb, sizeof(*sb));
    write_blocks(&writebuf, 1, 1);
}

void deallocate_blocks(
    void *loc,
    uint32_t blocks
) {
    uint32_t direct_count = (blocks < 12) ? blocks : 12;
    uint32_t total_freed = direct_count;

    uint32_t freed_per_group[GROUPS_COUNT] = {0};

    for (uint32_t i = 0; i < direct_count; i++) {
        uint32_t block_num = ((uint32_t *) loc)[i];
        freed_per_group[block_num / BLOCKS_PER_GROUP]++;
    }

    struct BlockBuffer bitmap;
    uint32_t last_bgd = 0;
    bool bgd_loaded = false;

    deallocate_block(
        (uint32_t *) loc, direct_count, &bitmap, 0, &last_bgd, bgd_loaded
    );

    uint32_t remaining = blocks - direct_count;
    if (remaining > 0) {
        uint32_t singly_count = (remaining < 128) ? remaining : 128;

        deallocate_block(
            &((uint32_t *) loc)[12], singly_count, &bitmap, 1, &last_bgd, false
        );

        total_freed += singly_count + 1;
        remaining -= singly_count;
    }

    if (remaining > 0) {
        uint32_t doubly_count = (remaining < 128 * 128) ? remaining : 128 * 128;

        deallocate_block(
            &((uint32_t *) loc)[13], doubly_count, &bitmap, 2, &last_bgd, false
        );

        uint32_t num_singly_used = (doubly_count + 127) / 128;
        total_freed += doubly_count + num_singly_used + 1;
        remaining -= doubly_count;
    }

    struct BlockBuffer bgdt_buf;
    read_blocks(&bgdt_buf, 2, 1);

    struct EXT2BlockGroupDescriptorTable *bgdt =
        (struct EXT2BlockGroupDescriptorTable *) bgdt_buf.buf;

    for (uint32_t group_index = 0; group_index < GROUPS_COUNT; group_index++) {
        if (freed_per_group[group_index] > 0)
            bgdt->table[group_index].bg_free_blocks_count +=
                freed_per_group[group_index];
    }

    struct BlockBuffer buf;
    memset(&buf, 0, sizeof(buf));
    memcpy(buf.buf, bgdt, sizeof(*bgdt));
    write_blocks(&buf, 2, 1);

    struct BlockBuffer sb_buf;
    read_blocks(&sb_buf, 1, 1);

    struct EXT2Superblock *sb = (struct EXT2Superblock *) sb_buf.buf;
    sb->s_free_blocks_count += total_freed;

    memset(&buf, 0, sizeof(buf));
    memcpy(buf.buf, sb, sizeof(*sb));
    write_blocks(&buf, 1, 1);
}

uint32_t deallocate_block(
    uint32_t *locations,
    uint32_t blocks,
    struct BlockBuffer *bitmap,
    uint32_t depth,
    uint32_t *last_bgd,
    bool bgd_loaded
) {
    if (depth == 0) {
        for (uint32_t i = 0; i < blocks; i++) {
            uint32_t block_num = locations[i];
            uint32_t group_index = block_num / BLOCKS_PER_GROUP;
            uint32_t local_index = block_num % BLOCKS_PER_GROUP;

            if (!bgd_loaded || group_index != *last_bgd) {
                if (bgd_loaded) {
                    struct BlockBuffer buf;
                    read_blocks(&buf, 2, 1);

                    struct EXT2BlockGroupDescriptorTable *bgdt =
                        (struct EXT2BlockGroupDescriptorTable *) buf.buf;

                    write_blocks(
                        bitmap, bgdt->table[*last_bgd].bg_block_bitmap, 1
                    );
                }
                struct BlockBuffer buf;
                read_blocks(&buf, 2, 1);

                struct EXT2BlockGroupDescriptorTable *bgdt =
                    (struct EXT2BlockGroupDescriptorTable *) buf.buf;

                read_blocks(
                    bitmap, bgdt->table[group_index].bg_block_bitmap, 1
                );

                *last_bgd = group_index;
                bgd_loaded = true;
            }
            bitmap->buf[local_index / 8] &= ~(1 << (local_index % 8));
        }

        if (bgd_loaded) {
            struct BlockBuffer buf;
            read_blocks(&buf, 2, 1);
            struct EXT2BlockGroupDescriptorTable *bgdt =
                (struct EXT2BlockGroupDescriptorTable *) buf.buf;
            write_blocks(bitmap, bgdt->table[*last_bgd].bg_block_bitmap, 1);
        }
        return *last_bgd;
    }

    uint32_t entries_per_block = BLOCK_SIZE / sizeof(uint32_t);
    uint32_t capacity_per_entry = 1;

    for (uint32_t d = 0; d < depth; d++) {
        capacity_per_entry *= entries_per_block;
    }

    uint32_t i = 0;
    while (blocks > 0) {
        uint32_t indirect_block_num = locations[i];
        struct BlockBuffer buf;
        read_blocks(&buf, indirect_block_num, 1);

        uint32_t covers =
            (blocks < capacity_per_entry) ? blocks : capacity_per_entry;

        deallocate_block(
            (uint32_t *) buf.buf, covers, bitmap, depth - 1, last_bgd,
            bgd_loaded
        );

        uint32_t group_index = indirect_block_num / BLOCKS_PER_GROUP;
        uint32_t local_index = indirect_block_num % BLOCKS_PER_GROUP;

        if (!bgd_loaded || group_index != *last_bgd) {
            if (bgd_loaded) {
                struct BlockBuffer bgdt_buf;
                read_blocks(&bgdt_buf, 2, 1);

                struct EXT2BlockGroupDescriptorTable *bgdt =
                    (struct EXT2BlockGroupDescriptorTable *) bgdt_buf.buf;

                write_blocks(bitmap, bgdt->table[*last_bgd].bg_block_bitmap, 1);
            }
            struct BlockBuffer bgdt_buf;
            read_blocks(&bgdt_buf, 2, 1);

            struct EXT2BlockGroupDescriptorTable *bgdt =
                (struct EXT2BlockGroupDescriptorTable *) bgdt_buf.buf;

            read_blocks(bitmap, bgdt->table[group_index].bg_block_bitmap, 1);

            *last_bgd = group_index;
            bgd_loaded = true;
        }

        bitmap->buf[local_index / 8] &= ~(1 << (local_index % 8));
        blocks -= covers;
        i++;
    }

    if (bgd_loaded) {
        struct BlockBuffer bgdt_buf;
        read_blocks(&bgdt_buf, 2, 1);

        struct EXT2BlockGroupDescriptorTable *bgdt =
            (struct EXT2BlockGroupDescriptorTable *) bgdt_buf.buf;

        write_blocks(bitmap, bgdt->table[*last_bgd].bg_block_bitmap, 1);
    }

    return *last_bgd;
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode node;
    get_node(&node, inode);

    struct BlockBuffer buf;
    read_blocks(&buf, node.i_block[0], 1);

    uint32_t offset = get_dir_first_child_offset(buf.buf);
    if (offset >= BLOCK_SIZE) {
        return true;
    }

    struct EXT2DirectoryEntry *first_child =
        get_directory_entry(buf.buf, offset);

    return first_child->inode == 0;
}

int8_t read_directory(struct EXT2DriverRequest *request) {
    struct EXT2Inode parent_node;
    get_node(&parent_node, request->parent_inode);

    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 3;
    }

    struct BlockBuffer parent_buf;
    read_blocks(&parent_buf, parent_node.i_block[0], 1);

    uint32_t offset = get_dir_first_child_offset(parent_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(parent_buf.buf, offset);

        if (entry->inode == 0) {
            break;
        }

        if ((entry->name_len == request->name_len) &&
            memcmp(get_entry_name(entry), request->name, request->name_len) ==
                0) {

            if (entry->file_type != EXT2_FT_DIR) {
                return 1;
            }

            struct EXT2Inode found_node;
            get_node(&found_node, entry->inode);
            read_blocks(request->buf, found_node.i_block[0], 1);

            return 0;
        }
        offset += entry->rec_len;
    }
    return 2;
}

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2Inode parent_node;
    get_node(&parent_node, request.parent_inode);

    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 4;
    }

    struct BlockBuffer parent_buf;
    read_blocks(&parent_buf, parent_node.i_block[0], 1);

    uint32_t offset = get_dir_first_child_offset(parent_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(parent_buf.buf, offset);

        if (entry->inode == 0) {
            break;
        }

        if ((entry->name_len == request.name_len) &&
            memcmp(get_entry_name(entry), request.name, request.name_len) ==
                0) {

            if (entry->file_type == EXT2_FT_DIR) {
                return 1;
            }

            struct EXT2Inode found_node;
            get_node(&found_node, entry->inode);
            if (request.buffer_size < found_node.i_size) {
                return 2;
            }

            uint32_t direct_count =
                (found_node.i_blocks < 12) ? found_node.i_blocks : 12;

            for (uint32_t i = 0; i < direct_count; i++) {
                read_blocks(
                    (uint8_t *) request.buf + i * BLOCK_SIZE,
                    found_node.i_block[i], 1
                );
            }

            uint32_t read_count = direct_count;
            uint32_t remaining = found_node.i_blocks - direct_count;

            if (remaining > 0) {
                struct BlockBuffer indirect_buf;
                read_blocks(&indirect_buf, found_node.i_block[12], 1);

                uint32_t singly_count = (remaining < 128) ? remaining : 128;
                for (uint32_t j = 0; j < singly_count; j++) {
                    uint32_t block_num = ((uint32_t *) indirect_buf.buf)[j];

                    read_blocks(
                        request.buf + read_count * BLOCK_SIZE, block_num, 1
                    );

                    read_count++;
                }
                remaining -= singly_count;
            } // free bit

            if (remaining > 0) {
                struct BlockBuffer doubly_buf;
                read_blocks(&doubly_buf, found_node.i_block[13], 1);

                uint32_t num_singly_needed = (remaining + 127) / 128;
                for (uint32_t k = 0; k < num_singly_needed; k++) {
                    uint32_t singly_block_num =
                        ((uint32_t *) doubly_buf.buf)[k];

                    struct BlockBuffer singly_buf;
                    read_blocks(&singly_buf, singly_block_num, 1);

                    uint32_t this_count = (remaining < 128) ? remaining : 128;

                    for (uint32_t j = 0; j < this_count; j++) {
                        uint32_t block_num = ((uint32_t *) singly_buf.buf)[j];

                        read_blocks(
                            request.buf + read_count * BLOCK_SIZE, block_num, 1
                        );

                        read_count++;
                    }
                    remaining -= this_count;
                }
            }
            return 0;
        }
        offset += entry->rec_len;
    }
    return 3;
}

struct EXT2DirectoryEntry *find_insert_slot(
    struct BlockBuffer *parent_buf,
    uint8_t new_name_len
) {
    uint16_t needed = get_entry_record_len(new_name_len);
    uint16_t offset = 0;
    struct EXT2DirectoryEntry *entry = get_directory_entry(parent_buf, 0);

    while (offset + entry->rec_len < BLOCK_SIZE) {
        offset += entry->rec_len;
        entry = get_next_directory_entry(entry);
    }

    uint16_t minimal = get_entry_record_len(entry->name_len);
    uint16_t slack = entry->rec_len - minimal;

    if (slack < needed) {
        return NULL;
    }

    struct EXT2DirectoryEntry *new_entry = get_directory_entry(entry, minimal);
    new_entry->rec_len = slack;
    entry->rec_len = minimal;

    return new_entry;
}

int8_t write(struct EXT2DriverRequest *request) {
    struct EXT2Inode parent_node;
    get_node(&parent_node, request->parent_inode);

    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 2;
    }

    bool creating_dir = (request->buffer_size == 0);
    uint32_t desired_type = creating_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

    struct BlockBuffer parent_buf;
    read_blocks(&parent_buf, parent_node.i_block[0], 1);

    uint32_t offset = get_dir_first_child_offset(parent_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(parent_buf.buf, offset);

        if (entry->inode == 0) {
            break;
        }

        if ((entry->name_len == request->name_len) &&
            entry->file_type == desired_type &&
            memcmp(get_entry_name(entry), request->name, request->name_len) ==
                0) {
            return 1;
        }

        offset += entry->rec_len;
    }

    struct EXT2DirectoryEntry *new_entry =
        find_insert_slot(&parent_buf, request->name_len);

    if (new_entry == NULL) {
        return -1;
    }

    uint32_t new_inode = allocate_node();
    struct EXT2Inode new_node;
    memset(&new_node, 0, sizeof(new_node));

    if (creating_dir) {
        new_node.i_mode = EXT2_S_IFDIR;
        uint32_t block_num = find_one_free_block(inode_to_bgd(new_inode));
        new_node.i_block[0] = block_num;

        init_directory_table(&new_node, new_inode, request->parent_inode);
    } else {
        new_node.i_mode = EXT2_S_IFREG;
        new_node.i_size = request->buffer_size;
        new_node.i_blocks =
            (((request->buffer_size) + (BLOCK_SIZE) -1) / BLOCK_SIZE);

        allocate_node_blocks(request->buf, &new_node, inode_to_bgd(new_inode));
    }

    sync_node(&new_node, new_inode);

    new_entry->inode = new_inode;
    new_entry->name_len = request->name_len;
    new_entry->file_type = desired_type;
    memcpy(get_entry_name(new_entry), request->name, request->name_len);

    write_blocks(&parent_buf, parent_node.i_block[0], 1);
    return 0;
}

int8_t delete(struct EXT2DriverRequest request) {
    struct EXT2Inode parent_node;
    get_node(&parent_node, request.parent_inode);

    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 3;
    }

    struct BlockBuffer parent_buf;
    read_blocks(&parent_buf, parent_node.i_block[0], 1);

    uint32_t desired_type =
        request.is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

    struct EXT2DirectoryEntry *prev =
        get_next_directory_entry(get_directory_entry(parent_buf.buf, 0));

    uint32_t offset = get_dir_first_child_offset(parent_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(parent_buf.buf, offset);

        if (entry->inode == 0) {
            break;
        }

        if (entry->name_len == request.name_len &&
            entry->file_type == desired_type &&
            memcmp(get_entry_name(entry), request.name, request.name_len) ==
                0) {
            if (desired_type == EXT2_FT_DIR &&
                !is_directory_empty(entry->inode)) {
                return 2;
            }

            deallocate_node(entry->inode);
            prev->rec_len += entry->rec_len;
            write_blocks(&parent_buf, parent_node.i_block[0], 1);

            return 0;
        }
        prev = entry;
        offset += entry->rec_len;
    }
    return 1;
}
