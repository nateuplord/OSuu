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
USER_HEADERS  := $(USER_FOLDER)/header

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
	user-shell user-clock user-snake \
	insert-shell insert-clock insert-snake insert-userland insert-badapple

# ============================================================
# QEMU
# ============================================================

QEMU      := qemu-system-i386
QEMU_DISK := -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk,cache=directsync
QEMU_ISO  := -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

# Enable the emulated SB16 and PC speaker. Override for headless runs, e.g.
#   make run QEMU_AUDIO="-audiodev wav,id=snd0,path=/tmp/audio.wav -device sb16,audiodev=snd0"
QEMU_AUDIO ?= -audiodev pipewire,id=snd0 -machine pcspk-audiodev=snd0 -device sb16,audiodev=snd0

run: clean build
	@$(QEMU) $(QEMU_DISK) $(QEMU_ISO) $(QEMU_AUDIO)

test-sample: clean build
	@$(QEMU) -s -S -drive file=$(SAMPLE_IMAGE),format=raw,if=ide,index=0,media=disk $(QEMU_ISO) $(QEMU_AUDIO)

debug: clean build
	@$(QEMU) -s -S $(QEMU_DISK) $(QEMU_ISO) $(QEMU_AUDIO)

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

disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 5M

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

USER_SHARED_SRC := $(USER_FOLDER)/kernel_api.c $(USER_FOLDER)/string.c
USER_SHELL_SRC  := $(USER_FOLDER)/shell.c $(USER_FOLDER)/user-shell.c
USER_CLOCK_SRC  := $(USER_FOLDER)/clock.c
USER_SNAKE_SRC  := $(USER_FOLDER)/snake.c

USER_SHARED_OBJS := $(USER_SHARED_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_SHELL_OBJS  := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_SHELL_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_CLOCK_OBJS  := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_CLOCK_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)
USER_SNAKE_OBJS  := $(USER_SHARED_OBJS) $(OUTPUT_FOLDER)/userprog/crt0.o \
	$(USER_SNAKE_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)

user-shell: $(USER_BIN)
user-clock: $(USER_CLOCK_BIN)
user-snake: $(USER_SNAKE_BIN)

$(OUTPUT_FOLDER)/userprog/%.o: $(USER_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(C_FLAGS) -I$(USER_HEADERS) -fno-pie $< -o $@

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

insert-userland: $(USER_BIN) $(USER_CLOCK_BIN) $(USER_SNAKE_BIN) inserter insert-badapple
	@echo Inserting shell into root directory...
	@$(INSERT_USER) shell 1 ../$(DISK_NAME).bin
	@echo Inserting clock into root directory...
	@$(INSERT_USER) clock 1 ../$(DISK_NAME).bin
	@echo Inserting snake into root directory...
	@$(INSERT_USER) snake 1 ../$(DISK_NAME).bin

# badapple reads these from the root directory at runtime.
insert-badapple: inserter misc/palette.bin misc/frames.bin misc/music.bin
	@echo Inserting Bad Apple assets into root directory...
	@$(INSERT_MISC) palette.bin 1 ../$(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@$(INSERT_MISC) frames.bin  1 ../$(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@$(INSERT_MISC) music.bin   1 ../$(OUTPUT_FOLDER)/$(DISK_NAME).bin
