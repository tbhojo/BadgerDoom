/*
 * UC8151 setup derived from Pimoroni's conference firmware:
 * https://github.com/pimoroni/pimoroni-pico/blob/74cf9b7cacc050f2e6c62e5cd57d9067460c658b/drivers/uc8151/uc8151.cpp
 * Differential waveforms derived from antirez's UC8151 driver:
 * https://github.com/antirez/uc8151_micropython/blob/main/uc8151.py
 *
 * MIT License
 * Copyright (c) 2021 Pimoroni Ltd
 * Copyright (c) 2024 Salvatore Sanfilippo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "badger_display.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/structs/io_bank0.h"
#include "pico/time.h"

#define PANEL_SPI SPI_INSTANCE(BADGER_PANEL_SPI)

// Panel addresses are portrait: one gate per landscape column, with 16
// vertical bytes per column. Pack eight adjacent columns per transpose.
enum {
    FRAME_STRIDE = BADGER_WIDTH / 8,
    PANEL_COLUMN_BYTES = BADGER_HEIGHT / 8,
    PANEL_BLOCK_BYTES = 8 * PANEL_COLUMN_BYTES,
    POWER_TIMEOUT_MS = 1000,
    FULL_REFRESH_TIMEOUT_MS = 10000,
    DIFFERENTIAL_REFRESH_TIMEOUT_MS = 2000,
    IDLE_CLEAN_MIN_UPDATES = 16,
    IDLE_CLEAN_DELAY_MS = 5000,
};
_Static_assert(FRAME_STRIDE * BADGER_HEIGHT == BADGER_FRAME_BYTES, "bad frame size");
_Static_assert(FRAME_STRIDE * PANEL_BLOCK_BYTES == BADGER_FRAME_BYTES, "bad panel frame size");

// UC8151 commands and panel setting flags for the badge's landscape rotation.
enum {
    PSR = 0x00, PWR = 0x01, PFS = 0x03, PON = 0x04, BTST = 0x06,
    DTM1 = 0x10, DSP = 0x11, DRF = 0x12, DTM2 = 0x13,
    LUT_VCOM = 0x20, LUT_WW = 0x21, LUT_BW = 0x22, LUT_WB = 0x23, LUT_BB = 0x24,
    PLL = 0x30, TSE = 0x41, CDI = 0x50, TCON = 0x60,
    PTOU = 0x92,
    PANEL_SETTINGS = 0x97, // 128x296, B/W, shift right/scan down, booster and reset release
    LUT_FROM_REGISTERS = 0x20,
    PLL_100_HZ = 0x3a,
    // Both select DDX=01 (1=white in OLD and NEW); only the border changes.
    // See UC8151c_B v0.71 p.19 and UC8151d_B v0.6 p.20.
    CDI_OTP = 0x9c, // Pimoroni's inverted-mode border and porch
    CDI_DIFFERENTIAL = 0xdc, // floating border, same porch
};

static enum { OFFLINE, READY, DIFFERENTIAL_REFRESHING, FULL_REFRESHING } display_state;
static absolute_time_t refresh_deadline;
static absolute_time_t idle_clean_deadline;
static unsigned updates_since_clean;
static bool idle_clean_armed;

// Last submitted panel-order image. It becomes the OLD plane for the next
// update only after BUSY_N confirms the previous refresh completed.
static uint8_t panel_frame[BADGER_FRAME_BYTES];

static void output_pin(uint pin, bool initial)
{
    gpio_init(pin);
    gpio_put(pin, initial);
    gpio_set_dir(pin, GPIO_OUT);
}

static void begin_command(uint8_t reg)
{
    gpio_put(BADGER_PANEL_DC_PIN, 0);
    gpio_put(BADGER_PANEL_CS_PIN, 0);
    spi_write_blocking(PANEL_SPI, &reg, 1);
}

static void command(uint8_t reg, const uint8_t *data, size_t len)
{
    begin_command(reg);
    if (len != 0) {
        gpio_put(BADGER_PANEL_DC_PIN, 1);
        spi_write_blocking(PANEL_SPI, data, len);
    }
    gpio_put(BADGER_PANEL_CS_PIN, 1);
}

static void command_only(uint8_t reg)
{
    command(reg, NULL, 0);
}

static bool wait_idle(void)
{
    absolute_time_t deadline = make_timeout_time_ms(POWER_TIMEOUT_MS);
    while (!gpio_get(BADGER_PANEL_BUSY_PIN)) {
        if (time_reached(deadline)) {
            return false;
        }
        sleep_us(100);
    }
    return true;
}

static void load_differential_waveform(void)
{
    // antirez's no-flickering speed 5 at 100 Hz. Drive only transitions:
    // 16 frames of VDL for BW or VDH for WB, with VCOM at DC-VCOM.
    // WW and BB stay neutral; reasserting either color would bias the panel.
    static const uint8_t vcom[44] = {0x00, 4, 4, 4, 4, 1};
    static const uint8_t bw[42] = {0x80, 16, 0, 0, 0, 1};
    static const uint8_t wb[42] = {0x40, 16, 0, 0, 0, 1};
    static const uint8_t unchanged[42] = {0};
    command(PSR, (const uint8_t[]){PANEL_SETTINGS | LUT_FROM_REGISTERS}, 1);
    command(CDI, (const uint8_t[]){CDI_DIFFERENTIAL}, 1);
    command(LUT_VCOM, vcom, sizeof(vcom));
    command(LUT_WW, unchanged, sizeof(unchanged));
    command(LUT_BW, bw, sizeof(bw));
    command(LUT_WB, wb, sizeof(wb));
    command(LUT_BB, unchanged, sizeof(unchanged));
}

static uint64_t transpose_tile(const uint8_t *src)
{
    // Gather rows top to bottom, each with its leftmost pixel in bit 7.
    uint64_t tile = 0;
    for (unsigned row = 0; row < 8; ++row) {
        tile = (tile << 8) | src[row * FRAME_STRIDE];
    }

    // Swap row and column address bits across the 8x8 diagonal.
    uint64_t swap = (tile ^ (tile >> 7)) & UINT64_C(0x00aa00aa00aa00aa);
    tile ^= swap ^ (swap << 7);
    swap = (tile ^ (tile >> 14)) & UINT64_C(0x0000cccc0000cccc);
    tile ^= swap ^ (swap << 14);
    swap = (tile ^ (tile >> 28)) & UINT64_C(0x00000000f0f0f0f0);
    return tile ^ swap ^ (swap << 28);
}

static void pack_frame(const uint8_t *frame)
{
    for (unsigned x = 0; x < FRAME_STRIDE; ++x) {
        uint8_t *block = panel_frame + x * PANEL_BLOCK_BYTES;
        for (unsigned y = 0; y < PANEL_COLUMN_BYTES; ++y) {
            uint64_t tile = transpose_tile(frame + y * 8 * FRAME_STRIDE + x);
            // Each packed output byte holds eight vertical pixels, MSB first.
            for (unsigned col = 8; col-- > 0;) {
                block[col * PANEL_COLUMN_BYTES + y] = (uint8_t)tile;
                tile >>= 8;
            }
        }
    }
}

static void write_panel_plane(uint8_t plane)
{
    command(plane, panel_frame, sizeof(panel_frame));
    command_only(DSP);
}

static void start_refresh(bool full)
{
    gpio_acknowledge_irq(BADGER_PANEL_BUSY_PIN, GPIO_IRQ_EDGE_FALL);
    command_only(DRF);
    refresh_deadline = make_timeout_time_ms(full ? FULL_REFRESH_TIMEOUT_MS
                                                 : DIFFERENTIAL_REFRESH_TIMEOUT_MS);
    display_state = full ? FULL_REFRESHING : DIFFERENTIAL_REFRESHING;
}

static bool refresh_complete(void)
{
    // BUSY_N may still be high just after DRF. Require a latched falling edge
    // as well as the idle level, even if the game didn't poll during the pulse.
    // Read the raw event without enabling the GPIO IRQ or installing a handler.
    const uint pin = BADGER_PANEL_BUSY_PIN;
    const uint32_t falling = GPIO_IRQ_EDGE_FALL << (4 * (pin % 8));
    return (io_bank0_hw->intr[pin / 8] & falling) && gpio_get(pin);
}

static bool panel_failed(void)
{
    // Stop the controller without dropping the board's 3V3 power latch.
    gpio_put(BADGER_PANEL_RESET_PIN, 0);
    display_state = OFFLINE;
    return false;
}

bool badger_display_init(void)
{
    display_state = OFFLINE;
    updates_since_clean = 0;
    idle_clean_armed = false;
    output_pin(BADGER_POWER_ENABLE_PIN, true);
    output_pin(BADGER_PANEL_CS_PIN, true);
    output_pin(BADGER_PANEL_DC_PIN, false);
    output_pin(BADGER_PANEL_RESET_PIN, false);
    gpio_init(BADGER_PANEL_BUSY_PIN);
    gpio_pull_up(BADGER_PANEL_BUSY_PIN);
    spi_init(PANEL_SPI, 12000000);
    spi_set_format(PANEL_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(BADGER_PANEL_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(BADGER_PANEL_MOSI_PIN, GPIO_FUNC_SPI);

    sleep_ms(10);
    gpio_put(BADGER_PANEL_RESET_PIN, 1);
    sleep_ms(10);
    if (!wait_idle()) {
        return panel_failed();
    }

    // OTP waveform for the initial full refresh. Use Pimoroni's panel power,
    // booster, internal temperature sensor, timing and 1=white settings.
    command(PSR, (const uint8_t[]){PANEL_SETTINGS}, 1);
    command(PWR, (const uint8_t[]){0x03, 0x00, 0x2b, 0x2b, 0x2b}, 5);
    command(BTST, (const uint8_t[]){0x17, 0x17, 0x17}, 3);
    command(PFS, (const uint8_t[]){0x00}, 1);
    command(TSE, (const uint8_t[]){0x00}, 1);
    command(TCON, (const uint8_t[]){0x22}, 1);
    command(CDI, (const uint8_t[]){CDI_OTP}, 1);
    // Keep this clock for both waveforms. At 200 Hz BUSY can complete
    // without producing a visible partial update on the Universe badge.
    command(PLL, (const uint8_t[]){PLL_100_HZ}, 1);
    command_only(PON);
    sleep_ms(1);
    if (!wait_idle()) {
        return panel_failed();
    }

    command_only(PTOU);
    memset(panel_frame, 0xff, sizeof(panel_frame));
    write_panel_plane(DTM2);
    start_refresh(true);
    while (!refresh_complete()) {
        if (time_reached(refresh_deadline)) {
            return panel_failed();
        }
        sleep_ms(1);
    }
    // As in the badge firmware, leave the booster on across game frames so
    // submit never waits for a power-on cycle.
    load_differential_waveform();
    display_state = READY;
    return true;
}

bool badger_display_ready(bool quiet)
{
    if (!quiet) {
        idle_clean_armed = false;
    }
    if (display_state == DIFFERENTIAL_REFRESHING || display_state == FULL_REFRESHING) {
        if (!refresh_complete()) {
            return time_reached(refresh_deadline) ? panel_failed() : false;
        }
        if (display_state == FULL_REFRESHING) {
            load_differential_waveform();
            updates_since_clean = 0;
        } else if (updates_since_clean < IDLE_CLEAN_MIN_UPDATES) {
            ++updates_since_clean;
        }
        display_state = READY;
    }
    if (display_state != READY) {
        return false;
    }

    // OTP takes seconds and cannot be interrupted. Only clean a settled
    // frame during a quiet interval explicitly identified by the caller.
    if (quiet && updates_since_clean >= IDLE_CLEAN_MIN_UPDATES) {
        if (!idle_clean_armed) {
            idle_clean_armed = true;
            idle_clean_deadline = make_timeout_time_ms(IDLE_CLEAN_DELAY_MS);
        } else if (time_reached(idle_clean_deadline)) {
            idle_clean_armed = false;
            command(PSR, (const uint8_t[]){PANEL_SETTINGS}, 1);
            command(CDI, (const uint8_t[]){CDI_OTP}, 1);
            write_panel_plane(DTM1);
            write_panel_plane(DTM2);
            start_refresh(true);
            return false;
        }
    }
    return true;
}

void badger_display_submit(const uint8_t *frame)
{
    if (display_state != READY) {
        return;
    }
    idle_clean_armed = false;
    write_panel_plane(DTM1);
    pack_frame(frame);
    write_panel_plane(DTM2);
    start_refresh(false);
}
