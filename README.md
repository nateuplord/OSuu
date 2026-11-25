![Credit: 朧月](assets/mascot.jpg)

# Tugas Besar IF2130 Sistem Operasi - 2026/2027 | OSuu

## Welcome To OSuu!

The bestest not yet rhythm OS

## Daftar Anggota

- 13525058 / Farish Firstian Erifiawan
- 13525061 / Rifqi Irfan Indrawan
- 13525065 / Christopher Hendrik Gunawan
- 13525083 / Natanael Chris Fabian Santoso

## Daftar Isi

```
root
├───.kit
├───assets
├───bin
├───boot
├───header
├───src
├───.clang-format
├───.gitignore
├───Makefile
└───README.md
```

## Cara Run

### 1. Install dependency

OSuu di-build di Linux (atau WSL) untuk target i386 (32-bit).

| Tool                                                    | Kegunaan                                |
| ------------------------------------------------------- | --------------------------------------- |
| `gcc` (dengan dukungan `-m32`)                          | Compile kernel dan program userland     |
| `nasm`                                                  | Assemble file `.s`                      |
| `ld`, `size` (binutils)                                 | Linking kernel dan flat binary userland |
| `grub-mkrescue` / `grub2-mkrescue`, `xorriso`, `mtools` | Membuat ISO bootable                    |
| `qemu-system-i386`, `qemu-img`                          | Menjalankan OS dan membuat disk image   |
| `make`                                                  | Build system                            |

Contoh instalasi:

```sh
# Arch Linux
sudo pacman -S base-devel nasm grub libisoburn mtools qemu-full

# Debian / Ubuntu / WSL
sudo apt install build-essential gcc-multilib nasm grub-pc-bin grub-common xorriso mtools qemu-system-x86 qemu-utils

# Fedora
sudo dnf install @development-tools glibc-devel.i686 nasm grub2-tools grub2-pc-modules xorriso mtools qemu-system-x86 qemu-img
```

Makefile otomatis memakai `grub-mkrescue` (atau `grub2-mkrescue` di Fedora).

### 2. Buat disk image (sekali saja)

Disk image (`bin/storage.bin`) tidak ikut di-commit, jadi buat dulu sebelum run pertama kali:

```sh
make disk
```

Disk ini masih kosong. Formatnya sebagai ext2 dibuat otomatis oleh `inserter` saat `make run` pertama kali memasukkan file ke dalamnya.

### 3. Build dan jalankan

```sh
make run
```

`make run` akan:

1. Membersihkan hasil build sebelumnya (`make clean`).
2. Compile kernel dan membuat `bin/OSuu.iso`.
3. Compile program userland (`shell`, `clock`, `snake`) lalu memasukkannya ke root directory disk, beserta aset Bad Apple (`misc/palette.bin`, `misc/frames.bin`).
4. Menjalankan QEMU dengan ISO tersebut dan `bin/storage.bin` sebagai harddisk.

### 4. Pakai shell-nya

Setelah boot, OSuu langsung masuk ke shell. Beberapa command yang bisa dicoba:

| Command                       | Fungsi                                              |
| ----------------------------- | --------------------------------------------------- |
| `help`                        | Daftar semua command                                |
| `ls`, `cd`, `pwd`             | Navigasi direktori                                  |
| `mkdir`, `cp`, `mv`, `rm`     | Kelola file dan folder                              |
| `cat`, `grep`, `find`, `echo` | Baca dan cari isi file                              |
| `clear`                       | Bersihkan layar                                     |
| `ps`, `kill <pid>`            | Lihat dan hentikan process                          |
| `clock`                       | Jalankan jam di background                          |
| `exec <program>`              | Jalankan program userland, contoh `exec snake`      |
| `badapple`                    | Rule 86: If it exists, you can play Bad Apple on it |

Tambahkan `--help` ke sebuah command untuk melihat cara pakainya, contoh `ls --help`.

### Target `make` lainnya

| Target                                                | Fungsi                                                                                     |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| `make debug`                                          | Sama seperti `make run`, tapi QEMU berhenti di awal (`-S`) dan menunggu GDB di port `1234` |
| `make build`                                          | Build ISO dan isi disk tanpa menjalankan QEMU                                              |
| `make clean`                                          | Hapus hasil build                                                                          |
| `make disk`                                           | Buat ulang `bin/storage.bin` kosong (isi disk lama hilang)                                 |
| `make insert-shell` / `insert-clock` / `insert-snake` | Compile ulang dan masukkan satu program saja ke disk                                       |

### Troubleshooting

- **`grub-mkrescue or grub2-mkrescue not found`**: install GRUB (lihat langkah 1). Di Debian/Ubuntu butuh paket `grub-pc-bin`, bukan cuma `grub-common`.
- **ISO gagal dibuat dengan error `xorriso` atau `mformat`**: install `xorriso` dan `mtools`.
- **`inserter: ... Error`** saat `make run`: disk image kemungkinan rusak atau format lama. Buat ulang dengan `rm bin/storage.bin && make disk`, lalu `make run` lagi.
- **QEMU tidak bisa dibuka di WSL**: pastikan WSLg aktif (Windows 11), atau jalankan dari Linux native.

## Fitur yang Dibuat

- Shell
- Process
- Round Robin Scheduler with Priority
- Keyboard I/O wait queue
- Process exit, `getpid`, and `yield` syscalls
