// OSuu minimal C library for Doom: FILE streams.
//
// - Files are read-only, and always looked up in the root directory: any
//   directory part of the path ("./doom1.wad", "/doom/doom1.wad") is ignored,
//   since OSuu keeps everything Doom needs in `/`.
// - fopen() only finds the file and its size. The contents are read in one
//   go (OSuu's ext2 read is whole-file) the first time they're needed, so
//   M_FileExists()-style fopen+fclose probes stay cheap.
// - Opening for writing fails with ENOSYS: no savegames or config on OSuu
//   yet, and Doom copes with both missing.
// - stdout/stderr go to osuu_console_write() in doomgeneric_osuu.c, which
//   decides whether the text can be shown (it can't once Doom is drawing).
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel_api.h"

#define MAX_OPEN_FILES 8
#define FILE_BLOCK_SIZE 512u

struct OsuuFile {
    int in_use;
    int console;     // 1 = stdout, 2 = stderr, 0 = disk file
    char name[64];
    uint32_t size;
    uint32_t pos;
    uint8_t *data;   // NULL until first read
    int eof;
    int error;
};

/// Implemented in doomgeneric_osuu.c.
void osuu_console_write(const char *text, size_t len, int is_stderr);

static struct OsuuFile console_out = {.in_use = 1, .console = 1};
static struct OsuuFile console_err = {.in_use = 1, .console = 2};
static struct OsuuFile console_in = {.in_use = 1, .console = 3};
FILE *stdout = &console_out;
FILE *stderr = &console_err;
FILE *stdin = &console_in;

static struct OsuuFile open_files[MAX_OPEN_FILES];

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
    return slash ? slash + 1 : path;
}

/// Look `name` up in the root directory. Returns its inode (0 if absent) and
/// writes its size; directories don't count as files.
static uint32_t find_in_root(const char *name, uint32_t *size_out) {
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > 255)
        return 0;

    struct EXT2Inode root;
    struct BlockBuffer directory;
    get_node(&root, 1);
    read_blocks(directory.buf, root.i_block[0], 1);

    uint32_t offset = get_dir_first_child_offset(directory.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory.buf, offset);
        if (entry->inode == 0 || entry->rec_len == 0 ||
            entry->rec_len > BLOCK_SIZE - offset) {
            break;
        }
        if (entry->name_len == name_len &&
            memcmp(get_entry_name(entry), name, name_len) == 0) {
            struct EXT2Inode node;
            get_node(&node, entry->inode);
            if (node.i_mode & EXT2_S_IFDIR)
                return 0;
            *size_out = node.i_size;
            return entry->inode;
        }
        offset += entry->rec_len;
    }
    return 0;
}

/// Read the whole file into memory on first use.
static int ensure_loaded(struct OsuuFile *f) {
    if (f->data)
        return 1;
    // ext2 reads whole 512-byte blocks, so leave room for the last one.
    uint32_t capacity = (f->size + FILE_BLOCK_SIZE - 1) & ~(FILE_BLOCK_SIZE - 1);
    if (capacity == 0)
        capacity = FILE_BLOCK_SIZE;
    uint8_t *data = malloc(capacity);
    if (!data) {
        // Callers like W_AddFile don't check read lengths and misreport a
        // failed load (e.g. "doesn't have IWAD or PWAD id") - say why here.
        fprintf(stderr, "osuu: out of memory loading %s (%u bytes)\n",
                f->name, (unsigned) capacity);
        f->error = 1;
        errno = ENOMEM;
        return 0;
    }

    struct EXT2DriverRequest request = {
        .buf = data,
        .name = f->name,
        .name_len = (uint8_t) strlen(f->name),
        .parent_inode = 1,
        .buffer_size = capacity,
        .is_directory = false,
    };
    int8_t status = read(request);
    if (status != 0) {
        fprintf(stderr, "osuu: reading %s failed: ext2 read() returned %d "
                        "(size %u, buffer %u at %p)\n",
                f->name, status, (unsigned) f->size, (unsigned) capacity,
                (void *) data);
        free(data);
        f->error = 1;
        errno = EIO;
        return 0;
    }
    f->data = data;
    return 1;
}

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode || mode[0] != 'r' || strchr(mode, '+')) {
        errno = ENOSYS; // writing isn't supported
        return NULL;
    }

    const char *name = basename_of(path);
    uint32_t size = 0;
    if (strlen(name) >= sizeof(open_files[0].name) || !find_in_root(name, &size)) {
        errno = ENOENT;
        return NULL;
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        struct OsuuFile *f = &open_files[i];
        if (f->in_use)
            continue;
        memset(f, 0, sizeof(*f));
        f->in_use = 1;
        strcpy(f->name, name);
        f->size = size;
        return f;
    }
    errno = ENOMEM;
    return NULL;
}

int fclose(FILE *f) {
    if (!f || f->console)
        return 0;
    free(f->data);
    f->in_use = 0;
    return 0;
}

size_t fread(void *buf, size_t size, size_t count, FILE *f) {
    if (!f || f->console || size == 0 || count == 0)
        return 0;
    if (!ensure_loaded(f))
        return 0;

    size_t wanted = size * count;
    size_t left = f->pos < f->size ? f->size - f->pos : 0;
    if (wanted > left) {
        wanted = left - left % size; // only whole items, like real fread
        f->eof = 1;
    }
    memcpy(buf, f->data + f->pos, wanted);
    f->pos += (uint32_t) wanted;
    return wanted / size;
}

size_t fwrite(const void *buf, size_t size, size_t count, FILE *f) {
    if (!f || size == 0 || count == 0)
        return 0;
    if (f->console == 1 || f->console == 2) {
        osuu_console_write(buf, size * count, f->console == 2);
        return count;
    }
    f->error = 1; // files are read-only
    return 0;
}

int fseek(FILE *f, long offset, int whence) {
    if (!f || f->console)
        return -1;
    long base = whence == SEEK_SET ? 0
              : whence == SEEK_CUR ? (long) f->pos
              : whence == SEEK_END ? (long) f->size
                                   : -1;
    if (base < 0 || base + offset < 0) {
        errno = EINVAL;
        return -1;
    }
    f->pos = (uint32_t) (base + offset);
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    return (!f || f->console) ? -1 : (long) f->pos;
}

int feof(FILE *f) { return f ? f->eof : 1; }
int ferror(FILE *f) { return f ? f->error : 1; }
int fflush(FILE *f) {
    (void) f;
    return 0; // console writes aren't buffered
}

int fileno(FILE *f) {
    if (f == stdin) return 0;
    if (f == stdout) return 1;
    if (f == stderr) return 2;
    return f ? 3 + (int) (f - open_files) : -1;
}

int fgetc(FILE *f) {
    unsigned char c;
    return fread(&c, 1, 1, f) == 1 ? c : EOF;
}

char *fgets(char *buf, int size, FILE *f) {
    int n = 0;
    while (n < size - 1) {
        int c = fgetc(f);
        if (c == EOF)
            break;
        buf[n++] = (char) c;
        if (c == '\n')
            break;
    }
    if (n == 0)
        return NULL;
    buf[n] = '\0';
    return buf;
}

int fputc(int c, FILE *f) {
    char ch = (char) c;
    return fwrite(&ch, 1, 1, f) == 1 ? (unsigned char) c : EOF;
}

int fputs(const char *s, FILE *f) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}
