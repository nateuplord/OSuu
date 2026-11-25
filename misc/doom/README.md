# Doom WAD

OSuu's Doom port (`userland/doom/`) needs the game data, the **shareware
`doom1.wad`** (Doom v1.9, Episode 1). It's not committed to this repo, since
it's 4 MiB of binary data. Put it **in this folder** before you build if you
want to play Doom on OSuu:

```
misc/doom/doom1.wad
```

`make run` / `make insert-userland` inserts it into the disk image when it's
here and skips it with a note when it isn't, so the rest of OSuu builds and
runs fine without it. Then, in the OSuu shell: `exec doom`.

## Getting it

The shareware WAD is freely distributable. John Carmack's 1999 permission is
reproduced in Debian's `doom-wad-shareware` copyright file. Any of these
work:

**Direct download** (works on any distro, from the repo root). This
fetches Debian's `doom-wad-shareware` package and pulls just the WAD out of
it. It needs `curl`, `ar` (binutils, which you already have for building
OSuu), `tar` and `xz`:

```sh
curl -fL -o /tmp/doom-wad.deb \
  https://deb.debian.org/debian/pool/non-free/d/doom-wad-shareware/doom-wad-shareware_1.9.fixed-5_all.deb
ar p /tmp/doom-wad.deb data.tar.xz | tar -xJO ./usr/share/games/doom/doom1.wad > misc/doom/doom1.wad
rm /tmp/doom-wad.deb
```

Keep the `-f` on curl: without it, a dead link saves the server's HTML error
page as `doom1.wad`. The ibiblio/SliTaz mirror Gentoo lists returned exactly
that, a 341-byte "404 Not Found" page, as of 2026-11-25.

**From a distro package**, then copy the file here:

| Distro | Install | Find where it put the file |
|---|---|---|
| Arch (AUR) | `yay -S doom1-wad` (or your AUR helper) | `pacman -Ql doom1-wad \| grep -i '\.wad$'` |
| Debian / Ubuntu | `sudo apt install doom-wad-shareware` (non-free / multiverse) | `dpkg -L doom-wad-shareware \| grep -i '\.wad$'` |

Then copy that path here, e.g. `cp <path printed above> misc/doom/doom1.wad`.

## Check you have the right file

```sh
md5sum misc/doom/doom1.wad
# f0cefca49926d00903cf57551d901abe  misc/doom/doom1.wad
```

It should be **4,196,020 bytes**. That's the v1.9 shareware release the port
is built and tested against. Other WADs (other shareware versions, or the
registered/commercial `doom.wad` / `doom2.wad`) aren't supported yet. They
also may not fit: OSuu's disk is 16 MiB in total, and the registered
`doom.wad` alone is about 11 MiB.
