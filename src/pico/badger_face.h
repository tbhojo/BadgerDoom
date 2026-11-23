#ifndef BADGER_FACE_H
#define BADGER_FACE_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "badger_display.h"
#include "doom/r_data.h"
#include "doom/st_stuff.h"
#include "w_wad.h"
#include "z_zone.h"

enum {
    BADGER_FACE_WIDTH = 18,
    BADGER_FACE_HEIGHT = 17,
    BADGER_FACE_ROW_BYTES = 3,
    // Fixed bounds of the stock poses in status-bar coordinates. Respect the
    // patch offsets so the head doesn't jump when the expression changes.
    BADGER_FACE_SOURCE_WIDTH = 30,
    BADGER_FACE_SOURCE_HEIGHT = 31,
};

static inline void badger_decode_face(const patch_t *patch, const uint8_t *playpal,
                                      uint8_t source[][BADGER_FACE_SOURCE_WIDTH]) {
    // whd_gen packs every STF* face as runs of six-bit local palette indices.
    assert(vpatch_type(patch) == vp6_runs && !vpatch_has_shared_palette(patch));
    uint8_t luma[64];
    const uint8_t *palette = vpatch_palette(patch);
    for (unsigned i = 0; i < vpatch_colorcount(patch); ++i) {
        const uint8_t *rgb = playpal + 3 * palette[i];
        luma[i] = (77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2] + 128) >> 8;
    }

    const uint8_t *data = vpatch_data(patch);
    // Canvas origin is (1, 1) relative to Doom's face anchor.
    const int left = -vpatch_leftoffset(patch) - 1;
    const int top = -vpatch_topoffset(patch) - 1;
    for (int y = 0; y < vpatch_height(patch) && top + y < BADGER_FACE_SOURCE_HEIGHT; ++y) {
        for (int x = 0; x < vpatch_width(patch);) {
            unsigned gap = *data++;
            if (gap == 0xff) break;
            x += gap;
            unsigned count = *data++;
            unsigned bits = 0, available = 0;
            for (; count; --count, ++x) {
                if (available < 6) {
                    bits |= (unsigned)*data++ << available;
                    available += 8;
                }
                const int dx = left + x, dy = top + y;
                if ((unsigned)dx < BADGER_FACE_SOURCE_WIDTH && dy >= 0) {
                    source[dy][dx] = luma[bits & 63];
                }
                bits >>= 6;
                available -= 6;
            }
        }
    }
}

static inline unsigned badger_face_overlap(unsigned start, unsigned end,
                                           unsigned pos, unsigned unit) {
    const unsigned lo = pos * unit, hi = lo + unit;
    return (end < hi ? end : hi) - (start > lo ? start : lo);
}

static inline void badger_render_face(const patch_t *patch, const uint8_t *playpal,
                                      uint8_t rows[][BADGER_FACE_ROW_BYTES]) {
    uint8_t source[BADGER_FACE_SOURCE_HEIGHT][BADGER_FACE_SOURCE_WIDTH] = {0};
    uint8_t shade[BADGER_FACE_HEIGHT + 2][BADGER_FACE_WIDTH + 2] = {0};
    badger_decode_face(patch, playpal, source);

    // Area average in integer coordinates, including transparent (black)
    // source pixels. Don't let nearest-neighbor drop small eyes or teeth.
    for (unsigned y = 0; y < BADGER_FACE_HEIGHT; ++y) {
        const unsigned top = y * BADGER_FACE_SOURCE_HEIGHT;
        const unsigned bottom = top + BADGER_FACE_SOURCE_HEIGHT;
        for (unsigned x = 0; x < BADGER_FACE_WIDTH; ++x) {
            const unsigned left = x * BADGER_FACE_SOURCE_WIDTH;
            const unsigned right = left + BADGER_FACE_SOURCE_WIDTH;
            unsigned sum = 0;
            for (unsigned sy = top / BADGER_FACE_HEIGHT; sy * BADGER_FACE_HEIGHT < bottom; ++sy) {
                const unsigned wy = badger_face_overlap(top, bottom, sy, BADGER_FACE_HEIGHT);
                for (unsigned sx = left / BADGER_FACE_WIDTH; sx * BADGER_FACE_WIDTH < right; ++sx) {
                    const unsigned wx = badger_face_overlap(left, right, sx, BADGER_FACE_WIDTH);
                    sum += source[sy][sx] * wx * wy;
                }
            }
            shade[y + 1][x + 1] = sum / (BADGER_FACE_SOURCE_WIDTH * BADGER_FACE_SOURCE_HEIGHT);
        }
    }

    for (unsigned y = 0; y < BADGER_FACE_HEIGHT; ++y) {
        uint32_t bits = 0;
        for (unsigned x = 0; x < BADGER_FACE_WIDTH; ++x) {
            unsigned sum = 0;
            for (unsigned dy = 0; dy < 3; ++dy) {
                sum += shade[y + dy][x] + shade[y + dy][x + 1] + shade[y + dy][x + 2];
            }
            // Local contrast keeps dark brows and mouths legible without
            // dithering; the floor rejects dark background and weak edges.
            int threshold = (int)(sum / 9) - 18;
            if (threshold < 49) threshold = 49;
            if (threshold > 136) threshold = 136;
            bits = (bits << 1) | (shade[y + 1][x + 1] > threshold);
        }
        bits <<= (8 * BADGER_FACE_ROW_BYTES - BADGER_FACE_WIDTH) / 2;
        rows[y][0] = bits >> 16;
        rows[y][1] = bits >> 8;
        rows[y][2] = bits;
    }
}

// x is byte-aligned. Owns all 24×17 pixels of its black-backed rectangle.
static inline void badger_draw_face(uint8_t *dest, unsigned x, unsigned y) {
    static const uint8_t *playpal;
    static unsigned cached_handle = VPATCH_NAME_INVALID;
    static uint8_t rows[BADGER_FACE_HEIGHT][BADGER_FACE_ROW_BYTES];
    const unsigned handle = ST_BadgerFaceHandle();
    if (handle != cached_handle) {
        if (!playpal) playpal = W_CacheLumpName("PLAYPAL", PU_STATIC);
        badger_render_face(resolve_vpatch_handle(handle), playpal, rows);
        cached_handle = handle;
    }

    dest += y * (BADGER_WIDTH / 8) + x / 8;
    for (unsigned row = 0; row < BADGER_FACE_HEIGHT; ++row, dest += BADGER_WIDTH / 8) {
        memcpy(dest, rows[row], BADGER_FACE_ROW_BYTES);
    }
}

#endif
