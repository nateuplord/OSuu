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
| `doomgeneric_osuu.c` | The `DG_*` platform hooks wired to OSuu syscalls, the scancode → `doomkeys.h` map, `main()`, and `exit()`, which hands the screen back and returns to the shell, showing the error if Doom failed |
| `osuu/libc.c` | Strings, ctype, number parsing, `malloc` (bump allocator over a 32 MiB `map_region` at `0x30000000`), stubs |
| `osuu/printf.c` | `printf` family (`%d %i %u %x %X %o %c %s %p %%`, flags/width/precision) and a small `sscanf` |
| `osuu/file.c` | Read-only `FILE` streams over the root directory, loaded whole on first read; stdout/stderr go to the console |
| `osuu/include/` | The C headers. Built with `-nostdinc`, so these plus gcc's freestanding headers are the only system headers |

Build: `make user-doom`. Objects go in `bin/doom/`, which `make clean` keeps.

The game data (`doom1.wad`) isn't here. See [`misc/doom/README.md`](../../misc/doom/README.md).

## Local changes to upstream files

Keep edits to upstream files minimal and wrap each one in `#ifdef OSUU`, so
that a `diff` against upstream shows exactly what OSuu changed. That makes
pulling a newer doomgeneric easy. List each change here:

| File | Change | Why |
|---|---|---|
| `v_video.c` | `V_DrawMouseSpeedBox`: always returns before the `fabs(mouse_acceleration - 1)` check | No mouse on OSuu, and no floating point in the build |
| `m_config.c` | `SaveDefaultCollection`: `DEFAULT_FLOAT` writes `0` instead of `%f` | No float formatting. The config is never written anyway |
| `m_config.c` | `SetVariable`: `DEFAULT_FLOAT` keeps the compiled-in default instead of `atof()` | No floats, and no config file to load |
| `g_game.c` | `G_CheckDemoStatus`: the timedemo FPS uses integer maths and `%i` | No floats |

All four exist so the engine can be built with `-mgeneral-regs-only`, like the
rest of OSuu userland, with no FPU state to save on context switches. Any new
float use fails to link, which is intentional: 32-bit libgcc has no
single-precision soft-float helpers.
