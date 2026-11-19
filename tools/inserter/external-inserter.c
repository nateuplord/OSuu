#include "driver/disk.h"
#include "fs/ext2.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *image_storage;
static uint8_t *file_buffer;
static uint8_t *read_buffer;
static bool is_replace;

static void usage(FILE *out) {
    fprintf(out, "Usage: inserter [-f] <file> <parent_inode> <storage.bin>\n");
    fprintf(out, "  -f  replace an existing file with the same name\n");
}

static const char *status_message(int8_t code) {
    switch (code) {
        case 0:
            return "Write success";
        case 1:
            return "Error: file/folder already exists";
        case 2:
            return "Error: invalid parent inode";
        default:
            return "Error: unknown filesystem error";
    }
}

static void check_block_range(
    uint32_t lba,
    uint8_t count
) {
    uint32_t offset = lba * BLOCK_SIZE;
    uint32_t length = (uint32_t) count * BLOCK_SIZE;

    if (offset > DISK_SPACE || length > DISK_SPACE - offset) {
        fprintf(
            stderr, "inserter: block range out of bounds (lba=%u, count=%u)\n",
            lba, count
        );
        exit(1);
    }
}

/* Host-side substitutes for the kernel's ATA PIO block I/O. */
void read_blocks(
    void *ptr,
    uint32_t logical_block_address,
    uint8_t block_count
) {
    check_block_range(logical_block_address, block_count);

    for (uint32_t i = 0; i < block_count; ++i) {
        memcpy(
            (uint8_t *) ptr + BLOCK_SIZE * i,
            image_storage + BLOCK_SIZE * (logical_block_address + i), BLOCK_SIZE
        );
    }
}

void write_blocks(
    const void *ptr,
    uint32_t logical_block_address,
    uint8_t block_count
) {
    check_block_range(logical_block_address, block_count);

    for (uint32_t i = 0; i < block_count; ++i) {
        memcpy(
            image_storage + BLOCK_SIZE * (logical_block_address + i),
            (uint8_t *) ptr + BLOCK_SIZE * i, BLOCK_SIZE
        );
    }
}

int main(
    int argc,
    char *argv[]
) {
    int argi = 1;
    int status = 1;
    FILE *disk_file = NULL;
    FILE *input_file = NULL;

    if (argc >= 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "-f") == 0) {
        is_replace = true;
        ++argi;
    }

    if (argc - argi < 3) {
        usage(stderr);
        goto cleanup;
    }

    char *target_path = argv[argi];
    char *parent_str = argv[argi + 1];
    char *storage_path = argv[argi + 2];

    size_t name_len = strlen(target_path);
    if (name_len == 0 || name_len > UINT8_MAX) {
        fprintf(stderr, "inserter: filename must be 1-255 bytes\n");
        goto cleanup;
    }

    unsigned int parent_inode;
    if (sscanf(parent_str, "%u", &parent_inode) != 1 || parent_inode == 0) {
        fprintf(stderr, "inserter: invalid parent inode '%s'\n", parent_str);
        goto cleanup;
    }

    image_storage = malloc(DISK_SPACE);
    file_buffer = malloc(DISK_SPACE);
    read_buffer = malloc(DISK_SPACE);
    if (image_storage == NULL || file_buffer == NULL || read_buffer == NULL) {
        fprintf(stderr, "inserter: out of host memory\n");
        goto cleanup;
    }

    disk_file = fopen(storage_path, "rb+");
    if (disk_file == NULL) {
        fprintf(stderr, "inserter: cannot open storage '%s'\n", storage_path);
        goto cleanup;
    }
    if (fread(image_storage, DISK_SPACE, 1, disk_file) != 1) {
        fprintf(
            stderr, "inserter: storage must be at least %u bytes\n", DISK_SPACE
        );
        goto cleanup;
    }

    input_file = fopen(target_path, "rb");
    if (input_file == NULL) {
        fprintf(stderr, "inserter: cannot open input file '%s'\n", target_path);
        goto cleanup;
    }
    if (fseek(input_file, 0, SEEK_END) != 0) {
        fprintf(stderr, "inserter: cannot seek input file\n");
        goto cleanup;
    }
    long file_size = ftell(input_file);
    if (file_size < 0 || (uint32_t) file_size > DISK_SPACE) {
        fprintf(stderr, "inserter: input file is too large\n");
        goto cleanup;
    }
    rewind(input_file);

    size_t filesize = (size_t) file_size;
    if (fread(file_buffer, 1, filesize, input_file) != filesize) {
        fprintf(stderr, "inserter: failed to read input file\n");
        goto cleanup;
    }

    initialize_filesystem_ext2();

    struct EXT2DriverRequest request = {
        .buf = file_buffer,
        .name = target_path,
        .name_len = (uint8_t) name_len,
        .parent_inode = parent_inode,
        .buffer_size = (uint32_t) filesize,
        .is_directory = false,
    };
    struct EXT2DriverRequest reqread = request;
    reqread.buf = read_buffer;

    int8_t retcode = write(&request);
    if (retcode == 1 && is_replace) {
        retcode = delete(request);
        if (retcode == 0) {
            retcode = write(&request);
        }
    }
    if (retcode != 0) {
        fprintf(stderr, "inserter: %s\n", status_message(retcode));
        goto cleanup;
    }

    if (read(reqread) != 0 || memcmp(read_buffer, file_buffer, filesize) != 0) {
        fprintf(stderr, "inserter: verification failed\n");
        goto cleanup;
    }

    if (fseek(disk_file, 0, SEEK_SET) != 0 ||
        fwrite(image_storage, DISK_SPACE, 1, disk_file) != 1) {
        fprintf(stderr, "inserter: failed to write storage\n");
        goto cleanup;
    }

    puts("Write success");
    status = 0;

cleanup:
    if (input_file != NULL) {
        fclose(input_file);
    }
    if (disk_file != NULL) {
        fclose(disk_file);
    }
    free(image_storage);
    free(file_buffer);
    free(read_buffer);
    return status;
}
