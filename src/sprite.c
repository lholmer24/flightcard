/*
 * Flightcard - sprite selection.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sprite.h"

#include <string.h>
#include <strings.h>

static const char *const HELI_PREFIXES[] = {
    "EC", "AS50", "AS55", "AS65", "AS32", "AS33", "AS35", "AS3B",
    "R22", "R44", "R66", "B06", "B212", "B407", "B412", "B429",
    "S76", "S92", "A109", "A139", "A169", "A189", "AW",
    "H47", "H60", "H64", "H135", "H145", "H160", "H175", "H225",
};

static bool starts_with_any(const char *s, const char *const *prefixes,
                            size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (strncasecmp(s, prefixes[i], strlen(prefixes[i])) == 0) return true;
    }
    return false;
}

const char *fc_sprite_family(const char *code, const char *category)
{
    if (code == NULL) code = "";
    if (category != NULL && strcasecmp(category, "A7") == 0) return "_heli";
    if (starts_with_any(code, HELI_PREFIXES,
                        sizeof(HELI_PREFIXES) / sizeof(HELI_PREFIXES[0]))) {
        return "_heli";
    }
    if (strcasecmp(code, "A400") == 0 || strcasecmp(code, "C130") == 0) return "_prop4";
    if (strncasecmp(code, "AT", 2) == 0 || strncasecmp(code, "DH", 2) == 0
        || strcasecmp(code, "B350") == 0) {
        return "_prop";
    }
    if (strncasecmp(code, "CR", 2) == 0) return "_regional";
    if (strncasecmp(code, "GL", 2) == 0) return "_biz";
    if (strcmp(code, "C17") == 0) return "_military";
    if (code[0] == 'C') return "_light";
    if (code[0] == 'E') return "_regional";
    if (strncmp(code, "A38", 3) == 0 || strncmp(code, "B74", 3) == 0) return "_jumbo4";
    if (strncmp(code, "A34", 3) == 0) return "_wide4";
    if (strncmp(code, "A33", 3) == 0 || strncmp(code, "A35", 3) == 0
        || strncmp(code, "B77", 3) == 0 || strncmp(code, "B78", 3) == 0) {
        return "_wide2";
    }
    return "_narrow";
}

static const fc_sprite_t *find_named(const char *name)
{
    for (unsigned i = 0; i < fc_sprite_count; i++) {
        if (strcmp(fc_sprites[i].name, name) == 0) return &fc_sprites[i];
    }
    return NULL;
}

const fc_sprite_t *fc_sprite_find(const char *code, const char *category)
{
    if (code != NULL && code[0]) {
        const fc_sprite_t *s = find_named(code);
        if (s) return s;
    }
    const fc_sprite_t *s = find_named(fc_sprite_family(code, category));
    if (s) return s;
    return find_named("_narrow");
}
