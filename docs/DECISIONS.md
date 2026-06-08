# Relic — design decisions

Short record of *why*, not *how*. The code is the source of truth for the
latter.

## Foundations

| Concern | Choice | Rationale |
|---|---|---|
| Language | **C99** | Lowest common denominator of Open Watcom v2, Retro68, nxdk, devkitPPC, and host clang. Kept to the conservative subset all five actually implement (no VLAs, no `//` comments; designated initializers only in the generated `trust_anchors.h`). |
| Memory | **No `malloc`**; all buffers static | Deterministic footprint on 4 MB boxes; no allocator to port. One exception: if `scroll=N` in `RELIC.CFG` requests a larger-than-default scrollback ring, that single block is allocated once at startup and never freed. |
| Porting layer | **`src/plat/plat.h`** | `core/` is 100% portable C; each OS gets its own `plat_*.c` implementing the contract. |
| Win95 toolchain | **Open Watcom v2** (`-bt=nt`) | Targets Win95, ships own CRT (no MSVCRT.DLL), runs on modern hosts. Import table is **audited at build time** against `tools/win32-allowlist.txt`. |
| Win 3.x toolchain | **OW2 Win386** (`-bt=windows` + `wbind`), not 16-bit code | 32-bit flat code bound into an NE: `int` stays 32 bits so `core/` and BearSSL compile unchanged (a true 16-bit build would need an int-width audit and slow segmented EC math). Winsock 1.1 is reached via runtime `_Call16` thunks into the 16-bit WINSOCK.DLL; far pointers come back through KERNEL's `GetSelectorBase` (win386's own `MapAliasToFlat` returns system-linear addresses, and raw DPMI faults inside the supervisor), and all socket payloads cross through one statically-aliased bounce buffer because per-call `'p'` aliasing corrupted handshake bytes. Console is Watcom "default windowing" (3.x has no console subsystem) — same trade as RetroConsole on macppc: no colors, no raw keys. Module references audited against `tools/win16-allowlist.txt`. |
| Mac toolchain | **Retro68** (PPC) | Modern-host build loop. |
| OS X PPC toolchain | **cctools-port (`877.8-ld64-253.9-ppc`) + FSF GCC 14** in Docker | Apple's current toolchain can't emit `ppc` Mach-O; the community-maintained ld64/as PPC revival plus upstream GCC's still-living Darwin/rs6000 support can. Reuses `src/plat/posix` unchanged, so the port is purely a toolchain, not code. The binary links against the 10.1.5 SDK (`-mmacosx-version-min=10.1 -mlong-double-64`) so one build covers 10.1 Puma through 10.5 Leopard; 10.0 is unreachable (no SDK, no `/dev/random` in xnu-123.5, no two-level namespace). SDKs are fetched at image build, like the OT glue. |
| TLS | **BearSSL**, ECDHE+{ChaCha20,AES128-GCM} only | Zero malloc, zero FILE\*, builds on every target compiler; minimal cipher set so the linker drops CBC, 3DES, AES-256, MD5, and the big AES tables. |
| Trust | **3 pinned roots** (GTS R4, GTS R1, GlobalSign Root CA) | Survives normal rotation; ~3 KB. Clock-skew: if the system clock reads earlier than 2020-01-01, validate against the build date instead. |
| JSON in | **jsmn** | Header-only, no malloc; response shape is shallow. |
| JSON out | **hand-rolled** (`json_write.c`) | Fixed request shape; only a correct string escaper is needed. |
| Net (Win) | **Winsock 1.1** (`wsock32`) | Lowest floor. |

## Runtime data

### Conversation store: binary `.DAT`, not JSONL

Each turn is `[role:1][is_json:1][len:4 LE][body]`. Chosen over JSONL because:

- **Streaming writes.** Tool-result content is written to disk as it's
  produced (`conv_open` / `conv_write` / `conv_commit`), with the length
  back-patched on commit. JSONL would require buffering or escaping the whole
  line first.
- **Streamed reads.** `conv_send_request` emits the `/v1/messages` body
  through a write callback, reading each turn's body from disk in small
  chunks; nothing is held in RAM. Raw-JSON turns are spliced in verbatim, so
  no decode pass is needed.
- **No allocator.** Parsing JSONL on resume would mean a jsmn pass per line
  and somewhere to put the unescaped result.
- **Self-healing.** A truncated trailing record is detected by the length
  prefix and overwritten on the next push.
- `.DAT` is the DOS convention for app-private binary; 8.3-safe.

The cost is that the files aren't human-readable. Acceptable: they're a cache,
not an export format.

### Multiple sessions

All session state lives under a `RELIC/` subdirectory: `RELIC.IDX` (last-used
id) + `RELICnnn.DAT` (1..999). Fresh launch allocates the next id; `-r`
resumes the latest, `-r N` a specific one. Mirrors Claude Code's
per-conversation transcripts and keeps individual files small. The parent
directory is picked from the first of cwd, `$TEMP`, `$TMP`, `$TMPDIR`,
`$HOME` where the `RELIC/` subdir can be created.

### Scrollback: in-app, RAM-only

On systems without native terminal scrollback, the app keeps its own: a
512×80 line ring (~41 KB BSS) captures everything written to stdout, and
**PgUp** at the prompt opens a full-screen viewer
(Up/Dn/PgUp/PgDn/Home/End). Kept in RAM (not spilled) because it's
display-only and 41 KB is cheap relative to the BearSSL/HTTP buffers.
`scroll=N` in `RELIC.CFG` resizes the ring at startup; values above the
built-in default are the project's sole `malloc` (one block, never freed).
Up-arrow is deliberately *not* the trigger — reserved for future
command history.

## Network

### HTTP keep-alive with retry-once

A single TLS handshake is **tens of seconds** of CPU on vintage hardware
(ECDHE + cert-chain verify). The connection is held open across requests; the server's
`Connection: close` is honoured. If a write/read on a *reused* connection
fails (idle-timeout RST), drop, reconnect, and resend exactly once. Fresh
connections don't retry. `http_read_response` consumes exactly the framed body
(Content-Length or chunked) so the socket is clean for the next request.

### Stateless API

`/v1/messages` keeps no server-side session — the full `messages[]` array is
sent every time. The body is streamed from the on-disk store via chunked
encoding, so request size is not bounded by any RAM buffer; `conv_trim` drops
oldest turn-pairs only to keep the upload under `CONV_BUDGET` (192 KB).
