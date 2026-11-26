#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <xnes.h>

int main(int argc, char **argv) {
    FILE *f;
    long len;
    uint8_t *rom;
    struct xnes_ctx_t *ctx;
    uint64_t frame_ns;
    uint32_t pixel;
    uint8_t buttons = XNES_JOYSTICK_A | XNES_JOYSTICK_RIGHT;

    assert(argc == 2);
    f = fopen(argv[1], "rb");
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    len = ftell(f);
    assert(len >= 16);
    assert(fseek(f, 0, SEEK_SET) == 0);
    rom = (uint8_t *)malloc((size_t)len);
    assert(rom != NULL);
    assert(fread(rom, 1, (size_t)len, f) == (size_t)len);
    fclose(f);

    ctx = xnes_ctx_alloc(rom, (size_t)len);
    free(rom);
    assert(ctx != NULL);
    assert(ctx->cartridge != NULL);
    assert(ctx->cartridge->mapper_number == 0);
    xnes_set_speed(ctx, 1.0f);

    frame_ns = xnes_step_frame(ctx);
    assert(frame_ns > 0 && frame_ns < 100000000ULL);
    pixel = xnes_get_pixel(ctx, 0, 0);
    (void)pixel;

    xnes_controller_joystick_p1(&ctx->ctl, buttons, 0);
    assert((ctx->ctl.joystick.p1.key & buttons) == buttons);
    xnes_controller_joystick_p1(&ctx->ctl, 0, buttons);
    assert((ctx->ctl.joystick.p1.key & buttons) == 0);

    xnes_ctx_free(ctx);
    puts("PASS: iNES mapper-0 load, frame step, pixel read, controller press/release, cleanup");
    return 0;
}
