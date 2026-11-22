/*
 * Flightcard - flash-backed configuration store.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_CONFIG_STORE_H
#define FC_CONFIG_STORE_H

#include <stdbool.h>

#include "fc_config.h"

void config_store_init(void);
const config_t *config_store_get(void);
config_t *config_store_mutable(void);

/* Load from flash, falling back to defaults + logging on invalid data. */
bool config_store_load(void);

/* Commit to the inactive 4 KiB sector. Must be called from the display core. */
bool config_store_save(void);

#endif /* FC_CONFIG_STORE_H */
