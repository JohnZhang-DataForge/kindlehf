#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    ROOT=vendor/libxnes
else
    ROOT=$1
fi

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
cartridge = root / "src" / "cartridge.c"
source = cartridge.read_text()

extern_line = "extern void xnes_mapper225_init(struct xnes_cartridge_t * c);\n"
extern_new = extern_line + "extern void xnes_mapper227_init(struct xnes_cartridge_t * c);\n"
if "xnes_mapper227_init" not in source:
    if extern_line not in source:
        raise SystemExit("cannot locate mapper extern declarations in cartridge.c")
    source = source.replace(extern_line, extern_new, 1)

case_anchor = """\t\tcase 225:
\t\t\txnes_mapper225_init(c);
\t\t\treturn 1;
"""
case_new = case_anchor + """\t\tcase 227:
\t\t\txnes_mapper227_init(c);
\t\t\treturn 1;
"""
if "case 227:" not in source:
    if case_anchor not in source:
        raise SystemExit("cannot locate mapper dispatch in cartridge.c")
    source = source.replace(case_anchor, case_new, 1)
cartridge.write_text(source)

mapper = root / "src" / "mapper" / "mapper227.c"
mapper_source = r'''/*
 * Mapper 227 (BMC 1200-in-1) implementation.
 *
 * Register source: the CPU write ADDRESS in $8000-$FFFF; write data is ignored.
 * Banking and mirroring follow the public NESdev INES Mapper 227 address-latch
 * specification. The cartridge uses a fixed, unbanked 8 KiB CHR-RAM window.
 *
 * This implementation is intentionally limited to the standard multicart
 * wiring (not the separate Nanjing/Waixing battery-WRAM variants).
 */

#include <xnes.h>

static unsigned mapper227_bank(struct xnes_cartridge_t *c, unsigned bank)
{
    unsigned count = c->prg_rom_size / 0x4000u;
    if (count == 0) return 0;
    return bank % count;
}

static void mapper227_apply(struct xnes_cartridge_t *c, uint16_t addr)
{
    unsigned bank = ((unsigned)(addr >> 2) & 0x1fu) |
                    ((unsigned)(addr & 0x0100u) >> 3);
    unsigned slot0, slot1;
    unsigned mode = (addr >> 7) & 1u;
    unsigned s = addr & 1u;
    unsigned l = (addr >> 9) & 1u;

    if (mode) {
        if (s) {
            slot0 = bank & 0x3eu;
            slot1 = slot0 + 1u;
        } else {
            slot0 = bank;
            slot1 = bank;
        }
    } else {
        slot0 = s ? (bank & 0x3eu) : bank;
        slot1 = l ? (bank | 0x07u) : (bank & 0x38u);
    }

    c->mapper.m.m225.prg_bank0 = (uint8_t)mapper227_bank(c, slot0);
    c->mapper.m.m225.prg_bank1 = (uint8_t)mapper227_bank(c, slot1);
    c->mapper.m.m225.chr_bank = (uint8_t)mode; /* O=1 protects multicart CHR-RAM */
    c->mirror = (addr & 0x0002u) ?
        XNES_CARTRIDGE_MIRROR_HORIZONTAL :
        XNES_CARTRIDGE_MIRROR_VERTICAL;
}

static uint8_t mapper227_cpu_read(struct xnes_ctx_t *ctx, uint16_t addr)
{
    struct xnes_cartridge_t *c = ctx->cartridge;
    unsigned bank;

    if (addr >= 0x6000u && addr < 0x8000u)
        return c->sram[addr - 0x6000u];
    if (addr < 0x8000u)
        return 0;

    bank = (addr < 0xc000u) ?
        c->mapper.m.m225.prg_bank0 : c->mapper.m.m225.prg_bank1;
    return c->prg_rom[(size_t)bank * 0x4000u + (addr & 0x3fffu)];
}

static void mapper227_cpu_write(struct xnes_ctx_t *ctx, uint16_t addr, uint8_t val)
{
    struct xnes_cartridge_t *c = ctx->cartridge;
    (void)val;

    if (addr >= 0x8000u)
        mapper227_apply(c, addr);
    else if (addr >= 0x6000u)
        c->sram[addr - 0x6000u] = val;
}

static uint8_t mapper227_ppu_read(struct xnes_ctx_t *ctx, uint16_t addr)
{
    struct xnes_cartridge_t *c = ctx->cartridge;
    return c->chr_rom[addr & 0x1fffu];
}

static void mapper227_ppu_write(struct xnes_ctx_t *ctx, uint16_t addr, uint8_t val)
{
    struct xnes_cartridge_t *c = ctx->cartridge;
    if (!c->mapper.m.m225.chr_bank)
        c->chr_rom[addr & 0x1fffu] = val;
}

void xnes_mapper227_init(struct xnes_cartridge_t *c)
{
    c->mapper.m.m225.prg_banks = (uint8_t)(c->prg_rom_size / 0x4000u);
    mapper227_apply(c, 0x8000u);

    c->mapper.cpu_read = mapper227_cpu_read;
    c->mapper.cpu_write = mapper227_cpu_write;
    c->mapper.ppu_read = mapper227_ppu_read;
    c->mapper.ppu_write = mapper227_ppu_write;
    c->mapper.apu_step = NULL;
    c->mapper.ppu_step = NULL;
}
'''
mapper.write_text(mapper_source)
print("Applied libxnes Mapper 227 patch:", root)
PY
