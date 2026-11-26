#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xnes.h>

#define PRG_BANK_SIZE 0x4000u
#define CHR_SIZE 0x2000u
#define PRG_BANKS 32u

int main(void) {
    const size_t rom_size = 16u + PRG_BANKS * PRG_BANK_SIZE + CHR_SIZE;
    uint8_t *rom = (uint8_t *)calloc(1, rom_size);
    struct xnes_ctx_t *ctx;
    struct xnes_cartridge_t *cart;
    unsigned bank;

    assert(rom != NULL);
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1a;
    rom[4] = PRG_BANKS;
    rom[5] = 1;
    rom[6] = 0x30;
    rom[7] = 0xe0; /* iNES mapper 227 */
    for (bank = 0; bank < PRG_BANKS; bank++) {
        memset(rom + 16u + bank * PRG_BANK_SIZE, (int)bank, PRG_BANK_SIZE);
    }

    ctx = xnes_ctx_alloc(rom, rom_size);
    free(rom);
    assert(ctx != NULL);
    cart = ctx->cartridge;
    assert(cart != NULL);
    assert(cart->mapper_number == 227);
    assert(cart->mapper.m.m225.prg_bank0 == 0);
    assert(cart->mapper.m.m225.prg_bank1 == 0);
    assert(cart->mapper.cpu_read(ctx, 0x8000) == 0);
    assert(cart->mapper.cpu_read(ctx, 0xc000) == 0);

    /* 16K switchable bank 5, fixed high bank 0. */
    cart->mapper.cpu_write(ctx, (uint16_t)(0x8000u | (5u << 2)), 0xff);
    assert(cart->mapper.cpu_read(ctx, 0x8000) == 5);
    assert(cart->mapper.cpu_read(ctx, 0xc000) == 0);

    /* L=1 selects the PPp|7 high bank; A1 selects horizontal mirroring. */
    cart->mapper.cpu_write(ctx, (uint16_t)(0x8000u | (5u << 2) | 0x0200u | 0x0002u), 0);
    assert(cart->mapper.cpu_read(ctx, 0x8000) == 5);
    assert(cart->mapper.cpu_read(ctx, 0xc000) == 7);
    assert(cart->mirror == XNES_CARTRIDGE_MIRROR_HORIZONTAL);

    /* O=1, S=1 selects the aligned 32K pair containing bank 5. */
    cart->mapper.cpu_write(ctx, (uint16_t)(0x8000u | (5u << 2) | 0x0080u | 0x0001u), 0);
    assert(cart->mapper.cpu_read(ctx, 0x8000) == 4);
    assert(cart->mapper.cpu_read(ctx, 0xc000) == 5);

    /* Mapper 227 exposes one fixed 8K CHR-RAM window. */
    cart->mapper.ppu_write(ctx, 0x0123, 0x5a);
    assert(cart->mapper.ppu_read(ctx, 0x0123) == 0x5a);

    xnes_ctx_free(ctx);
    puts("PASS: Mapper 227 BMC 1200-in-1 PRG modes, mirroring, CHR-RAM, cleanup");
    return 0;
}
