#include "badger_input.h"

#include "hardware/gpio.h"
#include "pico/time.h"

enum { DEBOUNCE_US = 12000 };

static const uint8_t button_pins[] = {
    BADGER_BUTTON_UP_PIN,
    BADGER_BUTTON_DOWN_PIN,
    BADGER_BUTTON_A_PIN,
    BADGER_BUTTON_B_PIN,
    BADGER_BUTTON_C_PIN,
};

static badger_buttons_t candidate;
static badger_buttons_t debounced;
static uint32_t changed_at[count_of(button_pins)];

void badger_buttons_init(void)
{
    for (unsigned i = 0; i < count_of(button_pins); ++i) {
        gpio_init(button_pins[i]);
        gpio_pull_down(button_pins[i]);
    }
    candidate = badger_buttons_raw();
    debounced = 0;
    const uint32_t now = time_us_32();
    for (unsigned i = 0; i < count_of(button_pins); ++i) {
        changed_at[i] = now;
    }
}

badger_buttons_t badger_buttons_raw(void)
{
    const uint32_t pins = gpio_get_all();
    return (((pins >> BADGER_BUTTON_UP_PIN) & 1u) * BADGER_BUTTON_UP)
         | (((pins >> BADGER_BUTTON_DOWN_PIN) & 1u) * BADGER_BUTTON_DOWN)
         | (((pins >> BADGER_BUTTON_A_PIN) & 1u) * BADGER_BUTTON_A)
         | (((pins >> BADGER_BUTTON_B_PIN) & 1u) * BADGER_BUTTON_B)
         | (((pins >> BADGER_BUTTON_C_PIN) & 1u) * BADGER_BUTTON_C);
}

badger_buttons_t badger_buttons_poll(void)
{
    const badger_buttons_t raw = badger_buttons_raw();
    const badger_buttons_t changed = candidate ^ raw;
    const badger_buttons_t pending = debounced ^ raw;
    if (changed || pending) {
        const uint32_t now = time_us_32();
        for (unsigned i = 0; i < count_of(button_pins); ++i) {
            const badger_buttons_t bit = 1u << i;
            if (changed & bit) {
                changed_at[i] = now;
            } else if ((pending & bit) && now - changed_at[i] >= DEBOUNCE_US) {
                debounced ^= bit;
            }
        }
        candidate = raw;
    }
    return debounced;
}
