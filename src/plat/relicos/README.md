# RelicOS

A bootable target where the agent loop **is** the operating system: the
`relicos` binary is PID 1. It mounts the filesystems, brings up the network,
owns the framebuffer, and runs the conversation loop. busybox and tcc are
in the image as libraries it shells out to; nothing else sits between the
kernel and the agent. Everything visual on the canvas is something the
model compiled and ran.

Lives alongside the other platform backends in `src/plat/`; the retro
build targets in `build/*` are unaffected.

## Prereqs

`qemu-system-x86_64`, `docker`, `curl`, `cpio`, `gzip`, `nc`. macOS or
Linux host.

## Targets

```sh
make relicos                          # from repo root: build initramfs
make -C src/plat/relicos test-fast    # smoke + tcc compile-in-guest + input loop  (~11 s)
make -C src/plat/relicos test-all     # + net, /data persistence, fb, relic TLS

export ANTHROPIC_API_KEY=sk-ant-...
make -C src/plat/relicos ui           # opens a QEMU window: chat at the bottom, type, Enter
make -C src/plat/relicos test-agent   # headless live API round-trip
./src/plat/relicos/scripts/once.sh "draw a colour gradient on the canvas" demo
                                      # one-shot, screendump to out/demo.png

make -C src/plat/relicos relicos      # rebuild the static binary via Docker
make -C src/plat/relicos screenshot   # headless boot, screendump to out/screenshot.png
make -C src/plat/relicos distclean    # nuke downloads + builds
```

API key is passed via QEMU `fw_cfg` (file=, mode 0600, deleted on exit) →
`/sys/firmware/qemu_fw_cfg/.../raw`; never on argv or kernel cmdline. For
a real image, drop it in `rootfs/RELIC.CFG` (gitignored).

## What's in the image

| piece | size |
|---|---|
| vmlinuz-virt (alpine 3.22) | 11 MB |
| initramfs (busybox + modules + tcc + headers + relicos) | ~14 MB gz -1 |
| `relicos` static (relic core + bearssl + init + chat shell) | 1.2 MB |

`relicos` is `relicos/*.{cpp,h}`: PID 1 boot + cmdline dispatch
(`init.cpp`), fbdev (`fb.cpp`), embedded 8×16 font + text blitter
(`textfb.cpp`), evdev keyboard (`evdev.cpp`), scrolling log
(`logview.cpp`), the agent loop over `src/core/*` (`agent_ui.cpp`), and
`ui.cpp` tying it together. The only third-party code here is the
Terminus 8×16 font bitmap in `font8x16.h` (SIL OFL 1.1; see `NOTICES`).

## Layout

```
┌────────────────────────────────────────────────────────────┐
│  CANVAS — /dev/fb0, full screen (CANVAS_H == FB_H).        │
│  Only programs the model compiles and runs draw here       │
│  (mmap /dev/fb0, write XRGB words).                        │
│                                                            │
│ ┌ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┐  │
│    console overlay (chat log + > input) — ` toggles.       │
│ │  snapshot-on-show / restore-on-hide; always on top.   │  │
│ └ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┘  │
└────────────────────────────────────────────────────────────┘
```

`FB_W`, `FB_H`, `CANVAS_H` are exported to the model's shell.

## Tools (root, unrestricted by design)

Same set as the relic CLI — `Bash`, `Read`, `Write`, `Edit`, `LS`,
`Grep`, `Glob` — plus RelicOS extras `Spawn`, `Windows`, `Agent`,
`Agents`, `AgentResult`. The model writes C, compiles with `tcc`, runs
it, draws to the canvas, persists under `/data`.

There is no host-side window manager. `rootfs/usr/include/relicos.h` is
the platform ABI the model `#include`s: `ros_open()` mmaps the fb and
reads `FB_W`/`CANVAS_H` from env, `ros_window()` picks a free rect and
appends it to `/tmp/windows` so later programs (and the `Windows` tool)
can see what's already placed, `ros_fill()`/`ros_put()` draw clipped to
the canvas, `ROS_DATA` names the persistent root. Layout is still the
model's problem; the registry is just bookkeeping.

## Threat model

The agent runs as root with no in-guest sandbox — that's the point. The
boundary is the **VM**: user-mode net, no host mounts, throwaway qcow2.
Don't attach real disks or bridge real networks.

## How tests self-verify

`init.cpp` reads `relicos.test=...` from the kernel cmdline, runs the
check, prints `RELICOS_*_PASS`/`FAIL`, then powers off. `scripts/await.sh`
launches QEMU, owns the PID, greps the serial log for the sentinel with a
timeout, and reaps qemu on every exit path.
