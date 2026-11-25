# ============================================================
# Toolchain
# ============================================================

ASM  := nasm
CC   := gcc
LINK := ld

# Upstream calls it grub-mkrescue; some distros rename it grub2-mkrescue.
GRUB_MKRESCUE := $(shell command -v grub-mkrescue 2>/dev/null || command -v grub2-mkrescue 2>/dev/null)

# ============================================================
# Paths
# ============================================================

SOURCE_FOLDER := src
HEADER_FOLDER := header
OUTPUT_FOLDER := bin
BOOT_FOLDER   := boot
USER_FOLDER   := userland

ISO_NAME  := OSuu
DISK_NAME := storage

SAMPLE_IMAGE := .kit/ch2/sample-image.bin

# ============================================================
# Flags
# ============================================================

WARNING_CFLAG := -Wall -Wextra -Werror
DEBUG_CFLAG   := -fshort-wchar -g
STRIP_CFLAG   := -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding

C_FLAGS := $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) \
	-I$(HEADER_FOLDER) \
	-m32 -mgeneral-regs-only -fno-omit-frame-pointer \
	-c -MMD -MP

ASM_FLAGS  := -f elf32 -g -F dwarf
LINK_FLAGS := -T $(SOURCE_FOLDER)/boot/linker.ld -melf_i386

USER_LINK_FLAGS := -T $(USER_FOLDER)/linker.ld -melf_i386 --oformat=binary

# ============================================================
# Kernel sources (auto-discovered)
# ============================================================

C_SRC   := $(shell find $(SOURCE_FOLDER) -name '*.c')
ASM_SRC := $(shell find $(SOURCE_FOLDER) -name '*.s')

OBJS := $(C_SRC:$(SOURCE_FOLDER)/%.c=$(OUTPUT_FOLDER)/%.o) \
	$(ASM_SRC:$(SOURCE_FOLDER)/%.s=$(OUTPUT_FOLDER)/%.o)

.PHONY: run test-sample debug clean build kernel iso disk \
	inserter \
	user-shell user-clock user-snake user-keytest user-memtest user-colortest user-doom \
	insert-shell insert-clock insert-snake insert-keytest insert-memtest insert-colortest insert-doom insert-doom-wad insert-userland insert-badapple

# ============================================================
# QEMU
# ============================================================

QEMU      := qemu-system-i386
QEMU_DISK := -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk,cache=directsync
QEMU_ISO  := -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

run: clean build
	@$(QEMU) -s $(QEMU_DISK) $(QEMU_ISO)

test-sample: clean build
	@$(QEMU) -s -S -drive file=$(SAMPLE_IMAGE),format=raw,if=ide,index=0,media=disk $(QEMU_ISO)

debug: clean build
	@$(QEMU) -s -S $(QEMU_DISK) $(QEMU_ISO)

# ============================================================
# Build
# ============================================================

build: iso insert-userland

clean:
	@rm -rf *.o *.iso \
		$(OUTPUT_FOLDER)/cpu/ \
		$(OUTPUT_FOLDER)/driver/ \
		$(OUTPUT_FOLDER)/stdlib/ \
		$(OUTPUT_FOLDER)/text/ \
		$(OUTPUT_FOLDER)/inserter \
		$(OUTPUT_FOLDER)/userprog/

$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(C_FLAGS) $< -o $@

$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.s
	@mkdir -p $(dir $@)
	@$(ASM) $(ASM_FLAGS) $< -o $@

kernel: $(OBJS)
	@$(LINK) $(LINK_FLAGS) $(OBJS) -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generating ELF32...

ISO_DIR := $(OUTPUT_FOLDER)/iso

iso: kernel
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel    $(ISO_DIR)/boot/
	@cp $(BOOT_FOLDER)/grub.cfg    $(ISO_DIR)/boot/grub/
	@if [ -z "$(GRUB_MKRESCUE)" ]; then \
		echo "error: grub-mkrescue or grub2-mkrescue not found"; \
		exit 1; \
	fi
	@$(GRUB_MKRESCUE) -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(ISO_DIR)
	@rm -r $(ISO_DIR)

-include $(OBJS:.o=.d)

# ============================================================
# Disk image and host-side inserter
# ============================================================

