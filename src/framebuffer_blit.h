#ifndef NES_FRAMEBUFFER_BLIT_H
#define NES_FRAMEBUFFER_BLIT_H

#include <stddef.h>
#include <stdint.h>

/* Copy an 8-bit grayscale rectangle into a mapped 8-bpp framebuffer.
 * The source buffer is tightly packed; framebuffer rows may be padded.
 * x/y offsets account for framebuffer virtual offsets and FBInk view origin.
 * Set invert when the framebuffer uses inverted 8-bit grayscale.
 */
int nes_blit_gray8(uint8_t *framebuffer, size_t framebuffer_size,
                   uint32_t stride, uint32_t x_offset, uint32_t y_offset,
                   uint32_t screen_width, uint32_t screen_height,
                   const uint8_t *source, int source_width, int source_height,
                   int destination_x, int destination_y, int invert);

#endif
