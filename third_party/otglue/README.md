# third_party/otglue — Open Transport static glue

The `*.o` files here are **not committed**. They are Apple's static link glue
for Open Transport on PowerPC, extracted from the final MPW release:

| File | Provides |
|---|---|
| `OpenTransportAppPPC.o` | `InitOpenTransport`, `OTOpenEndpoint`, `OTCloseProvider`, `CloseOpenTransport`, `__gOTClientRecord` … |
| `OpenTptInetPPC.o` | `OTOpenInternetServices` |

Retro68 ships PEF *import stubs* for `OpenTransportLib`/`OpenTptInternetLib`
but not this glue, so calling the exported `*Priv` entry points directly fails
on OT 1.x (Mac OS 8.1) with `kOTNotFoundErr` because ASLM and the per-app
client record are never initialised. Linking the real glue is exactly what
period apps (IE, Netscape, Fetch) did.

## Fetch

```sh
tools/fetch_otglue.sh
```

Downloads `MPW-GM.img.bin` (~24 MB) from the public Apple-FTP mirror,
verifies its SHA-256, and extracts the two XCOFF objects using the Retro68
docker image's `ConvertDiskImage` + hfsutils. Override the source with
`MPW_GM_URL=…` if the mirror moves.

## Manual

If you already have an MPW or CodeWarrior install, copy the data forks of
`Interfaces&Libraries/Libraries/PPCLibraries/OpenTransportAppPPC.o` and
`OpenTptInetPPC.o` here.
