# Compiler & linker
ASM  = nasm
LINK = ld
CC   = gcc

# Directory
SOURCE_FOLDER = src
HEADER_FOLDER = header
OUTPUT_FOLDER = bin
BOOT_FOLDER   = boot
ISO_NAME      = OSuu

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
C_FLAGS       = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -I$(HEADER_FOLDER) -m32 -mgeneral-regs-only -fno-omit-frame-pointer -c -MMD -MP
ASM_FLAGS     = -f elf32 -g -F dwarf
LINK_FLAGS    = -T $(SOURCE_FOLDER)/boot/linker.ld -melf_i386

# Auto-discover sources
C_SRC   := $(shell find $(SOURCE_FOLDER) -name '*.c')
ASM_SRC := $(shell find $(SOURCE_FOLDER) -name '*.s')
OBJS    := $(C_SRC:$(SOURCE_FOLDER)/%.c=$(OUTPUT_FOLDER)/%.o) \
           $(ASM_SRC:$(SOURCE_FOLDER)/%.s=$(OUTPUT_FOLDER)/%.o)

.PHONY: run clean build kernel iso disk test-sample inserter user-shell insert-shell

SAMPLE_IMAGE = .kit/ch2/sample-image.bin

run: clean build
# @qemu-system-i386 -s -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
	@qemu-system-i386 -s -drive file=$(OUTPUT_FOLDER)/storage.bin,format=raw,if=ide,index=0,media=disk,cache=directsync -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

test-sample: clean build
	@qemu-system-i386 -s -S -drive file=$(SAMPLE_IMAGE),format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

clean:
# @rm -rf $(OUTPUT_FOLDER)/*
	@rm -rf *.o *.iso $(OUTPUT_FOLDER)/cpu/ $(OUTPUT_FOLDER)/driver/ $(OUTPUT_FOLDER)/stdlib/ $(OUTPUT_FOLDER)/text/ $(OUTPUT_FOLDER)/inserter \
		$(OUTPUT_FOLDER)/userprog/

build: iso

# Pattern rules
$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(C_FLAGS) $< -o $@

$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.s
	@mkdir -p $(dir $@)
	@$(ASM) $(ASM_FLAGS) $< -o $@

kernel: $(OBJS)
	@$(LINK) $(LINK_FLAGS) $(OBJS) -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generating ELF32...

iso: kernel
	@mkdir -p                       $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp $(BOOT_FOLDER)/grub.cfg     $(OUTPUT_FOLDER)/iso/boot/grub/
	@grub2-mkrescue -o              $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r                          $(OUTPUT_FOLDER)/iso/

-include $(OBJS:.o=.d)

# Disk stuffs
DISK_NAME = storage

disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 5M

# Host-side tool: inject a file into the ext2 disk image
HOST_CC       = gcc
HOST_CFLAGS   = -std=c11 -Wall -Wextra -g -Wno-builtin-declaration-mismatch -I$(HEADER_FOLDER)
INSERTER_SRC  = tools/inserter/external-inserter.c \
                $(SOURCE_FOLDER)/fs/ext2.c \
                $(SOURCE_FOLDER)/stdlib/string.c

inserter: bin/inserter

bin/inserter: $(INSERTER_SRC)
	@mkdir -p $(OUTPUT_FOLDER)
	@$(HOST_CC) $(HOST_CFLAGS) $(INSERTER_SRC) -o $@

# User program: flat binary linked at 0x0, inserted as a file in the ext2 image
USER_FOLDER = userland
USER_BIN    = $(OUTPUT_FOLDER)/userprog/shell
USER_C_SRC  := $(wildcard $(USER_FOLDER)/*.c)
# USER_C_SRC  := $(USER_FOLDER)/user-shell.c # for 4.1 testing
USER_OBJS   := $(OUTPUT_FOLDER)/userprog/crt0.o \
               $(USER_C_SRC:$(USER_FOLDER)/%.c=$(OUTPUT_FOLDER)/userprog/%.o)

$(OUTPUT_FOLDER)/userprog/%.o: $(USER_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(C_FLAGS) -fno-pie $< -o $@

$(OUTPUT_FOLDER)/userprog/crt0.o: $(USER_FOLDER)/crt0.s
	@mkdir -p $(dir $@)
	@$(ASM) $(ASM_FLAGS) $< -o $@

$(USER_BIN): $(USER_OBJS) $(USER_FOLDER)/linker.ld
	@$(LINK) -T $(USER_FOLDER)/linker.ld -melf_i386 --oformat=binary \
		$(USER_OBJS) -o $@
	@echo Linking user object files and generating flat binary...
	@size --target=binary $@

user-shell: $(USER_BIN)

insert-shell: $(USER_BIN) inserter
	@echo Inserting shell into root directory...
	@cd $(OUTPUT_FOLDER)/userprog && ../inserter -f shell 1 ../$(DISK_NAME).bin


# process debug
debug: clean build insert-shell
	@qemu-system-i386 -s -S -drive file=$(OUTPUT_FOLDER)/storage.bin,format=raw,if=ide,index=0,media=disk,cache=directsync -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
