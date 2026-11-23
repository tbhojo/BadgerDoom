#ifndef BADGER_INPUT_H
#define BADGER_INPUT_H

#include <stdint.h>

typedef uint8_t badger_buttons_t;

enum {
    BADGER_BUTTON_UP   = 1u << 0,
    BADGER_BUTTON_DOWN = 1u << 1,
    BADGER_BUTTON_A    = 1u << 2,
    BADGER_BUTTON_B    = 1u << 3,
    BADGER_BUTTON_C    = 1u << 4,
};

// Single caller/core. No Doom or USB dependency; also usable in a diagnostic.
void badger_buttons_init(void);
badger_buttons_t badger_buttons_raw(void);
// Returns the current pressed mask after debouncing each switch independently.
badger_buttons_t badger_buttons_poll(void);

#endif
