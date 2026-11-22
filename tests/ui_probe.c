/*
 * Host LVGL probe: validates that the generated static fonts render through
 * the same UI code used on the Pico, without needing hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#include "clock.h"
#include "demo.h"
#include "log.h"
#include "ui.h"
#include "fc_config.h"
#include "fc_assets_generated.h"

#include "lvgl.h"

#include <stdio.h>
#include <string.h>

static uint32_t tick_ms;
static unsigned long painted_pixels;
static unsigned long non_black_pixels;
static uint16_t framebuffer[320 * 240];

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = framebuffer[i];
        unsigned r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        unsigned char rgb[3] = {
            (unsigned char)((r << 3) | (r >> 2)),
            (unsigned char)((g << 2) | (g >> 4)),
            (unsigned char)((b << 3) | (b >> 2)),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static uint32_t tick_cb(void)
{
    return tick_ms;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    const uint16_t *p = (const uint16_t *)px;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t c = p[y * w + x];
            framebuffer[(area->y1 + y) * 320 + area->x1 + x] = c;
            painted_pixels++;
            if (c) non_black_pixels++;
        }
    }
    lv_display_flush_ready(disp);
}

int main(void)
{
    static uint16_t buf1[320 * 40];
    static uint16_t buf2[320 * 40];

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(320, 240);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_default(disp);

    lv_font_glyph_dsc_t d;
    memset(&d, 0, sizeof(d));
    bool colon = lv_font_get_glyph_dsc(&fc_font_clock, &d, ':', 0);
    printf("clock ':' ok=%d adv=%u box=%ux%u ofs=%d,%d base=%d line=%d\n",
           colon, d.adv_w, d.box_w, d.box_h, d.ofs_x, d.ofs_y,
           (int)fc_font_clock.base_line, (int)fc_font_clock.line_height);
    printf("clock width 21:47 = %d\n",
           (int)lv_text_get_width("21:47", 5, &fc_font_clock, 0));
    printf("type width \"Airbus A380-800\" = %d\n",
           (int)lv_text_get_width("Airbus A380-800", 15, &fc_font_body, 0));

    clock_init();
    clock_set_utc(1795211220u); /* 2026-11-20 21:47 UTC */
    ui_init();
    ui_set_palette(FC_PALETTE_CABIN);
    ui_show_clock();
    painted_pixels = non_black_pixels = 0;
    lv_refr_now(disp);
    printf("clock frame: painted=%lu non-black=%lu\n",
           painted_pixels, non_black_pixels);
    write_ppm("/tmp/opencode/fc_clock.ppm");

    ui_show_card(fc_demo_card(0));
    painted_pixels = non_black_pixels = 0;
    lv_refr_now(disp);
    printf("A380 frame: painted=%lu non-black=%lu\n",
           painted_pixels, non_black_pixels);
    write_ppm("/tmp/opencode/fc_a380.ppm");

    ui_show_card(fc_demo_card(1));
    painted_pixels = non_black_pixels = 0;
    lv_refr_now(disp);
    printf("Dash8 frame: painted=%lu non-black=%lu\n",
           painted_pixels, non_black_pixels);
    write_ppm("/tmp/opencode/fc_dash8.ppm");

    ui_set_palette(FC_PALETTE_AMBER);
    painted_pixels = non_black_pixels = 0;
    lv_refr_now(disp);
    printf("Amber frame: painted=%lu non-black=%lu\n",
           painted_pixels, non_black_pixels);
    write_ppm("/tmp/opencode/fc_amber.ppm");

    if (!colon || non_black_pixels < 1000) {
        printf("PROBE FAILED\n");
        return 1;
    }
    printf("PROBE OK\n");
    return 0;
}
