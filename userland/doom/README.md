# doomgeneric (vendored)

The Doom engine OSuu's `doom` program is built from.

- **Upstream:** https://github.com/ozkl/doomgeneric
- **Commit:** `e257a09527062bfb986b6e5e0780839b6cbd5b71` (2026-10-11)
- **License:** GPLv2, see [`LICENSE`](LICENSE). Doom's source was released
  by id Software under the GPL, and doomgeneric keeps that license.

## What's here

Only the portable core: the 80 `.c` files upstream's `Makefile` builds, minus
every platform backend, plus all of its headers. Not copied: SDL, Xlib,
Allegro, Win32, Emscripten and the other `doomgeneric_*.c` backends, the
`i_sdl*` / `i_allegro*` sound and music files, and the upstream
Makefiles/project files.

OSuu's own pieces:

| File | What |
|---|---|
| `doomgeneric_osuu.c` | The `DG_*` platform hooks wired to OSuu syscalls *(Phase 2, to come)* |
| `../libc/` or similar | Minimal C library the engine links against *(Phase 2, to come)* |

The game data (`doom1.wad`) isn't here. See [`misc/doom/README.md`](../../misc/doom/README.md).

## Local changes to upstream files

Keep edits to upstream files minimal and wrap each one in `#ifdef OSUU`, so
that a `diff` against upstream shows exactly what OSuu changed. That makes
pulling a newer doomgeneric easy. List each change here:

| File | Change | Why |
|---|---|---|
| *(none yet)* | | |

Planned (from the Phase 0 fit check): patch out the three float uses so the
build can keep `-mgeneral-regs-only` (no FPU state to save on context
switches):
- `v_video.c` (mouse acceleration)
- `m_config.c` (loading float config values)
- `g_game.c` (timedemo FPS printout)
