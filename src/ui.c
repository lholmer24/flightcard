/*
 * Flightcard - LVGL clock and route-band card.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui.h"
#include "format.h"
#include "sprite.h"
#include "types.h"
#include "clock.h"
#include "timezone.h"
#include "fc_assets_generated.h"
#include "fc_config.h"

#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define W 320
#define H 240

/* Palette roles: 0xRRGGBB. Background is true black on the panel. */
static const uint32_t CABIN[3] = {0xF2E8D5, 0xB9A88B, 0x6B5D4A};
static const uint32_t AMBER[3] = {0xFFC061, 0xC98C3F, 0x6D4B22};

static uint32_t pal_primary = 0xF2E8D5;
static uint32_t pal_secondary = 0xB9A88B;
static uint32_t pal_dim = 0x6B5D4A;
static int pal_mode = 1;

static lv_obj_t *clock_layer;
static lv_obj_t *card_layer;
static lv_obj_t *clock_h;
static lv_obj_t *clock_colon;
static lv_obj_t *clock_m;
static lv_obj_t *clock_rule;
static lv_obj_t *date_lbl;

static lv_obj_t *callsign_lbl;
static lv_obj_t *airline_lbl;
static lv_obj_t *type1_lbl;
static lv_obj_t *type2_lbl;
static lv_obj_t *origin_lbl;
static lv_obj_t *dest_lbl;
static lv_obj_t *route_unknown_lbl;
static lv_obj_t *reg_lbl;
static lv_obj_t *header_rule;
static lv_obj_t *stats_rule;
static lv_obj_t *stat_labels[3];
static lv_obj_t *stat_values[3];
static lv_obj_t *arrow_shaft;

static lv_obj_t *sprite_img;
static lv_obj_t *arrow_head_img;
static uint16_t sprite_buf[96 * 96];
static uint16_t arrow_head_buf[10 * 11];
static lv_image_dsc_t sprite_dsc;
static lv_image_dsc_t arrow_head_dsc;
static char last_sprite[8];
static bool showing_card;
static bool route_origin_missing;
static bool route_dest_missing;

static int clock_last_year = -9999;
static int clock_last_yday = -1;
static int clock_last_hour = -1;
static int clock_last_min = -1;

static uint16_t rgb565(uint32_t rgb)
{
    uint8_t r = (uint8_t)(rgb >> 16);
    uint8_t g = (uint8_t)(rgb >> 8);
    uint8_t b = (uint8_t)rgb;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint32_t shade(uint32_t rgb, int level, int total)
{
    uint32_t r = ((rgb >> 16) & 0xFF) * (uint32_t)level / (uint32_t)total;
    uint32_t g = ((rgb >> 8) & 0xFF) * (uint32_t)level / (uint32_t)total;
    uint32_t b = (rgb & 0xFF) * (uint32_t)level / (uint32_t)total;
    return (r << 16) | (g << 8) | b;
}

static lv_obj_t *make_layer(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, W, H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* LVGL's base_line is measured from the bottom of the line box. */
static int font_top(const lv_font_t *font, int baseline)
{
    return baseline - (font->line_height - font->base_line);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            int x, int baseline, uint32_t colour)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(l, x, font_top(font, baseline));
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *make_rule(lv_obj_t *parent, int x, int y, int width,
                           uint32_t colour)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, width, 1);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void ink_bounds(const lv_font_t *font, const char *text,
                       int *lo, int *hi)
{
    int pen = 0;
    int lower = 0x7FFF;
    int upper = -0x7FFF;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        lv_font_glyph_dsc_t d;
        memset(&d, 0, sizeof(d));
        if (lv_font_get_glyph_dsc(font, &d, *p, 0)) {
            if (d.box_w > 0 && d.box_h > 0) {
                int left = pen + d.ofs_x;
                int right = left + d.box_w;
                if (left < lower) lower = left;
                if (right > upper) upper = right;
            }
            pen += d.adv_w;
        } else {
            pen += lv_font_get_glyph_width(font, *p, 0);
        }
    }
    if (upper < lower) {
        lower = 0;
        upper = 0;
    }
    *lo = lower;
    *hi = upper;
}

