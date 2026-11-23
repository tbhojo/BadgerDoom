/*
 * UC8151 setup and waveforms derived from Pimoroni's conference firmware:
 * https://github.com/pimoroni/pimoroni-pico/blob/74cf9b7cacc050f2e6c62e5cd57d9067460c658b/drivers/uc8151/uc8151.cpp
 *
 * MIT License
 * Copyright (c) 2021 Pimoroni Ltd
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
// vertical bytes per column. Stream eight adjacent columns per transpose.
enum {
    FRAME_STRIDE = BADGER_WIDTH / 8,
    PANEL_COLUMN_BYTES = BADGER_HEIGHT / 8,
    PANEL_BLOCK_BYTES = 8 * PANEL_COLUMN_BYTES,
    POWER_TIMEOUT_MS = 1000,
    FULL_REFRESH_TIMEOUT_MS = 10000,
    PARTIAL_REFRESH_TIMEOUT_MS = 2000,
};
_Static_assert(FRAME_STRIDE * BADGER_HEIGHT == BADGER_FRAME_BYTES, "bad frame size");

// UC8151 commands and panel setting flags for the badge's landscape rotation.
enum {
    PSR = 0x00, PWR = 0x01, PFS = 0x03, PON = 0x04, BTST = 0x06,
    DSP = 0x11, DRF = 0x12, DTM2 = 0x13,
    LUT_VCOM = 0x20, LUT_WW = 0x21, LUT_BW = 0x22, LUT_WB = 0x23, LUT_BB = 0x24,
    PLL = 0x30, TSE = 0x41, CDI = 0x50, TCON = 0x60,
    PTL = 0x90, PTIN = 0x91, PTOU = 0x92,
    PANEL_SETTINGS = 0x97, // 128x296, B/W, shift right/scan down, booster and reset release
    LUT_FROM_REGISTERS = 0x20,
    PLL_100_HZ = 0x3a,
};

static enum { OFFLINE, READY, REFRESHING } display_state;
static absolute_time_t refresh_deadline;

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

static void load_partial_waveform(void)
{
    // Pimoroni UPDATE_TURBO. WW/BW and WB/BB use identical waveforms, so
    // only the new image plane is needed; there is no previous-frame copy.
    static const uint8_t vcom[44] = {
        0x00, 1, 1, 2, 0, 1,  0x00, 2, 2, 0, 0, 2,  0x00, 2, 2, 3, 0, 2,
    };
    static const uint8_t white[42] = {
        0x54, 1, 1, 2, 0, 1,  0x60, 2, 2, 0, 0, 2,  0xa8, 2, 2, 3, 0, 2,
    };
    static const uint8_t black[42] = {
        0xa8, 1, 1, 2, 0, 1,  0x60, 2, 2, 0, 0, 2,  0x54, 2, 2, 3, 0, 2,
    };
    command(PSR, (const uint8_t[]){PANEL_SETTINGS | LUT_FROM_REGISTERS}, 1);
    command(LUT_VCOM, vcom, sizeof(vcom));
    command(LUT_WW, white, sizeof(white));
    command(LUT_BW, white, sizeof(white));
    command(LUT_WB, black, sizeof(black));
    command(LUT_BB, black, sizeof(black));
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

static void write_frame(const uint8_t *frame)
{
    uint8_t block[PANEL_BLOCK_BYTES];
    if (frame == NULL) {
        memset(block, 0xff, sizeof(block));
    }

    begin_command(DTM2);
    gpio_put(BADGER_PANEL_DC_PIN, 1);
    for (unsigned x = 0; x < FRAME_STRIDE; ++x) {
        if (frame != NULL) {
            for (unsigned y = 0; y < PANEL_COLUMN_BYTES; ++y) {
                uint64_t tile = transpose_tile(frame + y * 8 * FRAME_STRIDE + x);
                // Each packed output byte holds eight vertical pixels, MSB first.
                for (unsigned col = 8; col-- > 0;) {
                    block[col * PANEL_COLUMN_BYTES + y] = (uint8_t)tile;
                    tile >>= 8;
                }
            }
        }
        spi_write_blocking(PANEL_SPI, block, sizeof(block));
    }
    gpio_put(BADGER_PANEL_CS_PIN, 1);
    command_only(DSP);
}

static void start_refresh(uint32_t timeout_ms)
{
    gpio_acknowledge_irq(BADGER_PANEL_BUSY_PIN, GPIO_IRQ_EDGE_FALL);
    command_only(DRF);
    refresh_deadline = make_timeout_time_ms(timeout_ms);
    display_state = REFRESHING;
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
    command(CDI, (const uint8_t[]){0x9c}, 1);
    // Keep this clock for both waveforms. At 200 Hz BUSY can complete
    // without producing a visible partial update on the Universe badge.
    command(PLL, (const uint8_t[]){PLL_100_HZ}, 1);
    command_only(PON);
    sleep_ms(1);
    if (!wait_idle()) {
        return panel_failed();
    }

    command_only(PTOU);
    write_frame(NULL);
    start_refresh(FULL_REFRESH_TIMEOUT_MS);
    while (!refresh_complete()) {
        if (time_reached(refresh_deadline)) {
            return panel_failed();
        }
        sleep_ms(1);
    }
    // As in the badge firmware, leave the booster on across game frames so
    // submit never waits for a power-on cycle.
    load_partial_waveform();
    display_state = READY;
    return true;
}

bool badger_display_ready(void)
{
    if (display_state == REFRESHING) {
        if (refresh_complete()) {
            command_only(PTOU);
            display_state = READY;
        } else if (time_reached(refresh_deadline)) {
            return panel_failed();
        }
    }
    return display_state == READY;
}

void badger_display_submit(const uint8_t *frame)
{
    if (display_state != READY) {
        return;
    }
    static const uint8_t window[] = {
        0, BADGER_HEIGHT - 1,                    // native source range (8-bit)
        0, 0, (BADGER_WIDTH - 1) >> 8, (BADGER_WIDTH - 1) & 0xff, // gate range (BE)
        1, // PT_SCAN: scan all gates
    };
    command_only(PTIN);
    command(PTL, window, sizeof(window));
    write_frame(frame);
    start_refresh(PARTIAL_REFRESH_TIMEOUT_MS);
}
