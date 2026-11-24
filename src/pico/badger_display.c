/*
 * UC8151 setup and FAST/TURBO waveforms from Pimoroni's conference firmware:
 * https://github.com/pimoroni/pimoroni-pico/blob/74cf9b7cacc050f2e6c62e5cd57d9067460c658b/drivers/uc8151/uc8151.cpp
 * Differential and maintenance waveforms derived from antirez's UC8151 driver:
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
    UI_SCRUB_PAD = 3,
    POWER_TIMEOUT_MS = 1000,
    BOOT_REFRESH_TIMEOUT_MS = 10000,
    SHORT_REFRESH_TIMEOUT_MS = 2000,
    FAST_REFRESH_TIMEOUT_MS = 3000,
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

static enum { OFFLINE, READY, REFRESHING } display_state;
static absolute_time_t refresh_deadline;
static enum waveform {
    WAVEFORM_DIFF,
    WAVEFORM_MAINTENANCE,
    WAVEFORM_FAST_CHANGED,
    WAVEFORM_FAST_FULL,
    WAVEFORM_TURBO_HUD,
} active_waveform;

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

static void load_waveform(enum waveform mode)
{
    // antirez's no-flickering speed 5 at 100 Hz. For transitions, drive
    // 16 frames of VDL for BW or VDH for WB, with VCOM at DC-VCOM.
    static const uint8_t diff_vcom[44] = {0x00, 4, 4, 4, 4, 1};
    static const uint8_t diff_bw[42] = {0x80, 16, 0, 0, 0, 1};
    static const uint8_t diff_wb[42] = {0x40, 16, 0, 0, 0, 1};
    // antirez's full speed 4 at 100 Hz (p=8). WW/BB each spend 16 frames
    // at the opposite polarity, then 16 at the target. BW/WB instead drive
    // the target for all 32 frames, with matching durations for both colors.
    static const uint8_t maintenance_vcom[44] = {0x00,  8,  8, 8, 8, 1};
    static const uint8_t maintenance_ww[42] =   {0x60, 16, 16, 0, 0, 1};
    static const uint8_t maintenance_bw[42] =   {0x80, 32,  0, 0, 0, 1};
    static const uint8_t maintenance_wb[42] =   {0x40, 32,  0, 0, 0, 1};
    static const uint8_t maintenance_bb[42] =   {0x90, 16, 16, 0, 0, 1};
    // Pimoroni TURBO, retained for the shorter HUD-only scrub.
    static const uint8_t turbo_vcom[44] = {
        0x00, 1, 1, 2, 0, 1,
        0x00, 2, 2, 0, 0, 2,
        0x00, 2, 2, 3, 0, 2,
    };
    static const uint8_t turbo_bw[42] = {
        0x54, 1, 1, 2, 0, 1,
        0x60, 2, 2, 0, 0, 2,
        0xa8, 2, 2, 3, 0, 2,
    };
    static const uint8_t turbo_wb[42] = {
        0xa8, 1, 1, 2, 0, 1,
        0x60, 2, 2, 0, 0, 2,
        0x54, 2, 2, 3, 0, 2,
    };
    // Pimoroni fast_luts(), without its 200 Hz PLL change. For a full
    // clean, drive pixels by target color even when OLD and NEW agree.
    static const uint8_t fast_vcom[44] = {
        0x00, 4, 4, 7, 0, 1,
        0x00, 12, 12, 0, 0, 2,
        0x00, 4, 4, 7, 0, 2,
    };
    static const uint8_t fast_bw[42] = {
        0x54, 4, 4, 7, 0, 1,
        0x60, 12, 12, 0, 0, 2,
        0xa8, 4, 4, 7, 0, 2,
    };
    static const uint8_t fast_wb[42] = {
        0xa8, 4, 4, 7, 0, 1,
        0x60, 12, 12, 0, 0, 2,
        0x54, 4, 4, 7, 0, 2,
    };
    static const uint8_t neutral[42] = {0};
    if (mode == WAVEFORM_MAINTENANCE) {
        command(LUT_VCOM, maintenance_vcom, sizeof(maintenance_vcom));
        command(LUT_WW, maintenance_ww, sizeof(maintenance_ww));
        command(LUT_BW, maintenance_bw, sizeof(maintenance_bw));
        command(LUT_WB, maintenance_wb, sizeof(maintenance_wb));
        command(LUT_BB, maintenance_bb, sizeof(maintenance_bb));
    } else {
        const bool diff = mode == WAVEFORM_DIFF;
        const bool turbo = mode == WAVEFORM_TURBO_HUD;
        const bool full = mode == WAVEFORM_FAST_FULL;
        command(LUT_VCOM, diff ? diff_vcom : turbo ? turbo_vcom : fast_vcom, sizeof(fast_vcom));
        command(LUT_WW, full ? fast_bw : neutral, sizeof(neutral));
        command(LUT_BW, diff ? diff_bw : turbo ? turbo_bw : fast_bw, sizeof(fast_bw));
        command(LUT_WB, diff ? diff_wb : turbo ? turbo_wb : fast_wb, sizeof(fast_wb));
        command(LUT_BB, full ? fast_wb : neutral, sizeof(neutral));
    }
    active_waveform = mode;
}

static void select_waveform(enum waveform mode)
{
    if (mode != active_waveform) {
        load_waveform(mode);
    }
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

// Bounds are half-open in 8x8 tiles.
static void pack_frame(const uint8_t *frame, unsigned x_begin, unsigned y_begin,
                       unsigned x_end, unsigned y_end)
{
    for (unsigned x = x_begin; x < x_end; ++x) {
        uint8_t *block = panel_frame + x * PANEL_BLOCK_BYTES;
        for (unsigned y = y_begin; y < y_end; ++y) {
            uint64_t tile = transpose_tile(frame + y * 8 * FRAME_STRIDE + x);
            // Each packed output byte holds eight vertical pixels, MSB first.
            for (unsigned col = 8; col-- > 0;) {
                block[col * PANEL_COLUMN_BYTES + y] = (uint8_t)tile;
                tile >>= 8;
            }
        }
    }
}

// Flip the half-open landscape rectangle in the packed portrait plane.
static void invert_rect(unsigned left, unsigned top, unsigned right, unsigned bottom)
{
    const unsigned first_byte = top / 8;
    const unsigned last_byte = (bottom - 1) / 8;
    const uint8_t first_mask = 0xffu >> (top % 8);
    const uint8_t last_mask = (uint8_t)(0xffu << (7 - (bottom - 1) % 8));
    if (first_byte == last_byte) {
        const uint8_t mask = first_mask & last_mask;
        for (unsigned x = left; x < right; ++x) {
            panel_frame[x * PANEL_COLUMN_BYTES + first_byte] ^= mask;
        }
        return;
    }
    for (unsigned x = left; x < right; ++x) {
        uint8_t *column = panel_frame + x * PANEL_COLUMN_BYTES;
        column[first_byte] ^= first_mask;
        for (unsigned y = first_byte + 1; y < last_byte; ++y) {
            column[y] ^= 0xff;
        }
        column[last_byte] ^= last_mask;
    }
}

static void write_panel_plane(uint8_t plane)
{
    command(plane, panel_frame, sizeof(panel_frame));
    command_only(DSP);
}

static void start_refresh(unsigned timeout_ms)
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
    command(CDI, (const uint8_t[]){CDI_OTP}, 1);
    // At 200 Hz BUSY can complete without producing a visible partial
    // update on the Universe badge. Keep the 100 Hz clock for all modes.
    command(PLL, (const uint8_t[]){PLL_100_HZ}, 1);
    command_only(PON);
    sleep_ms(1);
    if (!wait_idle()) {
        return panel_failed();
    }

    command_only(PTOU);
    memset(panel_frame, 0xff, sizeof(panel_frame));
    write_panel_plane(DTM2);
    start_refresh(BOOT_REFRESH_TIMEOUT_MS);
    while (!refresh_complete()) {
        if (time_reached(refresh_deadline)) {
            return panel_failed();
        }
        sleep_ms(1);
    }
    // As in the badge firmware, leave the booster on across game frames so
    // submit never waits for a power-on cycle.
    command(PSR, (const uint8_t[]){PANEL_SETTINGS | LUT_FROM_REGISTERS}, 1);
    command(CDI, (const uint8_t[]){CDI_DIFFERENTIAL}, 1);
    load_waveform(WAVEFORM_DIFF);
    display_state = READY;
    return true;
}

bool badger_display_ready(void)
{
    if (display_state == REFRESHING) {
        if (!refresh_complete()) {
            return time_reached(refresh_deadline) ? panel_failed() : false;
        }
        display_state = READY;
    }
    return display_state == READY;
}

void badger_display_submit(const uint8_t *frame, enum badger_update_mode mode)
{
    if (display_state != READY) {
        return;
    }
    select_waveform(mode == BADGER_UPDATE_DIFF ? WAVEFORM_DIFF :
                    mode == BADGER_UPDATE_MAINTENANCE ? WAVEFORM_MAINTENANCE : WAVEFORM_FAST_FULL);
    write_panel_plane(DTM1);
    pack_frame(frame, 0, 0, FRAME_STRIDE, PANEL_COLUMN_BYTES);
    write_panel_plane(DTM2);
    start_refresh(mode == BADGER_UPDATE_CLEAN ? FAST_REFRESH_TIMEOUT_MS : SHORT_REFRESH_TIMEOUT_MS);
}

void badger_display_submit_ui(const uint8_t *frame, const uint8_t *previous)
{
    if (display_state != READY) {
        return;
    }
    unsigned left = BADGER_WIDTH, top = BADGER_HEIGHT;
    unsigned right = 0, bottom = 0;
    for (unsigned y = 0; y < BADGER_HEIGHT; ++y) {
        for (unsigned byte = 0; byte < FRAME_STRIDE; ++byte) {
            const unsigned offset = y * FRAME_STRIDE + byte;
            const unsigned diff = frame[offset] ^ previous[offset];
            if (!diff) continue;
            if (y < top) top = y;
            bottom = y + 1;

            // Bit 7 is leftmost. Only examine bits in bytes that can
            // extend an edge beyond the current bounds.
            const unsigned x = byte * 8;
            if (x < left) {
                const unsigned first = x + __builtin_clz(diff << 24);
                if (first < left) left = first;
            }
            if (x + 8 > right) {
                const unsigned after = x + 8 - __builtin_ctz(diff);
                if (after > right) right = after;
            }
        }
    }
    if (bottom == 0) {
        return;
    }

    select_waveform(WAVEFORM_FAST_CHANGED);
    // Only the tiles intersecting the difference need repacking. All
    // other cached pixels (including in the margin) already match NEW.
    pack_frame(frame, left / 8, top / 8, (right + 7) / 8, (bottom + 7) / 8);
    left = left > UI_SCRUB_PAD ? left - UI_SCRUB_PAD : 0;
    top = top > UI_SCRUB_PAD ? top - UI_SCRUB_PAD : 0;
    right = right < BADGER_WIDTH - UI_SCRUB_PAD ? right + UI_SCRUB_PAD : BADGER_WIDTH;
    bottom = bottom < BADGER_HEIGHT - UI_SCRUB_PAD ? bottom + UI_SCRUB_PAD : BADGER_HEIGHT;

    // Present the opposite OLD color only inside the padded rectangle so
    // FAST drives its background too. Outside it, the WW/BB LUTs are zero.
    invert_rect(left, top, right, bottom);
    write_panel_plane(DTM1);
    invert_rect(left, top, right, bottom);
    write_panel_plane(DTM2);
    start_refresh(FAST_REFRESH_TIMEOUT_MS);
}

void badger_display_clean_hud(const uint8_t *frame, unsigned first_row)
{
    if (display_state != READY || first_row >= BADGER_HEIGHT) {
        return;
    }
    select_waveform(WAVEFORM_TURBO_HUD);
    const unsigned first_byte = first_row / 8;
    // The boundary tile may also contain world rows; those match the
    // cached frame. Make only the HUD look changed in the OLD plane.
    pack_frame(frame, 0, first_byte, FRAME_STRIDE, PANEL_COLUMN_BYTES);
    invert_rect(0, first_row, BADGER_WIDTH, BADGER_HEIGHT);
    write_panel_plane(DTM1);
    invert_rect(0, first_row, BADGER_WIDTH, BADGER_HEIGHT);
    write_panel_plane(DTM2);
    start_refresh(SHORT_REFRESH_TIMEOUT_MS);
}
