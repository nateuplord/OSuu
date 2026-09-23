# Compiler & linker
ASM           	= nasm
LINK           	= ld
CC            	= gcc

# Directory
SOURCE_FOLDER 	= src
OUTPUT_FOLDER 	= bin
ISO_NAME      	= OSuu

# Flags
WARNING_CFLAG	= -Wall -Wextra -Werror
DEBUG_CFLAG		= -fshort-wchar -g
STRIP_CFLAG		= -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
C_FLAGS			= $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
ASM_FLAGS		= -f elf32 -g -F dwarf
LINK_FLAGS		= -T $(SOURCE_FOLDER)/linker.ld -melf_i386
ISO_FLAGS		= -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table

run: clean build
	@qemu-system-i386 -s -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

clean:
	@rm -rf *.o *.iso $(OUTPUT_FOLDER)/*

build: iso

kernel:
	@$(CC) $(C_FLAGS) $(SOURCE_FOLDER)/gdt.c -o                     $(OUTPUT_FOLDER)/gdt.o
	@$(ASM) $(ASM_FLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o    $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(CC) $(C_FLAGS) $(SOURCE_FOLDER)/kernel.c -o                  $(OUTPUT_FOLDER)/kernel.o
	@$(LINK) $(LINK_FLAGS) $(OUTPUT_FOLDER)/*.o -o                  $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generating ELF32...
	@rm -f *.o

iso: kernel
	@mkdir -p                       $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage $(ISO_FLAGS) -o    $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/
