#include "framebuffer_blit.h"

#include <errno.h>
#include <stdint.h>

int nes_blit_gray8(uint8_t *framebuffer, size_t framebuffer_size,
                   uint32_t stride, uint32_t x_offset, uint32_t y_offset,
                   uint32_t screen_width, uint32_t screen_height,
                   const uint8_t *source, int source_width, int source_height,
                   int destination_x, int destination_y, int invert) {
    int64_t left, top, right, bottom;
    int x, y;

    if (!framebuffer || !source || source_width <= 0 || source_height <= 0 ||
        stride == 0 || screen_width == 0 || screen_height == 0 ||
        (uint64_t)x_offset + screen_width > stride) {
        errno = EINVAL;
        return -1;
    }

    left = destination_x < 0 ? 0 : destination_x;
    top = destination_y < 0 ? 0 : destination_y;
    right = (int64_t)destination_x + source_width;
    bottom = (int64_t)destination_y + source_height;
    if (right > screen_width) right = screen_width;
    if (bottom > screen_height) bottom = screen_height;
    if (left >= right || top >= bottom) return 0;

    for (y = (int)top; y < (int)bottom; y++) {
        size_t dst_offset = ((size_t)y + y_offset) * stride +
                            (size_t)left + x_offset;
        size_t src_offset = (size_t)(y - destination_y) * (size_t)source_width +
                            (size_t)(left - destination_x);
        size_t width = (size_t)(right - left);
        size_t x_bytes;

        if (dst_offset > framebuffer_size || width > framebuffer_size - dst_offset) {
            errno = ERANGE;
            return -1;
        }
        for (x_bytes = 0; x_bytes < width; x_bytes++) {
            uint8_t pixel = source[src_offset + x_bytes];
            framebuffer[dst_offset + x_bytes] = invert ? (uint8_t)(255u - pixel) : pixel;
        }
    }
    return 0;
}
