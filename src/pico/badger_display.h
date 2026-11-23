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

// Poll regularly, even when the frame is unchanged. May start a full clean
// of the last frame; never waits for the panel. True if ready for a new frame.
bool badger_display_ready(void);

// Call only when ready. Copies a row-major, MSB-first frame (1 = white) to
// the controller over SPI before returning. The panel refresh runs afterwards.
void badger_display_submit(const uint8_t *frame);

#endif