# 16 MiB of ext2 (DISK_SPACE in header/fs/ext2.h) followed by the swap area
# (header/memory/swap.h, 10 x 4 KiB slots); 17M leaves room for both.
# Changing DISK_SPACE means recreating the image: rm bin/storage.bin && make disk
disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 17M

HOST_CC     := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -g \
	-Wno-builtin-declaration-mismatch \
	-I$(HEADER_FOLDER)

INSERTER_SRC := tools/inserter/external-inserter.c \
	$(SOURCE_FOLDER)/fs/ext2.c \
	$(SOURCE_FOLDER)/stdlib/string.c

INSERTER_BIN := $(OUTPUT_FOLDER)/inserter

inserter: $(INSERTER_BIN)

$(INSERTER_BIN): $(INSERTER_SRC)
	@mkdir -p $(OUTPUT_FOLDER)
	@$(HOST_CC) $(HOST_CFLAGS) $(INSERTER_SRC) -o $@

# ============================================================
# User programs
# ============================================================

# Flat binaries linked at 0x0. Keep these source lists explicit: a wildcard
# would pull every program's main() into the shell binary.
USER_BIN       := $(OUTPUT_FOLDER)/userprog/shell
USER_CLOCK_BIN := $(OUTPUT_FOLDER)/userprog/clock
USER_SNAKE_BIN := $(OUTPUT_FOLDER)/userprog/snake
USER_KEYTEST_BIN := $(OUTPUT_FOLDER)/userprog/keytest
USER_MEMTEST_BIN := $(OUTPUT_FOLDER)/userprog/memtest
USER_COLORTEST_BIN := $(OUTPUT_FOLDER)/userprog/colortest

USER_SHARED_SRC := $(USER_FOLDER)/kernel_api.c $(USER_FOLDER)/string.c
USER_SHELL_SRC  := $(USER_FOLDER)/shell.c $(USER_FOLDER)/user-shell.c
USER_CLOCK_SRC  := $(USER_FOLDER)/clock.c
USER_SNAKE_SRC  := $(USER_FOLDER)/snake.c
USER_KEYTEST_SRC := $(USER_FOLDER)/keytest.c
USER_MEMTEST_SRC := $(USER_FOLDER)/memtest.c
USER_COLORTEST_SRC := $(USER_FOLDER)/colortest.c

USER_SHARED_OBJS := $(USER_SHARED_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_SHELL_OBJS  := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_SHELL_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_CLOCK_OBJS  := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_CLOCK_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_SNAKE_OBJS  := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_SNAKE_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_KEYTEST_OBJS := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_KEYTEST_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_MEMTEST_OBJS := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_MEMTEST_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_COLORTEST_OBJS := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_COLORTEST_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)

user-shell: $(USER_BIN)
user-clock: $(USER_CLOCK_BIN)
user-snake: $(USER_SNAKE_BIN)
user-keytest: $(USER_KEYTEST_BIN)
user-memtest: $(USER_MEMTEST_BIN)
user-colortest: $(USER_COLORTEST_BIN)