static void position_clock(void)
{
    int c_lo, c_hi, h_lo, h_hi, m_lo, m_hi;
    ink_bounds(&fc_font_clock, ":", &c_lo, &c_hi);
    ink_bounds(&fc_font_clock, lv_label_get_text(clock_h), &h_lo, &h_hi);
    ink_bounds(&fc_font_clock, lv_label_get_text(clock_m), &m_lo, &m_hi);

    int c_width = c_hi - c_lo;
    int h_width = h_hi - h_lo;
    int m_width = m_hi - m_lo;
    int gap = 16;
    int span = (h_width > m_width ? h_width : m_width) + gap;

    int hx = W / 2 - c_width / 2 - (span - h_width) - h_hi;
    int mx = W / 2 + c_width / 2 + (span - m_width) - m_lo;
    int cx = W / 2 - (c_lo + c_hi) / 2;

    lv_obj_set_x(clock_h, hx);
    lv_obj_set_x(clock_m, mx);
    lv_obj_set_x(clock_colon, cx);
}

static void apply_clock_text(void)
{
    struct tm tm;
    clock_get_local(&tm);
    char h[4], m[4];
    snprintf(h, sizeof(h), "%02d", tm.tm_hour);
    snprintf(m, sizeof(m), "%02d", tm.tm_min);
    lv_label_set_text(clock_h, h);
    lv_label_set_text(clock_m, m);
    lv_label_set_text(clock_colon, ":");
    position_clock();

    char date[48];
    snprintf(date, sizeof(date), "%s %02d %s",
             fc_weekday_name(tm.tm_wday), tm.tm_mday,
             fc_month_name(tm.tm_mon + 1));
    lv_label_set_text(date_lbl, date);
    int width = lv_text_get_width(date, strlen(date), &fc_font_date, 0);
    lv_obj_set_x(date_lbl, W / 2 - width / 2);
}

static void fill_sprite(void)
{
    if (!showing_card || last_sprite[0] == '\0') return;
    const fc_sprite_t *s = NULL;
    for (unsigned i = 0; i < fc_sprite_count; i++) {
        if (strcmp(fc_sprites[i].name, last_sprite) == 0) {
            s = &fc_sprites[i];
            break;
        }
    }
    if (s == NULL) return;

    for (int y = 0; y < 96; y++) {
        for (int x = 0; x < 96; x++) {
            int index = y * 48 + x / 2;
            uint8_t byte = s->data[index];
            int pen = (x & 1) ? (byte & 0x0F) : (byte >> 4);
            uint32_t rgb = 0;
            if (pen >= 1 && pen <= 7) rgb = shade(pal_primary, pen, 7);
            else if (pen >= 8) rgb = shade(pal_secondary, pen - 7, 8);
            sprite_buf[y * 96 + x] = rgb565(rgb);
        }
    }
    lv_obj_invalidate(sprite_img);
}

static void fill_arrow_head(void)
{
    uint16_t colour = rgb565(pal_secondary);
    for (int i = 0; i < 10; i++) {
        int half = 5 - (i * 5) / 9;
        for (int row = 0; row < 11; row++) {
            bool on = row >= 5 - half && row <= 5 + half;
            arrow_head_buf[row * 10 + i] = on ? colour : 0;
        }
    }
    lv_obj_invalidate(arrow_head_img);
}

static void set_route_colours(void)
{
    lv_obj_set_style_text_color(origin_lbl,
        lv_color_hex(route_origin_missing ? pal_dim : pal_primary), 0);
    lv_obj_set_style_text_color(dest_lbl,
        lv_color_hex(route_dest_missing ? pal_dim : pal_primary), 0);
}

static void apply_colours(void)
{
    lv_obj_set_style_text_color(clock_h, lv_color_hex(pal_primary), 0);
    lv_obj_set_style_text_color(clock_colon, lv_color_hex(pal_primary), 0);
    lv_obj_set_style_text_color(clock_m, lv_color_hex(pal_primary), 0);
    lv_obj_set_style_text_color(date_lbl, lv_color_hex(pal_secondary), 0);
    lv_obj_set_style_bg_color(clock_rule, lv_color_hex(pal_dim), 0);

    lv_obj_set_style_text_color(callsign_lbl, lv_color_hex(pal_primary), 0);
    lv_obj_set_style_text_color(type1_lbl, lv_color_hex(pal_primary), 0);
    lv_obj_set_style_text_color(type2_lbl, lv_color_hex(pal_primary), 0);
    lv_obj_set_style_text_color(airline_lbl, lv_color_hex(pal_secondary), 0);
    lv_obj_set_style_text_color(route_unknown_lbl, lv_color_hex(pal_secondary), 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_text_color(stat_labels[i], lv_color_hex(pal_dim), 0);
        lv_obj_set_style_text_color(stat_values[i], lv_color_hex(pal_secondary), 0);
    }
    lv_obj_set_style_text_color(reg_lbl, lv_color_hex(pal_dim), 0);
    lv_obj_set_style_bg_color(header_rule, lv_color_hex(pal_dim), 0);
    lv_obj_set_style_bg_color(stats_rule, lv_color_hex(pal_dim), 0);
    lv_obj_set_style_bg_color(arrow_shaft, lv_color_hex(pal_secondary), 0);
    set_route_colours();
    fill_arrow_head();
    fill_sprite();
}

