#!/usr/bin/env python3
"""Generate a tiny original mapper-0 iNES homebrew smoke-test ROM."""
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
header = bytearray(b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8))
prg = bytearray([0xEA] * 0x4000)
program = bytes([
    0x78, 0xD8,
    0xA9, 0x3F,
    0x8D, 0x06, 0x20,
    0xA9, 0x00,
    0x8D, 0x06, 0x20,
    0xA9, 0x0F,
    0x8D, 0x07, 0x20,
    0xA9, 0x08,
    0x8D, 0x01, 0x20,
    0x4C, 0x16, 0x80
])
prg[:len(program)] = program
for offset in (0x3FFA, 0x3FFC, 0x3FFE):
    prg[offset:offset + 2] = bytes([0x16, 0x80])
chr_rom = bytearray(0x2000)
for row in range(8):
    chr_rom[16 + row] = 0xAA if row & 1 else 0x55
    chr_rom[16 + row + 8] = 0x00
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(header + prg + chr_rom)
print(f"Generated mapper-0 test ROM: {out} ({out.stat().st_size} bytes)")
