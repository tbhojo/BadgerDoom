#ifndef BADGER_DISPLAY_H
#define BADGER_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#define BADGER_WIDTH 296
#define BADGER_HEIGHT 128
#define BADGER_FRAME_BYTES 4736

// Initialize the panel and wait for the initial full white refresh.
// Call all display functions from the same core.
bool badger_display_init(void);

enum badger_update_mode {
    BADGER_UPDATE_DIFF,  // Differential, changed pixels only
    BADGER_UPDATE_UI,    // Multiphase FAST, changed pixels only
    BADGER_UPDATE_CLEAN, // Multiphase FAST, all pixels
};

// Poll regularly, even when the frame is unchanged. Never waits for BUSY.
bool badger_display_ready(void);

// Call only when ready. Sends a row-major, MSB-first frame (1 = white)
// over SPI; the refresh runs after return. Skip unchanged frames unless
// requesting a whole-frame clean.
void badger_display_submit(const uint8_t *frame, enum badger_update_mode mode);

// Call only when ready and rows [0, first_row) match the last displayed frame.
// Balanced TURBO drives rows [first_row, BADGER_HEIGHT) only. This still
// occupies the panel for one full-window refresh.
void badger_display_clean_hud(const uint8_t *frame, unsigned first_row);

#endif
