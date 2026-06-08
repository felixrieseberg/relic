# CLAUDE.md

## Building and testing

- Host build + unit tests: `make -C build/posix test`
- Cross builds live under `build/{win32,win16,macppc,osxppc,xbox,wii}/`; see
  `docs/BUILDING.md` for toolchain setup.
- Build test binaries into a subdirectory of this repo (e.g. `dist/posix/`
  or `./tmp/`), not `/tmp` — keeps artefacts next to the source and avoids
  per-host temp-dir execution policies.

## Code conventions

- `src/core/` is portable hosted C: it may use the C standard library
  (`string.h`, `stdio.h`, …) but never OS or socket headers directly.
  Anything OS-specific goes through `src/plat/plat.h`. Dynamic allocation
  is avoided; the one deliberate exception (the `scroll=N` ring) is noted
  in `docs/DECISIONS.md`.
- Stay within the conservative C99 subset every target compiler accepts:
  no VLAs, no `//` comments, declarations at the top of each block.
  Designated initializers are avoided in hand-written code; the generated
  `trust_anchors.h` is the only exception (BearSSL's `brssl ta` emits them
  for the union members).