$(OUTPUT_FOLDER)/userprog/%.o: $(USER_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(C_FLAGS) -fno-pie $< -o $@

$(OUTPUT_FOLDER)/userprog/crt0.o: $(USER_FOLDER)/crt0.s
	@mkdir -p $(dir $@)
	@$(ASM) $(ASM_FLAGS) $< -o $@

$(USER_BIN): $(USER_SHELL_OBJS) $(USER_FOLDER)/linker.ld
	@$(LINK) $(USER_LINK_FLAGS) $(USER_SHELL_OBJS) -o $@
	@echo Linking shell flat binary...
	@size --target=binary $@

$(USER_CLOCK_BIN): $(USER_CLOCK_OBJS) $(USER_FOLDER)/linker.ld
	@$(LINK) $(USER_LINK_FLAGS) $(USER_CLOCK_OBJS) -o $@
	@echo Linking clock flat binary...
	@size --target=binary $@

$(USER_SNAKE_BIN): $(USER_SNAKE_OBJS) $(USER_FOLDER)/linker.ld
	@$(LINK) $(USER_LINK_FLAGS) $(USER_SNAKE_OBJS) -o $@
	@echo Linking snake flat binary...
	@size --target=binary $@

$(USER_KEYTEST_BIN): $(USER_KEYTEST_OBJS) $(USER_FOLDER)/linker.ld
	@$(LINK) $(USER_LINK_FLAGS) $(USER_KEYTEST_OBJS) -o $@
	@echo Linking keytest flat binary...
	@size --target=binary $@

$(USER_MEMTEST_BIN): $(USER_MEMTEST_OBJS) $(USER_FOLDER)/linker.ld
	@$(LINK) $(USER_LINK_FLAGS) $(USER_MEMTEST_OBJS) -o $@
	@echo Linking memtest flat binary...
	@size --target=binary $@

$(USER_COLORTEST_BIN): $(USER_COLORTEST_OBJS) $(USER_FOLDER)/linker.ld
	@$(LINK) $(USER_LINK_FLAGS) $(USER_COLORTEST_OBJS) -o $@
	@echo Linking colortest flat binary...
	@size --target=binary $@

# ============================================================
# Doom (doomgeneric, vendored in userland/doom/ - see its README)
# ============================================================

# Objects live in bin/doom/, which `clean` leaves alone: the engine is ~80
# files and rarely changes, so `make run` shouldn't recompile it every time.
# -MMD keeps header edits tracked. Remove bin/doom/ to force a full rebuild.
DOOM_FOLDER   := $(USER_FOLDER)/doom
DOOM_OBJ_DIR  := $(OUTPUT_FOLDER)/doom
DOOM_BIN      := $(OUTPUT_FOLDER)/userprog/doom
DOOM_LIBGCC   := $(shell $(CC) -m32 -print-libgcc-file-name)

# -nostdinc: only gcc's freestanding headers (stdint, stdarg, limits, ...)
# plus OSuu's minimal libc headers in osuu/include - no host glibc headers.
# -mgeneral-regs-only: no FPU/SSE state, same as the rest of userland; the
# engine's three float uses are patched out under #ifdef OSUU.
DOOM_BASE_CFLAGS := -m32 -Os -ffreestanding -fno-pie -fno-stack-protector \
	-mgeneral-regs-only -fno-asynchronous-unwind-tables -fno-omit-frame-pointer \
	-nostdinc -isystem $(shell $(CC) -m32 -print-file-name=include) \
	-I$(DOOM_FOLDER)/osuu/include -I$(DOOM_FOLDER) -I$(USER_FOLDER) -I$(HEADER_FOLDER) \
	-DOSUU -DCMAP256 -MMD -MP -c
# Upstream engine files: 1990s C, warnings aren't ours to fix.
DOOM_ENGINE_CFLAGS := $(DOOM_BASE_CFLAGS) -w
# OSuu's own files (libc shim + platform layer) are held to the usual bar.
DOOM_OSUU_CFLAGS   := $(DOOM_BASE_CFLAGS) -Wall -Wextra -Werror \
	-Wno-builtin-declaration-mismatch

DOOM_OSUU_SRC   := $(DOOM_FOLDER)/doomgeneric_osuu.c $(wildcard $(DOOM_FOLDER)/osuu/*.c)
DOOM_ENGINE_SRC := $(filter-out $(DOOM_FOLDER)/doomgeneric_osuu.c,$(wildcard $(DOOM_FOLDER)/*.c))
DOOM_OBJS := $(DOOM_ENGINE_SRC:$(DOOM_FOLDER)/%.c=$(DOOM_OBJ_DIR)/%.o) \
	$(DOOM_OSUU_SRC:$(DOOM_FOLDER)/%.c=$(DOOM_OBJ_DIR)/%.o)

$(DOOM_OBJ_DIR)/osuu/%.o: $(DOOM_FOLDER)/osuu/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(DOOM_OSUU_CFLAGS) $< -o $@

$(DOOM_OBJ_DIR)/doomgeneric_osuu.o: $(DOOM_FOLDER)/doomgeneric_osuu.c
	@mkdir -p $(dir $@)
	@$(CC) $(DOOM_OSUU_CFLAGS) $< -o $@

$(DOOM_OBJ_DIR)/%.o: $(DOOM_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(DOOM_ENGINE_CFLAGS) $< -o $@

-include $(DOOM_OBJS:.o=.d)

# crt0 first (the linker script puts its .text at 0x0), then kernel_api.o for
# the syscall wrappers - NOT userland's string.o: the shim has its own mem*.
# libgcc supplies 64-bit division (__divdi3 & co) for FixedDiv.
$(DOOM_BIN): $(OUTPUT_FOLDER)/userprog/crt0.o $(OUTPUT_FOLDER)/userprog/kernel_api.o \
		$(DOOM_OBJS) $(USER_FOLDER)/linker.ld
	@mkdir -p $(dir $@)
	@$(LINK) $(USER_LINK_FLAGS) $(OUTPUT_FOLDER)/userprog/crt0.o \
		$(OUTPUT_FOLDER)/userprog/kernel_api.o $(DOOM_OBJS) $(DOOM_LIBGCC) -o $@
	@echo Linking doom flat binary...
	@size --target=binary $@

user-doom: $(DOOM_BIN)

# ============================================================
# Insert user programs / assets into the ext2 root directory
# ============================================================

INSERT_USER := cd $(OUTPUT_FOLDER)/userprog && ../inserter -f
INSERT_MISC := cd misc && ../$(OUTPUT_FOLDER)/inserter -f

insert-shell: $(USER_BIN) inserter
	@echo Inserting shell into root directory...
	@$(INSERT_USER) shell 1 ../$(DISK_NAME).bin

insert-clock: $(USER_CLOCK_BIN) inserter
	@echo Inserting clock into root directory...
	@$(INSERT_USER) clock 1 ../$(DISK_NAME).bin

insert-snake: $(USER_SNAKE_BIN) inserter
	@echo Inserting snake into root directory...
	@$(INSERT_USER) snake 1 ../$(DISK_NAME).bin

insert-keytest: $(USER_KEYTEST_BIN) inserter
	@echo Inserting keytest into root directory...
	@$(INSERT_USER) keytest 1 ../$(DISK_NAME).bin

insert-memtest: $(USER_MEMTEST_BIN) inserter
	@echo Inserting memtest into root directory...
	@$(INSERT_USER) memtest 1 ../$(DISK_NAME).bin

insert-doom: $(DOOM_BIN) inserter
	@echo Inserting doom into root directory...
	@$(INSERT_USER) doom 1 ../$(DISK_NAME).bin

insert-colortest: $(USER_COLORTEST_BIN) inserter
	@echo Inserting colortest into root directory...
	@$(INSERT_USER) colortest 1 ../$(DISK_NAME).bin

insert-userland: $(USER_BIN) $(USER_CLOCK_BIN) $(USER_SNAKE_BIN) $(USER_KEYTEST_BIN) $(USER_MEMTEST_BIN) $(USER_COLORTEST_BIN) $(DOOM_BIN) inserter insert-badapple insert-doom-wad
	@echo Inserting shell into root directory...
	@$(INSERT_USER) shell 1 ../$(DISK_NAME).bin
	@echo Inserting clock into root directory...
	@$(INSERT_USER) clock 1 ../$(DISK_NAME).bin
	@echo Inserting snake into root directory...
	@$(INSERT_USER) snake 1 ../$(DISK_NAME).bin
	@echo Inserting keytest into root directory...
	@$(INSERT_USER) keytest 1 ../$(DISK_NAME).bin
	@echo Inserting memtest into root directory...
	@$(INSERT_USER) memtest 1 ../$(DISK_NAME).bin
	@echo Inserting colortest into root directory...
	@$(INSERT_USER) colortest 1 ../$(DISK_NAME).bin
	@echo Inserting doom into root directory...
	@$(INSERT_USER) doom 1 ../$(DISK_NAME).bin

# Doom's game data. Not in git (see misc/doom/README.md), so skip quietly
# when it's absent - everything else still builds and runs without it.
DOOM_WAD := misc/doom/doom1.wad

insert-doom-wad: inserter
	@if [ -f $(DOOM_WAD) ]; then \
		echo Inserting doom1.wad into root directory...; \
		cd misc/doom && ../../$(OUTPUT_FOLDER)/inserter -f doom1.wad 1 ../../$(OUTPUT_FOLDER)/$(DISK_NAME).bin; \
	else \
		echo "Skipping doom1.wad (not found - see misc/doom/README.md)"; \
	fi

# badapple reads these from the root directory at runtime.
insert-badapple: inserter misc/palette.bin misc/frames.bin
	@echo Inserting Bad Apple assets into root directory...
	@$(INSERT_MISC) palette.bin 1 ../$(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@$(INSERT_MISC) frames.bin  1 ../$(OUTPUT_FOLDER)/$(DISK_NAME).bin
