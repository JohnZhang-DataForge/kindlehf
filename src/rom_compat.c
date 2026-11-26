#include "rom_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INES_HEADER_SIZE 16u
#define PRG_BANK_SIZE 16384u
#define CHR_BANK_SIZE 8192u
#define MAX_ROM_SIZE (64u * 1024u * 1024u)

static int fail(char *error, size_t error_size, const char *message) {
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return -1;
}

static size_t nes2_ram_size(uint8_t shift) {
    if (shift == 0) return 0;
    return (size_t)64u << shift;
}

int nes_rom_prepare_for_libxnes(const uint8_t *source, size_t source_size,
                                uint8_t **prepared, size_t *prepared_size,
                                int *was_normalized, unsigned *mapper_number,
                                char *error, size_t error_size) {
    unsigned format;
    unsigned mapper;
    unsigned prg_banks;
    unsigned source_chr_banks;
    unsigned output_chr_banks;
    size_t trainer_size;
    size_t prg_size;
    size_t source_chr_size;
    size_t source_payload_size;
    size_t synthetic_chr_size = 0;
    size_t output_size;
    uint8_t *output;
    int normalized = 0;

    if (prepared) *prepared = NULL;
    if (prepared_size) *prepared_size = 0;
    if (was_normalized) *was_normalized = 0;
    if (mapper_number) *mapper_number = 0;
    if (!source || !prepared || !prepared_size)
        return fail(error, error_size, "Invalid ROM compatibility arguments.");
    if (source_size < INES_HEADER_SIZE)
        return fail(error, error_size, "File is shorter than the 16-byte iNES header.");
    if (source_size > MAX_ROM_SIZE)
        return fail(error, error_size, "ROM exceeds the 64 MB safety limit.");
    if (source[0] != 'N' || source[1] != 'E' || source[2] != 'S' || source[3] != 0x1a)
        return fail(error, error_size, "File does not have the NES\x1A signature.");

    format = (source[7] >> 2) & 0x03;
    if (format == 1 || format == 3)
        return fail(error, error_size, "ROM uses a reserved or unsupported NES header format.");

    prg_banks = source[4];
    source_chr_banks = source[5];
    mapper = ((unsigned)(source[6] >> 4) & 0x0f) | ((unsigned)source[7] & 0xf0);

    if (format == 2) {
        /* libxnes supports only 8-bit mapper IDs and linear 16/8 KB bank sizes. */
        if (source[8] != 0)
            return fail(error, error_size, "NES 2.0 extended mapper IDs and submappers are not supported by this core.");
        if (source[9] != 0)
            return fail(error, error_size, "NES 2.0 extended/exponent ROM sizes are not supported by this core.");
        normalized = 1;
    }

    if (prg_banks == 0)
        return fail(error, error_size, "ROM contains no PRG-ROM banks.");

    trainer_size = (source[6] & 0x04) ? 512u : 0u;
    prg_size = (size_t)prg_banks * PRG_BANK_SIZE;
    source_chr_size = (size_t)source_chr_banks * CHR_BANK_SIZE;
    source_payload_size = INES_HEADER_SIZE + trainer_size + prg_size + source_chr_size;
    if (source_size < source_payload_size)
        return fail(error, error_size, "ROM data is truncated (file is smaller than its header declares).");

    output_chr_banks = source_chr_banks;
    if (source_chr_banks == 0) {
        size_t chr_ram_size = 0;
        if (format == 2) {
            size_t chr_volatile = nes2_ram_size(source[11] & 0x0f);
            size_t chr_nonvolatile = nes2_ram_size((source[11] >> 4) & 0x0f);
            chr_ram_size = chr_volatile + chr_nonvolatile;
        }
        /* libxnes mapper implementations access CHR through chr_rom even for CHR-RAM carts. */
        if (chr_ram_size == 0) chr_ram_size = CHR_BANK_SIZE;
        if (chr_ram_size > (size_t)255u * CHR_BANK_SIZE)
            return fail(error, error_size, "CHR-RAM size is too large for the bundled legacy core.");
        output_chr_banks = (unsigned)((chr_ram_size + CHR_BANK_SIZE - 1u) / CHR_BANK_SIZE);
        synthetic_chr_size = (size_t)output_chr_banks * CHR_BANK_SIZE;
        normalized = 1;
    }

    if (mapper_number) *mapper_number = mapper;
    output_size = source_payload_size + synthetic_chr_size;
    if (output_size > MAX_ROM_SIZE + (size_t)255u * CHR_BANK_SIZE)
        return fail(error, error_size, "Prepared ROM exceeds the supported size.");

    output = (uint8_t *)malloc(output_size);
    if (!output)
        return fail(error, error_size, "Not enough memory to prepare this ROM.");

    memcpy(output, source, source_payload_size);
    if (synthetic_chr_size) {
        memset(output + source_payload_size, 0, synthetic_chr_size);
        output[5] = (uint8_t)output_chr_banks;
    }
    if (format == 2) {
        /* Clear NES 2.0 marker and discard NES 2.0-only fields after validating them. */
        output[7] = (uint8_t)(output[7] & 0xf3);
        memset(output + 8, 0, 8);
    }

    *prepared = output;
    *prepared_size = output_size;
    if (was_normalized) *was_normalized = normalized;
    if (error && error_size) error[0] = '\0';
    return 0;
}
