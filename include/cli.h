/*
 * Flightcard - USB CDC configuration CLI.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_CLI_H
#define FC_CLI_H

#include <stddef.h>

#include "fc_config.h"

typedef enum {
    CLI_ACTION_NONE = 0,
    CLI_ACTION_SAVE,
    CLI_ACTION_REBOOT,
} cli_action_t;

/* Execute one line. Writes human-readable output (never the password). */
cli_action_t cli_execute(config_t *cfg, const char *line,
                         char *out, size_t cap);

/* Poll USB CDC without blocking. Buffers a line across calls. */
void cli_poll(void);
void cli_printf(const char *fmt, ...);

#endif /* FC_CLI_H */