static bool missing_code(const char *s)
{
    return s == NULL || s[0] == '\0' || strcmp(s, "?") == 0
           || strcmp(s, "\xE2\x80\x94") == 0;
}

static void set_label_left(lv_obj_t *lbl, const lv_font_t *font, int x,
                           int baseline, const char *text)
{
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_pos(lbl, x, font_top(font, baseline));
}

static void set_label_right(lv_obj_t *lbl, const lv_font_t *font, int right,
                            int baseline, const char *text)
{
    const char *t = text ? text : "";
    lv_label_set_text(lbl, t);
    int width = lv_text_get_width(t, strlen(t), font, 0);
    lv_obj_set_pos(lbl, right - width, font_top(font, baseline));
}

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    clock_layer = make_layer(scr);
    card_layer = make_layer(scr);

    clock_h = make_label(clock_layer, &fc_font_clock, 0, 135, pal_primary);
    clock_colon = make_label(clock_layer, &fc_font_clock, 0, 135, pal_primary);
    clock_m = make_label(clock_layer, &fc_font_clock, 0, 135, pal_primary);
    clock_rule = make_rule(clock_layer, W / 2 - 24, 146, 48, pal_dim);
    date_lbl = make_label(clock_layer, &fc_font_date, 0, 168, pal_secondary);

    header_rule = make_rule(card_layer, 18, 76, 184, pal_dim);
    stats_rule = make_rule(card_layer, 18, 180, 284, pal_dim);
    arrow_shaft = make_rule(card_layer, 75, 156, 160, pal_secondary);

    callsign_lbl = make_label(card_layer, &fc_font_callsign, 18, 40, pal_primary);
    airline_lbl = make_label(card_layer, &fc_font_airline, 18, 60, pal_secondary);
    type1_lbl = make_label(card_layer, &fc_font_body, 18, 99, pal_primary);
    type2_lbl = make_label(card_layer, &fc_font_body, 18, 120, pal_primary);
    origin_lbl = make_label(card_layer, &fc_font_route, 18, 164, pal_primary);
    dest_lbl = make_label(card_layer, &fc_font_route, 302, 164, pal_primary);
    route_unknown_lbl = make_label(card_layer, &fc_font_body, 18, 164,
                                   pal_secondary);
    reg_lbl = make_label(card_layer, &fc_font_small, 302, 128, pal_dim);
    lv_label_set_text(route_unknown_lbl, "ROUTE UNKNOWN");
    lv_obj_add_flag(route_unknown_lbl, LV_OBJ_FLAG_HIDDEN);

    const char *labels[3] = {"GROUND SPEED", "ALTITUDE", "DISTANCE"};
    int xs[3] = {18, 120, 224};
    for (int i = 0; i < 3; i++) {
        stat_labels[i] = make_label(card_layer, &fc_font_label, xs[i], 198,
                                    pal_dim);
        lv_label_set_text(stat_labels[i], labels[i]);
        stat_values[i] = make_label(card_layer, &fc_font_value, xs[i], 222,
                                    pal_secondary);
    }

    memset(&sprite_dsc, 0, sizeof(sprite_dsc));
    sprite_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    sprite_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    sprite_dsc.header.w = 96;
    sprite_dsc.header.h = 96;
    sprite_dsc.header.stride = 96 * 2;
    sprite_dsc.data_size = sizeof(sprite_buf);
    sprite_dsc.data = (const uint8_t *)sprite_buf;
    sprite_img = lv_image_create(card_layer);
    lv_obj_set_pos(sprite_img, 214, 10);
    lv_image_set_src(sprite_img, &sprite_dsc);

    memset(&arrow_head_dsc, 0, sizeof(arrow_head_dsc));
    arrow_head_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    arrow_head_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    arrow_head_dsc.header.w = 10;
    arrow_head_dsc.header.h = 11;
    arrow_head_dsc.header.stride = 10 * 2;
    arrow_head_dsc.data_size = sizeof(arrow_head_buf);
    arrow_head_dsc.data = (const uint8_t *)arrow_head_buf;
    arrow_head_img = lv_image_create(card_layer);
    lv_obj_set_pos(arrow_head_img, 235, 151);
    lv_image_set_src(arrow_head_img, &arrow_head_dsc);

    apply_colours();
    ui_show_clock();
}

