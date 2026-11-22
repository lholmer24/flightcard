/*
 * Flightcard - ST7789V + LVGL display driver.
 *
 * SPI0: SCK GP18, MOSI GP19, CS GP17, DC GP16, backlight GP20.
 * The PIM715 pack has no reset GPIO, so use ST7789 software reset.
 *
 * SPDX-License-Identifier: MIT
 */
#include "display.h"
#include "log.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "lvgl.h"

#define PIN_CS 17
#define PIN_DC 16
#define PIN_BL 20
#define PIN_SCK 18
#define PIN_MOSI 19

#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_NORON 0x13
#define ST7789_INVON 0x21
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3A

/* Rotation 270 for a 320 x 240 logical display (MX | MV). */
#ifndef ST7789_ROTATION_MADCTL
#define ST7789_ROTATION_MADCTL 0x60
#endif
#ifndef ST7789_X_OFFSET
#define ST7789_X_OFFSET 0
#endif
#ifndef ST7789_Y_OFFSET
#define ST7789_Y_OFFSET 0
#endif

#define SPI_HZ (40u * 1000u * 1000u)

#ifndef FC_DISPLAY_DMA
#define FC_DISPLAY_DMA 1
#endif

static int dma_chan = -1;
static volatile bool dma_busy;
static lv_display_t *flush_disp;

static uint16_t draw_buf1[FC_DISPLAY_W * 40];
static uint16_t draw_buf2[FC_DISPLAY_W * 40];

static inline void cs(bool level)
{
    gpio_put(PIN_CS, level ? 1 : 0);
}

static inline void dc(bool data)
{
    gpio_put(PIN_DC, data ? 1 : 0);
}

static void st7789_cmd(uint8_t cmd)
{
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    dc(false);
    cs(false);
    spi_write_blocking(spi0, &cmd, 1);
    cs(true);
}

static void st7789_data(const uint8_t *data, size_t len)
{
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    dc(true);
    cs(false);
    spi_write_blocking(spi0, data, len);
    cs(true);
}

static void st7789_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    st7789_cmd(cmd);
    if (len) st7789_data(data, len);
}

static void dma_irq_handler(void)
{
    dma_hw->ints0 = 1u << (unsigned)dma_chan;
    /*
     * DMA finishes when the last word reaches the SPI TX FIFO, not when it has
     * been shifted out. Wait for the shifter to go idle before raising CS, or
     * the final pixels of the flush are truncated (visible at the screen's
     * bottom-right corner, which is the end of the last partial area).
     */
    while (spi_is_busy(spi0)) tight_loop_contents();
    cs(true);
    dma_busy = false;
    if (flush_disp != NULL) {
        lv_display_t *disp = flush_disp;
        flush_disp = NULL;
        lv_display_flush_ready(disp);
    }
}

static void st7789_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t buf[4];
    x0 += ST7789_X_OFFSET;
    x1 += ST7789_X_OFFSET;
    y0 += ST7789_Y_OFFSET;
    y1 += ST7789_Y_OFFSET;
    buf[0] = (uint8_t)(x0 >> 8);
    buf[1] = (uint8_t)(x0 & 0xFF);
    buf[2] = (uint8_t)(x1 >> 8);
    buf[3] = (uint8_t)(x1 & 0xFF);
    st7789_cmd_data(ST7789_CASET, buf, 4);
    buf[0] = (uint8_t)(y0 >> 8);
    buf[1] = (uint8_t)(y0 & 0xFF);
    buf[2] = (uint8_t)(y1 >> 8);
    buf[3] = (uint8_t)(y1 & 0xFF);
    st7789_cmd_data(ST7789_RASET, buf, 4);
    st7789_cmd(ST7789_RAMWR);
}

static void display_flush(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map)
{
    if (area == NULL || px_map == NULL) {
        lv_display_flush_ready(disp);
        return;
    }

    /* The previous transfer must be complete before a new window. */
    while (dma_busy) tight_loop_contents();

    st7789_set_window(area->x1, area->y1, area->x2, area->y2);

    size_t count = (size_t)(area->x2 - area->x1 + 1)
                   * (size_t)(area->y2 - area->y1 + 1);

    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    dc(true);
    cs(false);

#if FC_DISPLAY_DMA
    dma_channel_config cfg = dma_channel_get_default_config((uint)dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, spi_get_dreq(spi0, true));

    flush_disp = disp;
    dma_busy = true;
    dma_channel_configure((uint)dma_chan, &cfg,
                          &spi_get_hw(spi0)->dr, px_map,
                          count, true);
#else
    spi_write16_blocking(spi0, (const uint16_t *)px_map, count);
    cs(true);
    lv_display_flush_ready(disp);
#endif
}

