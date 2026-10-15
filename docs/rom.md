# ROM handling

The port ships no game data. The user supplies a Pilotwings 64 (US) ROM next to
`default.xbe`; nothing runs until it has passed the checks below
(`xbox/src/hw/xhw_rom.c`, PLAN.md section 3.1).

## What is accepted

One file in `D:\` (the folder of `default.xbe`), any name, with the extension
`.z64`, `.n64`, `.v64`, `.rom` or `.bin` (case-insensitive). The first such file
found is used; files named `default*`, `settings*`, `boot*`, `autopad*` are
skipped. The extension is only a hint to find the file.

Provenance of the one accepted dump (the "[!]" good dump of the US cartridge,
game code `NPWE`, version 0, "US v1.0"):

| | |
|---|---|
| size | 8,388,608 bytes |
| SHA-1 (z64 order) | `ec771aedf54ee1b214c25404fb4ec51cfd43191a` |
| MD5 (z64 order) | `8b346182730ceaffe5e2ccf6d223c5ef` |
| header title | `Pilot Wings64` (0x20, 20 bytes) |
| CRC1 / CRC2 | `0xc851961c` / `0x78fcaafa` |

The SHA-1 lives only in `xbox/include/xrom_known.h`.

## Procedure

1. Find the file. None: the "No ROM found" card (what is needed, where to put it,
   accepted extensions), 30 s, dashboard.
2. Size must be exactly 8,388,608 bytes, else "This is not a Pilotwings 64 ROM
   (size N bytes; expected 8 MB)". Nothing is read.
3. Read the file in 256 KB pieces into one 8 MB buffer; the splash load bar
   advances from 10 % to 90 %.
4. Byte order from the first four bytes (the bytes decide, not the extension):
   `80 37 12 40` z64 (big-endian, as is), `37 80 40 12` v64 (byte pairs swapped),
   `40 12 37 80` n64 (32-bit words reversed). Otherwise "Not an N64 ROM (bad
   header)". The image is normalised in RAM to z64.
5. Header: game code `NPWE` required. `NPWJ`/`NPWP`: "Japanese/European
   Pilotwings 64 is not supported; the port is built from the US release
   (NPWE)". Any other code: "This ROM is not Pilotwings 64 (game code XXXX)".
6. SHA-1 over the whole normalised image, compared with the table. Mismatch:
   "Bad or modified dump: SHA-1 is <40 hex>, expected <40 hex>. Re-dump your
   cartridge." Overdumps and trimmed files fail at step 2.
7. Match: `[ROM] verified: US v1.0 SHA-1 ok`, and `xhw_rom_load_verified` returns the
   normalised image. Every failure shows a card (reason, file name, byte order,
   header fields, SHA-1 when relevant), logs the same lines to `boot.log` and
   sets `lastexit.txt` to `ROM check failed (<reason>)`, where reason is one of
   `no ROM`, `size`, `bad header`, `wrong game`, `region`, `bad SHA-1`,
   `unreadable`.

Log lines (COM1 and `boot.log`):

```
[ROM] file D:\Pilotwings 64 (U).z64
[ROM] title "Pilot Wings64" code NPWE region E v0 crc 0xc851961c 0x78fcaafa order z64
[ROM] loaded 8192 KB in N ms, SHA-1 in N ms
[ROM] verified: US v1.0 SHA-1 ok
```

## Never commit a ROM

No `.z64/.n64/.v64/.eep/.rom`, no extracted data, no game frames. Keep ROMs and
conversions outside the checkout. `python3 tools/xbox/rom_leak_check.py` fails on
any tracked file that has a ROM extension, begins with an N64 header word (in any
of the three byte orders), contains a Pilotwings header, or is an image other
than `xbox/assets/logo.png`. Run it before every commit.

## Testing

- Host: `python3 tools/xbox/test_rom_check.py` compiles
  `tests/xbox/test_rom_check.c` with `xhw_rom.c` and `xhw_sha1.c`
  (`-DXROM_HOST_TEST`) using `cc`/`gcc`/`clang` and runs it. It builds a random
  8 MB image with a made-up header, writes it as z64/v64/n64 (same normalised
  bytes, same SHA-1), then checks each verdict: wrong size, bad magic, another
  game, NPWJ/NPWP, one corrupted byte; plus SHA-1 against the standard vectors
  (`abc`, empty, two-block, one million `a`). The real hash is not used; the
  checker takes an injectable table (`xrom_verify_with_table`).
- Byte-order variants for emulator or console runs: `python3
  tools/xbox/rom_order.py in.z64 out.v64` (and `.n64`, `.z64`; detects the input
  order from its bytes). It is a converter and contains no ROM data.
- xemu: `MX_ROM=<file> tools/xbox/xemu_run.sh 90 '\[ROM\] verified|\[BOOT\] card'`
  stages the file next to `default.xbe` under its own name; `MX_ROM=none` shows the
  "No ROM found" card. Test builds (`XHW_AUTOPAD`) also honour `getenv("MX_ROM")`
  as a file name on D: (the xemu runner already stages by name, so it is rarely
  needed).