void ui_set_palette(int mode)
{
    if (mode != FC_PALETTE_AMBER) mode = FC_PALETTE_CABIN;
    pal_mode = mode;
    const uint32_t *p = (mode == FC_PALETTE_AMBER) ? AMBER : CABIN;
    pal_primary = p[0];
    pal_secondary = p[1];
    pal_dim = p[2];
    apply_colours();
}

int ui_palette(void)
{
    return pal_mode;
}

void ui_refresh_sprite(void)
{
    fill_sprite();
}

void ui_update_clock(void)
{
    struct tm tm;
    clock_get_local(&tm);
    if (tm.tm_year == clock_last_year && tm.tm_yday == clock_last_yday
        && tm.tm_hour == clock_last_hour && tm.tm_min == clock_last_min) {
        return;
    }
    clock_last_year = tm.tm_year;
    clock_last_yday = tm.tm_yday;
    clock_last_hour = tm.tm_hour;
    clock_last_min = tm.tm_min;
    apply_clock_text();
}

void ui_show_clock(void)
{
    showing_card = false;
    lv_obj_add_flag(card_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(clock_layer, LV_OBJ_FLAG_HIDDEN);
    apply_clock_text();
}

void ui_show_card(const card_msg_t *msg)
{
    if (msg == NULL || !msg->valid) {
        ui_show_clock();
        return;
    }

    char callsign[24];
    const char *base = msg->flight.callsign[0] ? msg->flight.callsign
                                               : msg->flight.hex;
    fc_ellipsize(callsign, sizeof(callsign), base, 9);
    set_label_left(callsign_lbl, &fc_font_callsign, 18, 40, callsign);

    char airline[48];
    fc_upper_ellipsize(airline, sizeof(airline), msg->airline, 26);
    set_label_left(airline_lbl, &fc_font_airline, 18, 60, airline);

    const char *name = msg->type_name[0] ? msg->type_name
                                         : fc_type_name(msg->flight.type);
    char line1[64], line2[64];
    fc_wrap_type(name, 18, line1, line2);
    set_label_left(type1_lbl, &fc_font_body, 18, 99, line1);
    set_label_left(type2_lbl, &fc_font_body, 18, 120, line2);

    route_origin_missing = missing_code(msg->origin);
    route_dest_missing = missing_code(msg->dest);
    bool both_missing = route_origin_missing && route_dest_missing;
    if (!msg->has_route || both_missing) {
        lv_obj_remove_flag(route_unknown_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(origin_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dest_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(arrow_shaft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(arrow_head_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(route_unknown_lbl, "ROUTE UNKNOWN");
    lv_obj_add_flag(route_unknown_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(origin_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(dest_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(arrow_shaft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(arrow_head_img, LV_OBJ_FLAG_HIDDEN);
        set_label_left(origin_lbl, &fc_font_route, 18, 164,
                       route_origin_missing ? "\xE2\x80\x94" : msg->origin);
        set_label_right(dest_lbl, &fc_font_route, 302, 164,
                        route_dest_missing ? "\xE2\x80\x94" : msg->dest);
        set_route_colours();
    }

    char value[24];
    fc_format_speed(value, sizeof(value), msg->flight.gs_kt);
    set_label_left(stat_values[0], &fc_font_value, 18, 222, value);
    fc_format_altitude(value, sizeof(value), msg->flight.alt_ft);
    set_label_left(stat_values[1], &fc_font_value, 120, 222, value);
    fc_format_distance(value, sizeof(value), msg->flight.dist_km);
    set_label_left(stat_values[2], &fc_font_value, 224, 222, value);

    char reg[48];
    const char *code = msg->flight.type;
    const char *registration = msg->flight.reg;
    if (code[0] && registration[0]) {
        snprintf(reg, sizeof(reg), "%s \xC2\xB7 %s", code, registration);
    } else {
        snprintf(reg, sizeof(reg), "%s", code[0] ? code : registration);
    }
    char reg_short[48];
    fc_ellipsize(reg_short, sizeof(reg_short), reg, 17);
    set_label_right(reg_lbl, &fc_font_small, 302, 128, reg_short);

    const fc_sprite_t *s = fc_sprite_find(msg->flight.type, msg->flight.category);
    if (s != NULL) {
        if (strcmp(last_sprite, s->name) != 0) {
            snprintf(last_sprite, sizeof(last_sprite), "%s", s->name);
        }
        showing_card = true;
        fill_sprite();
    }

    lv_obj_add_flag(clock_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(card_layer, LV_OBJ_FLAG_HIDDEN);
    showing_card = true;
    lv_obj_invalidate(card_layer);
}
