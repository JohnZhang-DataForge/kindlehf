#ifndef NES_ROM_COMPAT_H
#define NES_ROM_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Prepare a ROM for the bundled libxnes legacy iNES parser.
 * The returned buffer is heap-allocated and must be released with free().
 */
int nes_rom_prepare_for_libxnes(const uint8_t *source, size_t source_size,
                                uint8_t **prepared, size_t *prepared_size,
                                int *was_normalized, unsigned *mapper_number,
                                char *error, size_t error_size);

#endif
