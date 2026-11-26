#include "rom_compat.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xnes.h>

int main(void) {
    const size_t prg_size = 8u * 16384u;
    const size_t source_size = 16u + prg_size;
    uint8_t *source = (uint8_t *)calloc(1, source_size);
    uint8_t *prepared = NULL;
    size_t prepared_size = 0;
    int normalized = 0;
    unsigned mapper = 0;
    char error[256];
    struct xnes_ctx_t *ctx;
    uint64_t frame_ns;

    assert(source != NULL);
    memcpy(source, "NES\\x1a", 4);
    source[4] = 8;
    source[5] = 0;
    source[6] = 0x12;
    source[7] = 0x08;
    memset(source + 16, 0xea, prg_size);
    source[16] = 0x4c; /* JMP $8000: stable CPU loop */
    source[17] = 0x00;
    source[18] = 0x80;
    source[16 + 0x3ffa] = 0x00;
    source[16 + 0x3ffb] = 0x80;
    source[16 + 0x3ffc] = 0x00;
    source[16 + 0x3ffd] = 0x80;
    source[16 + 0x3ffe] = 0x00;
    source[16 + 0x3fff] = 0x80;

    assert(nes_rom_prepare_for_libxnes(source, source_size, &prepared,
                                       &prepared_size, &normalized, &mapper,
                                       error, sizeof(error)) == 0);
    assert(normalized == 1);
    assert(mapper == 1);
    assert(prepared_size == source_size + 8192u);
    assert(prepared[4] == 8 && prepared[5] == 1);
    assert((prepared[7] & 0x0c) == 0);
    assert(memcmp(prepared, "NES\\x1a", 4) == 0);

    ctx = xnes_ctx_alloc(prepared, prepared_size);
    assert(ctx != NULL);
    assert(ctx->cartridge != NULL);
    assert(ctx->cartridge->mapper_number == 1);
    assert(ctx->cartridge->prg_rom_size == prg_size);
    assert(ctx->cartridge->chr_rom_size == 8192u);

    xnes_cartridge_mapper_ppu_write(ctx, 0x0000, 0xa5);
    assert(xnes_cartridge_mapper_ppu_read(ctx, 0x0000) == 0xa5);
    xnes_set_speed(ctx, 1.0f);
    xnes_reset(ctx);
    frame_ns = xnes_step_frame(ctx);
    assert(frame_ns > 0 && frame_ns < 100000000ULL);

    xnes_ctx_free(ctx);
    free(prepared);
    free(source);
    puts("PASS: NES 2.0 mapper-1 CHR-RAM normalization, MMC1 core load, CHR read/write, frame step");
    return 0;
}
