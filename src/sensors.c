/*
 * Flightcard - BH1750 and optional I2C RTC.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sensors.h"
#include "log.h"
#include "timezone.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <string.h>

#define I2C_PORT i2c0
#define PIN_SDA 4
#define PIN_SCL 5
#define BH1750_ADDR 0x23
#define DS3231_ADDR 0x68
#define PCF8563_ADDR 0x51

static bool light_present;
static bool rtc_present;
static int rtc_kind; /* 0 none, 1 DS3231, 2 PCF8563 */

static bool i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    return i2c_write_blocking(I2C_PORT, addr, data, len, false) == (int)len;
}

static bool i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    if (i2c_write_blocking(I2C_PORT, addr, &reg, 1, true) != 1) return false;
    return i2c_read_blocking(I2C_PORT, addr, data, len, false) == (int)len;
}

static bool i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data,
                          size_t len)
{
    uint8_t buf[16];
    if (len + 1 > sizeof(buf)) return false;
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_write_blocking(I2C_PORT, addr, buf, len + 1, false)
           == (int)(len + 1);
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)(((v >> 4) & 0x0F) * 10 + (v & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static void rtc_detect(void)
{
    uint8_t status;
    if (i2c_read(DS3231_ADDR, 0x0F, &status, 1)) {
        rtc_kind = 1;
        rtc_present = true;
        LOG_I("rtc: DS3231 at 0x68%s", (status & 0x80) ? " (OSF set)" : "");
        return;
    }
    uint8_t ctrl;
    if (i2c_read(PCF8563_ADDR, 0x00, &ctrl, 1)) {
        rtc_kind = 2;
        rtc_present = true;
        LOG_I("rtc: PCF8563 at 0x51");
    }
}

void sensors_init(void)
{
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
    sleep_ms(2);

    /* BH1750 continuous high-resolution mode. */
    uint8_t mode = 0x10;
    if (i2c_write(BH1750_ADDR, &mode, 1)) {
        light_present = true;
        LOG_I("light: BH1750 at 0x23");
    } else {
        LOG_I("light: BH1750 absent; fixed brightness and Cabin palette");
    }

    rtc_detect();
}

bool sensors_light_present(void)
{
    return light_present;
}

bool sensors_read_lux(float *lux)
{
    if (!light_present || lux == NULL) return false;
    uint8_t data[2];
    if (!i2c_read(BH1750_ADDR, 0x00, data, 2)) return false;
    uint16_t raw = (uint16_t)((data[0] << 8) | data[1]);
    *lux = (float)raw / 1.2f;
    return true;
}

bool sensors_rtc_present(void)
{
    return rtc_present;
}

bool sensors_rtc_read(uint32_t *epoch)
{
    if (!rtc_present || epoch == NULL) return false;
    uint8_t d[7];
    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    if (rtc_kind == 1) {
        uint8_t status;
        if (!i2c_read(DS3231_ADDR, 0x0F, &status, 1)) return false;
        if (status & 0x80) return false; /* oscillator stopped */
        if (!i2c_read(DS3231_ADDR, 0x00, d, 7)) return false;
        tm.tm_sec = bcd_to_bin(d[0] & 0x7F);
        tm.tm_min = bcd_to_bin(d[1] & 0x7F);
        tm.tm_hour = bcd_to_bin(d[2] & 0x3F);
        tm.tm_mday = bcd_to_bin(d[4] & 0x3F);
        tm.tm_mon = bcd_to_bin(d[5] & 0x1F) - 1;
        tm.tm_year = bcd_to_bin(d[6]) + 100;
    } else if (rtc_kind == 2) {
        if (!i2c_read(PCF8563_ADDR, 0x02, d, 7)) return false;
        if (d[0] & 0x80) return false;
        tm.tm_sec = bcd_to_bin(d[0] & 0x7F);
        tm.tm_min = bcd_to_bin(d[1] & 0x7F);
        tm.tm_hour = bcd_to_bin(d[2] & 0x3F);
        tm.tm_mday = bcd_to_bin(d[3] & 0x3F);
        tm.tm_mon = bcd_to_bin(d[5] & 0x1F) - 1;
        tm.tm_year = bcd_to_bin(d[6]) + 100;
        if (d[5] & 0x80) tm.tm_year += 100;
    } else {
        return false;
    }

    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > 31
        || tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 59) {
        return false;
    }
    uint32_t value = fc_epoch_from_utc(&tm);
    if (value < 1700000000u) return false;
    *epoch = value;
    return true;
}

bool sensors_rtc_write(uint32_t epoch)
{
    if (!rtc_present) return false;
    time_t t = (time_t)epoch;
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) return false;

    uint8_t d[7];
    d[0] = bin_to_bcd((uint8_t)tm.tm_sec);
    d[1] = bin_to_bcd((uint8_t)tm.tm_min);
    d[2] = bin_to_bcd((uint8_t)tm.tm_hour);
    d[3] = bin_to_bcd((uint8_t)tm.tm_wday);
    d[4] = bin_to_bcd((uint8_t)tm.tm_mday);
    d[5] = bin_to_bcd((uint8_t)(tm.tm_mon + 1));
    d[6] = bin_to_bcd((uint8_t)(tm.tm_year % 100));

    if (rtc_kind == 1) {
        return i2c_write_reg(DS3231_ADDR, 0x00, d, 7);
    }
    if (rtc_kind == 2) {
        return i2c_write_reg(PCF8563_ADDR, 0x02, d, 7);
    }
    return false;
}
