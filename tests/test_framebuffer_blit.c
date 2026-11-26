#include "framebuffer_blit.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t fb[20u * 12u];
    const uint8_t src[] = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11
    };

    memset(fb, 0x5a, sizeof(fb));
    assert(nes_blit_gray8(fb, sizeof(fb), 20, 2, 1, 16, 10,
                          src, 4, 3, 3, 2, 0) == 0);
    assert(fb[3u * 20u + 5u] == 0);
    assert(fb[3u * 20u + 8u] == 3);
    assert(fb[4u * 20u + 5u] == 4);
    assert(fb[5u * 20u + 8u] == 11);
    assert(fb[3u * 20u + 4u] == 0x5a); /* left margin untouched */
    assert(fb[3u * 20u + 9u] == 0x5a); /* right margin untouched */
    assert(fb[2u * 20u + 5u] == 0x5a); /* row above untouched */

    memset(fb, 0x5a, sizeof(fb));
    assert(nes_blit_gray8(fb, sizeof(fb), 20, 2, 1, 16, 10,
                          src, 4, 3, -2, -1, 0) == 0);
    assert(fb[1u * 20u + 2u] == 6);
    assert(fb[2u * 20u + 2u] == 10);

    memset(fb, 0, sizeof(fb));
    assert(nes_blit_gray8(fb, sizeof(fb), 20, 2, 1, 16, 10,
                          src, 4, 3, 0, 0, 1) == 0);
    assert(fb[1u * 20u + 2u] == 255);
    assert(fb[1u * 20u + 5u] == 252);

    errno = 0;
    assert(nes_blit_gray8(fb, sizeof(fb), 10, 8, 0, 4, 4,
                          src, 4, 3, 0, 0, 0) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(nes_blit_gray8(fb, 10, 20, 0, 0, 16, 10,
                          src, 4, 3, 0, 0, 0) == -1);
    assert(errno == ERANGE);

    puts("PASS: direct 8-bpp framebuffer blit, stride, offsets, clipping, inversion and bounds");
    return 0;
}
