#ifndef BADGER_TONE_H
#define BADGER_TONE_H

#include <stdint.h>

// Convert one scene palette entry (8-bit RGB) to the Badger's dither input.
static inline uint8_t badger_scene_tone(unsigned r, unsigned g, unsigned b) {
    // Give reds more weight and greens less than standard luma, so similarly
    // lit warm and green surfaces need not share the same dither density.
    // Positive weights preserve the light ramp; neutral greys are unchanged.
    unsigned tone = (27 * r + 28 * g + 9 * b + 32) >> 6;
    // Retain the existing shadow lift without changing black or white.
    return tone + tone * (255 - tone) / 512;
}

#endif
