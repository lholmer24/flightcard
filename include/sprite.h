/*
 * Flightcard - sprite table/family selection.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_SPRITE_H
#define FC_SPRITE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *data; /* 96 x 96, 4 bpp, high nibble first, top-down */
    uint32_t size;
} fc_sprite_t;

extern const fc_sprite_t fc_sprites[];
extern const unsigned fc_sprite_count;

const char *fc_sprite_family(const char *code, const char *category);
const fc_sprite_t *fc_sprite_find(const char *code, const char *category);

#endif /* FC_SPRITE_H */
