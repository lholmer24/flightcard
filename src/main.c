/*
 * Flightcard - application entry point and task scheduling.
 *
 * SPDX-License-Identifier: MIT
 */
#include "clock.h"
#include "cli.h"
#include "config_store.h"
#include "demo.h"
#include "display.h"
#include "light.h"
#include "log.h"
#include "net.h"
#include "platform.h"
#include "sensors.h"
#include "ui.h"
#include "fc_config.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define DISPLAY_STACK_WORDS (12 * 1024 / 4)
#define NET_STACK_WORDS     (16 * 1024 / 4)
#define SUPER_STACK_WORDS   (2 * 1024 / 4)

static TaskHandle_t display_handle;
static TaskHandle_t net_handle;
static TaskHandle_t super_handle;
static volatile uint32_t display_heartbeat;

static void display_task(void *arg);
static void supervisor_task(void *arg);

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    printf("FATAL: stack overflow in %s\n", name ? name : "?");
    for (;;) tight_loop_contents();
}

void vApplicationMallocFailedHook(void)
{
    printf("FATAL: FreeRTOS malloc failed\n");
    for (;;) tight_loop_contents();
}

static void display_task(void *arg)
{
    (void)arg;
    LOG_I("display task started on core %u",
          (unsigned)portGET_CORE_ID());

    const config_t *cfg = config_store_get();
    bool demo = cfg->demo_mode != 0;
    uint32_t last_seq = 0;
    bool card_active = false;
    uint32_t card_ms = 0;
    uint32_t last_light = 0;
    float lux_samples[5] = {0};
    int lux_count = 0;
    int palette = (cfg->palette == FC_PALETTE_AMBER) ? FC_PALETTE_AMBER
                                                     : FC_PALETTE_CABIN;
    if (cfg->palette == FC_PALETTE_AUTO) palette = FC_PALETTE_CABIN;
    ui_set_palette(palette);

    float brightness = cfg->auto_brightness ? 0.05f : cfg->fixed_brightness;
    if (brightness < 0.0f || brightness > 1.0f) brightness = 0.40f;
    display_set_backlight(brightness);

    uint32_t demo_phase_ms = (uint32_t)fc_monotonic_ms();
    unsigned demo_index = 0;
    bool demo_showing_card = false;
    uint32_t next_rtc_ms = 0;

    watchdog_enable(8000, false);

    for (;;) {
        display_heartbeat++;
        uint32_t now = (uint32_t)fc_monotonic_ms();

        if (demo) {
            if ((uint32_t)(now - demo_phase_ms) >= 6000u) {
                demo_phase_ms = now;
                if (demo_showing_card) {
                    ui_show_clock();
                    demo_showing_card = false;
                } else {
                    ui_show_card(fc_demo_card(demo_index++));
                    demo_showing_card = true;
                }
            }
            ui_update_clock();
        } else {
            card_msg_t msg;
            if (g_card_queue != NULL
                && xQueueReceive(g_card_queue, &msg, 0) == pdTRUE) {
                if (msg.sequence > last_seq) {
                    last_seq = msg.sequence;
                    if (msg.valid) {
                        ui_show_card(&msg);
                        card_active = true;
                        card_ms = now;
                    } else {
                        ui_show_clock();
                        card_active = false;
                    }
                }
            }

            if (card_active && (uint32_t)(now - card_ms) > 45000u) {
                LOG_I("card: stale for 45 s, returning to clock");
                ui_show_clock();
                card_active = false;
            }
            ui_update_clock();
        }

        /* Ambient light: sample every 2 s, average five readings. */
        if (sensors_light_present()
            && (uint32_t)(now - last_light) >= 2000u) {
            last_light = now;
            float lux = 0.0f;
            if (sensors_read_lux(&lux)) {
                if (lux_count < 5) lux_samples[lux_count++] = lux;
                else {
                    memmove(lux_samples, lux_samples + 1, sizeof(float) * 4);
                    lux_samples[4] = lux;
                }
                float avg = 0.0f;
                for (int i = 0; i < lux_count; i++) avg += lux_samples[i];
                avg /= (float)lux_count;

                if (cfg->palette == FC_PALETTE_AUTO) {
                    if (fc_palette_step(&palette, avg, cfg->night_lux)) {
                        ui_set_palette(palette);
                        LOG_I("palette: %s at %.1f lux",
                              palette == FC_PALETTE_AMBER ? "amber" : "cabin",
                              (double)avg);
                    }
                }
                if (cfg->auto_brightness) {
                    float target = fc_lux_to_brightness(avg);
                    if (target < 0.05f) target = 0.05f;
                    if (target > 0.45f) target = 0.45f;
                    if (target > brightness + 0.02f || target < brightness - 0.02f) {
                        brightness = fc_brightness_slew(brightness, target);
                        display_set_backlight(brightness);
                        LOG_I("light: %.1f lux -> brightness %.2f",
                              (double)avg, (double)brightness);
                    }
                }
            }
        }

        /* Keep an optional RTC within a few seconds of the network clock. */
        if (sensors_rtc_present() && clock_is_valid()
            && (int32_t)(now - next_rtc_ms) >= 0) {
            if (sensors_rtc_write(clock_now_utc())) {
                next_rtc_ms = now + 6u * 60u * 60u * 1000u;
            } else {
                next_rtc_ms = now + 10u * 60u * 1000u;
            }
        }

        cli_poll();
        display_poll();
        watchdog_update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void supervisor_task(void *arg)
{
    (void)arg;
    uint32_t last_display = 0;
    uint32_t last_net = 0;
    uint32_t net_stale_since = 0;
    uint32_t last_report = (uint32_t)fc_monotonic_ms();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t now = (uint32_t)fc_monotonic_ms();
        uint32_t display_now = display_heartbeat;
        uint32_t net_now = g_net_heartbeat;

        if (display_now == last_display) {
            LOG_E("supervisor: display task heartbeat stopped");
        }
        last_display = display_now;

        if (!config_store_get()->demo_mode) {
            if (net_now == last_net) {
                if (net_stale_since == 0) net_stale_since = now;
                if ((uint32_t)(now - net_stale_since) > 300000u) {
                    LOG_E("supervisor: network task has been silent for 5 min; "
                          "clock/display continue");
                    net_stale_since = now; /* report once per 5 min */
                }
            } else {
                net_stale_since = 0;
            }
            last_net = net_now;
        }

        if ((uint32_t)(now - last_report) >= 60000u) {
            last_report = now;
            LOG_I("supervisor: heap %u free, display stack %u, net stack %u",
                  (unsigned)xPortGetFreeHeapSize(),
                  (unsigned)uxTaskGetStackHighWaterMark(display_handle),
                  (unsigned)uxTaskGetStackHighWaterMark(net_handle));
        }
    }
}

int main(void)
{
    stdio_init_all();
    sleep_ms(150);

    config_store_init();
    const config_t *cfg = config_store_get();

    clock_init();
    clock_set_timezone(cfg->timezone);

    if (cfg->seed_epoch >= 1700000000u) {
        clock_set_utc(cfg->seed_epoch);
        LOG_I("clock: seeded from config (%lu)", (unsigned long)cfg->seed_epoch);
    }

    sensors_init();
    uint32_t rtc_epoch = 0;
    if (sensors_rtc_present() && sensors_rtc_read(&rtc_epoch)) {
        clock_set_utc(rtc_epoch);
        LOG_I("clock: read RTC (%lu)", (unsigned long)rtc_epoch);
    }

    if (cfg->demo_mode) {
        uint32_t demo_epoch = 0;
        fc_demo_clock_epoch(&demo_epoch);
        clock_set_utc(demo_epoch);
        LOG_I("demo mode: network disabled");
    }

    display_init();
    ui_init();
    ui_set_palette(cfg->palette == FC_PALETTE_AMBER ? FC_PALETTE_AMBER
                                                  : FC_PALETTE_CABIN);
    ui_show_clock();
    display_set_backlight(cfg->fixed_brightness > 0.0f
                              ? cfg->fixed_brightness : 0.40f);

    g_card_queue = xQueueCreate(1, sizeof(card_msg_t));
    if (g_card_queue == NULL) {
        LOG_E("boot: could not create card queue");
    }

#if FC_SMP
    BaseType_t ok = xTaskCreateAffinitySet(display_task, "display",
                                           DISPLAY_STACK_WORDS, NULL, 5,
                                           1u << 0, &display_handle);
#else
    BaseType_t ok = xTaskCreate(display_task, "display", DISPLAY_STACK_WORDS,
                                NULL, 5, &display_handle);
#endif
    if (ok != pdPASS) LOG_E("boot: display task create failed");

    if (!cfg->demo_mode) {
#if FC_SMP
        ok = xTaskCreateAffinitySet(net_task, "net", NET_STACK_WORDS, NULL, 3,
                                    1u << 1, &net_handle);
#else
        ok = xTaskCreate(net_task, "net", NET_STACK_WORDS, NULL, 3, &net_handle);
#endif
        if (ok != pdPASS) LOG_E("boot: net task create failed");
    }

#if FC_SMP
    ok = xTaskCreateAffinitySet(supervisor_task, "super", SUPER_STACK_WORDS,
                                NULL, 1, 1u << 0, &super_handle);
#else
    ok = xTaskCreate(supervisor_task, "super", SUPER_STACK_WORDS, NULL, 1,
                     &super_handle);
#endif
    if (ok != pdPASS) LOG_E("boot: supervisor create failed");

    LOG_I("boot: clock visible, starting scheduler");
    vTaskStartScheduler();

    /* Should never return. */
    for (;;) tight_loop_contents();
}