static uint32_t lv_tick_cb(void)
{
    return (uint32_t)to_ms_since_boot(get_absolute_time());
}

static void backlight_init(void)
{
    gpio_set_function(PIN_BL, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_BL);
    pwm_config cfg = pwm_get_default_config();
    /* 150 MHz / (10 * 1500) = 10 kHz. */
    pwm_config_set_clkdiv(&cfg, 10.0f);
    pwm_config_set_wrap(&cfg, 1499);
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(PIN_BL, 0);
}

void display_set_backlight(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    pwm_set_gpio_level(PIN_BL, (uint16_t)(value * 1500.0f));
}

static void st7789_init(void)
{
    st7789_cmd(ST7789_SWRESET);
    sleep_ms(150);
    st7789_cmd(ST7789_SLPOUT);
    sleep_ms(120);

    const uint8_t colmod = 0x55; /* 16 bits/pixel */
    st7789_cmd_data(ST7789_COLMOD, &colmod, 1);
    const uint8_t madctl = ST7789_ROTATION_MADCTL;
    st7789_cmd_data(ST7789_MADCTL, &madctl, 1);

    const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    st7789_cmd_data(0xB2, porch, sizeof(porch));
    const uint8_t gate = 0x35;
    st7789_cmd_data(0xB7, &gate, 1);
    const uint8_t vcom = 0x19;
    st7789_cmd_data(0xBB, &vcom, 1);
    const uint8_t lcm = 0x2C;
    st7789_cmd_data(0xC0, &lcm, 1);
    const uint8_t vdvrh = 0x01;
    st7789_cmd_data(0xC2, &vdvrh, 1);
    const uint8_t vrh = 0x12;
    st7789_cmd_data(0xC3, &vrh, 1);
    const uint8_t vdvs = 0x20;
    st7789_cmd_data(0xC4, &vdvs, 1);
    const uint8_t fr = 0x0F;
    st7789_cmd_data(0xC6, &fr, 1);
    const uint8_t pw[] = {0xA4, 0xA1};
    st7789_cmd_data(0xD0, pw, sizeof(pw));

    const uint8_t gamma_p[] = {
        0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
        0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
    };
    st7789_cmd_data(0xE0, gamma_p, sizeof(gamma_p));
    const uint8_t gamma_n[] = {
        0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
        0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
    };
    st7789_cmd_data(0xE1, gamma_n, sizeof(gamma_n));

    st7789_cmd(ST7789_INVON);
    st7789_cmd(ST7789_NORON);
    st7789_cmd(ST7789_DISPON);
    sleep_ms(100);
}

void display_init(void)
{
    gpio_init(PIN_CS);
    gpio_init(PIN_DC);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    cs(true);
    dc(true);

    spi_init(spi0, SPI_HZ);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    backlight_init();
    st7789_init();

#if FC_DISPLAY_DMA
    dma_chan = dma_claim_unused_channel(false);
    if (dma_chan < 0) {
        LOG_E("display: no DMA channel");
    } else {
        dma_channel_set_irq0_enabled((uint)dma_chan, true);
        irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
        irq_set_enabled(DMA_IRQ_0, true);
        LOG_I("display: DMA channel %d", dma_chan);
    }
#else
    LOG_I("display: blocking SPI flush");
#endif

    lv_init();
    lv_tick_set_cb(lv_tick_cb);

    lv_display_t *disp = lv_display_create(FC_DISPLAY_W, FC_DISPLAY_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, display_flush);
    lv_display_set_buffers(disp, draw_buf1, draw_buf2, sizeof(draw_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(disp);

    display_set_backlight(0.0f);
    LOG_I("display: %dx%d ST7789 rotation=270, SPI %u MHz",
          FC_DISPLAY_W, FC_DISPLAY_H, SPI_HZ / 1000000u);
}

void display_poll(void)
{
    lv_timer_handler();
}

void display_test_pattern(void)
{
    static const uint16_t colours[] = {
        0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000,
    };
    uint16_t line[FC_DISPLAY_W];
    for (unsigned c = 0; c < sizeof(colours) / sizeof(colours[0]); c++) {
        for (int i = 0; i < FC_DISPLAY_W; i++) line[i] = colours[c];
        st7789_set_window(0, 0, FC_DISPLAY_W - 1, FC_DISPLAY_H - 1);
        spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        dc(true);
        cs(false);
        for (int y = 0; y < FC_DISPLAY_H; y++) {
            spi_write16_blocking(spi0, line, FC_DISPLAY_W);
        }
        cs(true);
        sleep_ms(350);
    }
}
