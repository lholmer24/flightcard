/*
 * Flightcard - serial logging with a monotonic timestamp.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_LOG_H
#define FC_LOG_H

void fc_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fc_log_hexdump(const void *data, unsigned len);

#define LOG_I(...) fc_log("I " __VA_ARGS__)
#define LOG_W(...) fc_log("W " __VA_ARGS__)
#define LOG_E(...) fc_log("E " __VA_ARGS__)

#endif /* FC_LOG_H */
