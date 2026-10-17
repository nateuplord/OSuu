# Compiler & linker
ASM  = nasm
LINK = ld
CC   = gcc

# Directory
SOURCE_FOLDER = src
HEADER_FOLDER = header
OUTPUT_FOLDER = bin
ISO_NAME      = OSuu

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
C_FLAGS       = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -I$(HEADER_FOLDER) -m32 -c
ASM_FLAGS     = -f elf32 -g -F dwarf
LINK_FLAGS    = -T $(SOURCE_FOLDER)/linker.ld -melf_i386
ISO_FLAGS     = -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table

# Auto-discover sources
C_SRC   := $(shell find $(SOURCE_FOLDER) -name '*.c')
ASM_SRC := $(shell find $(SOURCE_FOLDER) -name '*.s')
OBJS    := $(C_SRC:$(SOURCE_FOLDER)/%.c=$(OUTPUT_FOLDER)/%.o) \
           $(ASM_SRC:$(SOURCE_FOLDER)/%.s=$(OUTPUT_FOLDER)/%.o)

.PHONY: run clean build kernel iso disk

run: clean build
#	@qemu-system-i386 -s -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
	@qemu-system-i386 -s -S -drive file=$(OUTPUT_FOLDER)/storage.bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

clean:
#	@rm -rf $(OUTPUT_FOLDER)/*
	@rm -f *.o *.iso $(OUTPUT_FOLDER)/cpu $(OUTPUT_FOLDER)/driver $(OUTPUT_FOLDER)/stdlib $(OUTPUT_FOLDER)/text

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
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage $(ISO_FLAGS) -o    $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r                          $(OUTPUT_FOLDER)/iso/

-include $(OBJS:.o=.d)

# Disk stuffs
DISK_NAME      = storage

disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M
