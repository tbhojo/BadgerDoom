//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2021-2022 Graham Sanderson
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     SDL implementation of system-specific input interface.
//


//#include "SDL.h"
//#include "SDL_keycode.h"
#include <doom/sounds.h>
#include <doom/s_sound.h>
#include "pico.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "d_event.h"
#include "i_input.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "hardware/uart.h"
#include <stdlib.h>
#if BADGER_DOOM
#include "badger_input.h"
#include "doom/doomstat.h"
#include "doom/m_menu.h"
#include "m_controls.h"
#include "pico/time.h"
#elif USB_SUPPORT
#include "pico/binary_info.h"
#include "tusb.h"
#include "hardware/irq.h"
bi_decl(bi_program_feature("USB keyboard support"));
#endif

static const int scancode_translate_table[] = SCANCODE_TO_KEYS_ARRAY;

// Lookup table for mapping ASCII characters to their equivalent when
// shift is pressed on a US layout keyboard. This is the original table
// as found in the Doom sources, comments and all.
static const char shiftxform[] =
        {
                0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                31, ' ', '!', '"', '#', '$', '%', '&',
                '"', // shift-'
                '(', ')', '*', '+',
                '<', // shift-,
                '_', // shift--
                '>', // shift-.
                '?', // shift-/
                ')', // shift-0
                '!', // shift-1
                '@', // shift-2
                '#', // shift-3
                '$', // shift-4
                '%', // shift-5
                '^', // shift-6
                '&', // shift-7
                '*', // shift-8
                '(', // shift-9
                ':',
                ':', // shift-;
                '<',
                '+', // shift-=
                '>', '?', '@',
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                '[', // shift-[
                '!', // shift-backslash - OH MY GOD DOES WATCOM SUCK
                ']', // shift-]
                '"', '_',
                '\'', // shift-`
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                '{', '|', '}', '~', 127
        };

// If true, I_StartTextInput() has been called, and we are populating
// the data3 field of ev_keydown events.
static boolean text_input_enabled = true;

// Bit mask of mouse button state.
static unsigned int mouse_button_state = 0;

// Disallow mouse and joystick movement to cause forward/backward
// motion.  Specified with the '-novert' command line parameter.
// This is an int to allow saving to config file
int novert = 0;

// If true, keyboard mapping is ignored, like in Vanilla Doom.
// The sensible thing to do is to disable this if you have a non-US
// keyboard.

#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
int vanilla_keyboard_mapping = true;
#endif

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.
#if !NO_USE_MOUSE
float mouse_acceleration = 2.0;
int mouse_threshold = 10;
#endif

enum {
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_LALT = 226, /**< alt, option */
    SDL_SCANCODE_LGUI = 227, /**< windows, command (apple), meta */
    SDL_SCANCODE_RCTRL = 228,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_RALT = 230, /**< alt gr, option */
    SDL_SCANCODE_RGUI = 231, /**< windows, command (apple), meta */
};

// Translates the SDL key to a value of the type found in doomkeys.h
int TranslateKey(int scancode)
{
    switch (scancode)
    {
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            return KEY_RCTRL;

        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
            return KEY_RSHIFT;

        case SDL_SCANCODE_LALT:
            return KEY_LALT;

        case SDL_SCANCODE_RALT:
            return KEY_RALT;

        default:
            if (scancode >= 0 && scancode < arrlen(scancode_translate_table))
            {
                return scancode_translate_table[scancode];
            }
            else
            {
                return 0;
            }
    }
}

// Get the localized version of the key press. This takes into account the
// keyboard layout, but does not apply any changes due to modifiers, (eg.
// shift-, alt-, etc.)
static int GetLocalizedKey(int scancode)
{
    // When using Vanilla mapping, we just base everything off the scancode
    // and always pretend the user is using a US layout keyboard.
    if (vanilla_keyboard_mapping)
    {
        return TranslateKey(scancode);
    }
    else
    {
        assert(false); return 0;
//        int result = sym->sym;
//
//        if (result < 0 || result >= 128)
//        {
//            result = 0;
//        }
//
//        return sym_<result;
    }
}

// Get the equivalent ASCII (Unicode?) character for a keypress.
int GetTypedChar(int scancode, boolean shiftdown)
{
    // We only return typed characters when entering text, after
    // I_StartTextInput() has been called. Otherwise we return nothing.
    if (!text_input_enabled)
    {
        return 0;
    }

    // If we're strictly emulating Vanilla, we should always act like
    // we're using a US layout keyboard (in ev_keydown, data1=data2).
    // Otherwise we should use the native key mapping.
    if (vanilla_keyboard_mapping)
    {
        int result = TranslateKey(scancode);

        // If shift is held down, apply the original uppercase
        // translation table used under DOS.
        if (shiftdown
            && result >= 0 && result < arrlen(shiftxform))
        {
            result = shiftxform[result];
        }

        return result;
    }
    else
    {
#if 0
        SDL_Event next_event;

        // Special cases, where we always return a fixed value.
        switch (sym->sym)
        {
            case SDLK_BACKSPACE: return KEY_BACKSPACE;
            case SDLK_RETURN:    return KEY_ENTER;
            default:
                break;
        }

        // The following is a gross hack, but I don't see an easier way
        // of doing this within the SDL2 API (in SDL1 it was easier).
        // We want to get the fully transformed input character associated
        // with this keypress - correct keyboard layout, appropriately
        // transformed by any modifier keys, etc. So peek ahead in the SDL
        // event queue and see if the key press is immediately followed by
        // an SDL_TEXTINPUT event. If it is, it's reasonable to assume the
        // key press and the text input are connected. Technically the SDL
        // API does not guarantee anything of the sort, but in practice this
        // is what happens and I've verified it through manual inspect of
        // the SDL source code.
        //
        // In an ideal world we'd split out ev_keydown into a separate
        // ev_textinput event, as SDL2 has done. But this doesn't work
        // (I experimented with the idea), because lots of Doom's code is
        // based around different responders "eating" events to stop them
        // being passed on to another responder. If code is listening for
        // a text input, it cannot block the corresponding keydown events
        // which can affect other responders.
        //
        // So we're stuck with this as a rather fragile alternative.

        if (SDL_PeepEvents(&next_event, 1, SDL_PEEKEVENT,
                           SDL_FIRSTEVENT, SDL_LASTEVENT) == 1
            && next_event.type == SDL_TEXTINPUT)
        {
            // If an SDL_TEXTINPUT event is found, we always assume it
            // matches the key press. The input text must be a single
            // ASCII character - if it isn't, it's possible the input
            // char is a Unicode value instead; better to send a null
            // character than the unshifted key.
            if (strlen(next_event.text.text) == 1
                && (next_event.text.text[0] & 0x80) == 0)
            {
                return next_event.text.text[0];
            }
        }
#else
        assert(false);
#endif

        // Failed to find anything :/
        return 0;
    }
}

void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    text_input_enabled = true;

    if (!vanilla_keyboard_mapping)
    {
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
        // SDL2-TODO: SDL_SetTextInputRect(...);
        SDL_StartTextInput();
#endif
    }
}

void I_StopTextInput(void)
{
    text_input_enabled = false;

    if (!vanilla_keyboard_mapping)
    {
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
        SDL_StopTextInput();
#endif
    }
}

#if !NO_USE_MOUSE
static void UpdateMouseButtonState(unsigned int button, boolean on)
{
    static event_t event;

    if (button < SDL_BUTTON_LEFT || button > MAX_MOUSE_BUTTONS)
    {
        return;
    }

    // Note: button "0" is left, button "1" is right,
    // button "2" is middle for Doom.  This is different
    // to how SDL sees things.

    switch (button)
    {
        case SDL_BUTTON_LEFT:
            button = 0;
            break;

        case SDL_BUTTON_RIGHT:
            button = 1;
            break;

        case SDL_BUTTON_MIDDLE:
            button = 2;
            break;

        default:
            // SDL buttons are indexed from 1.
            --button;
            break;
    }

    // Turn bit representing this button on or off.

    if (on)
    {
        mouse_button_state |= (1 << button);
    }
    else
    {
        mouse_button_state &= ~(1 << button);
    }

    // Post an event with the new button state.

    event.type = ev_mouse;
    event.data1 = mouse_button_state;
    event.data2 = event.data3 = 0;
    D_PostEvent(&event);
}

static void MapMouseWheelToButtons(SDL_MouseWheelEvent *wheel)
{
    // SDL2 distinguishes button events from mouse wheel events.
    // We want to treat the mouse wheel as two buttons, as per
    // SDL1
    static event_t up, down;
    int button;

    if (wheel->y <= 0)
    {   // scroll down
        button = 4;
    }
    else
    {   // scroll up
        button = 3;
    }

    // post a button down event
    mouse_button_state |= (1 << button);
    down.type = ev_mouse;
    down.data1 = mouse_button_state;
    down.data2 = down.data3 = 0;
    D_PostEvent(&down);

    // post a button up event
    mouse_button_state &= ~(1 << button);
    up.type = ev_mouse;
    up.data1 = mouse_button_state;
    up.data2 = up.data3 = 0;
    D_PostEvent(&up);
}

void I_HandleMouseEvent(SDL_Event *sdlevent)
{
    switch (sdlevent->type)
    {
        case SDL_MOUSEBUTTONDOWN:
            UpdateMouseButtonState(sdlevent->button.button, true);
            break;

        case SDL_MOUSEBUTTONUP:
            UpdateMouseButtonState(sdlevent->button.button, false);
            break;

        case SDL_MOUSEWHEEL:
            MapMouseWheelToButtons(&(sdlevent->wheel));
            break;

        default:
            break;
    }
}

static int AccelerateMouse(int val)
{
    if (val < 0)
        return -AccelerateMouse(-val);

    if (val > mouse_threshold)
    {
        return (int)((val - mouse_threshold) * mouse_acceleration + mouse_threshold);
    }
    else
    {
        return val;
    }
}

//
// Read the change in mouse state to generate mouse motion events
//
// This is to combine all mouse movement for a tic into one mouse
// motion event.
void I_ReadMouse(void)
{
    int x, y;
    event_t ev;

    SDL_GetRelativeMouseState(&x, &y);

    if (x != 0 || y != 0)
    {
        ev.type = ev_mouse;
        ev.data1 = mouse_button_state;
        ev.data2 = AccelerateMouse(x);

        if (!novert)
        {
            ev.data3 = -AccelerateMouse(y);
        }
        else
        {
            ev.data3 = 0;
        }

        // XXX: undefined behaviour since event is scoped to
        // this function
        D_PostEvent(&ev);
    }
}
#endif

// Bind all variables controlling input options.
void I_BindInputVariables(void)
{
#if !NO_USE_MOUSE
    M_BindFloatVariable("mouse_acceleration",      &mouse_acceleration);
    M_BindIntVariable("mouse_threshold",           &mouse_threshold);
#endif
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
    M_BindIntVariable("vanilla_keyboard_mapping",  &vanilla_keyboard_mapping);
#endif
    M_BindIntVariable("novert",                    &novert);
}

#if PICO_NO_HARDWARE
#include "pico/scanvideo.h"
#else
#define WITH_SHIFT 0x8000
#endif

#if !BADGER_DOOM
static void pico_key_down(int scancode, int keysym, int modifiers) {
    event_t event;
    event.type = ev_keydown;
    event.data1 = TranslateKey(scancode);
    event.data2 = GetLocalizedKey(scancode);
    event.data3 = GetTypedChar(scancode, modifiers & WITH_SHIFT ? 1 : 0);

    if (at_exit_screen) {
        handle_exit_key_down(scancode, modifiers & WITH_SHIFT ? 1 : 0, exit_screen_kb_buffer_80, 80);
        return;
    }
    if (event.data1 != 0)
    {
        D_PostEvent(&event);
    }
}

static void pico_key_up(int scancode, int keysym, int modifiers) {
    event_t event;
    event.type = ev_keyup;
    event.data1 = TranslateKey(scancode);
    // data2/data3 are initialized to zero for ev_keyup.
    // For ev_keydown it's the shifted Unicode character
    // that was typed, but if something wants to detect
    // key releases it should do so based on data1
    // (key ID), not the printable char.
    event.data2 = 0;
    event.data3 = 0;
    if (event.data1 != 0)
    {
        D_PostEvent(&event);
    }
}
#endif

#if PICO_NO_HARDWARE
static void pico_quit(void) {
    exit(0);
}
#endif

#if BADGER_DOOM
// Universe Badger Doom controls:
//   Play: UP/DOWN move, A/C turn, B fires, UP+DOWN uses/opens.
//   Hold A+C, then press B to cycle weapons, UP to toggle the automap,
//   or DOWN to pause. Release both without choosing a shortcut to open the menu.
//   Automap: A+C then B toggles follow. UP/DOWN/A/C pan with follow off;
//   B zooms in and UP+DOWN zooms out.
//   Menus: UP/DOWN select, A/C adjust, B enters, UP+DOWN backs out one level,
//   A+C closes the menu. On prompts B means yes, either chord means no.
//   Save name: keep the existing name or default to the current map name;
//   B accepts, either chord cancels. Load uses normal slot navigation.
//   Chord members are suppressed until both have been released.

extern int messageToPrint;
#if !NO_USE_SAVE
extern isb_int8_t stringEntry;
extern isb_int8_t stringEntryIndex;
#endif

typedef enum {
    BADGER_PLAY,
    BADGER_MAP,
    BADGER_MENU,
    BADGER_PROMPT,
    BADGER_NAME,
} badger_mode_t;

typedef struct {
    uint32_t since;
    badger_buttons_t pending;
    bool sent;
    bool latched;
    bool held;
} badger_chord_t;

enum {
    BADGER_LR_CHORD = BADGER_BUTTON_A | BADGER_BUTTON_C,
    BADGER_UD_CHORD = BADGER_BUTTON_UP | BADGER_BUTTON_DOWN,
    BADGER_CHORD_GRACE_US = 30000,
    BADGER_MENU_KEYS = 4,
};

static void BadgerPostKey(int key, bool down)
{
    if (key != 0) {
        const int ch = down && key >= 32 && key < 127 ? key : 0;
        event_t ev = {
            .type = down ? ev_keydown : ev_keyup,
            .data1 = key,
            .data2 = ch,
            .data3 = text_input_enabled ? ch : 0,
        };
        D_PostEvent(&ev);
    }
}

static void BadgerTapKey(int key)
{
    BadgerPostKey(key, true);
    BadgerPostKey(key, false);
}

static badger_mode_t BadgerMode(void)
{
#if !NO_USE_SAVE
    if (stringEntry)
        return BADGER_NAME;
#endif
    if (messageToPrint)
        return BADGER_PROMPT;
    if (menuactive)
        return BADGER_MENU;
    return automapactive ? BADGER_MAP : BADGER_PLAY;
}

// Give the other member time to settle before emitting a single direction;
// preserve a short solo tap if it ends within this window.
static badger_buttons_t BadgerChord(badger_chord_t *chord, badger_buttons_t buttons,
                                    badger_buttons_t mask, uint32_t now)
{
    buttons &= mask;
    if (chord->latched) {
        if (!buttons)
            *chord = (badger_chord_t) {0};
        else if (buttons != mask)
            chord->held = false;
        return 0;
    }
    if (buttons == mask) {
        chord->latched = chord->held = true;
        return 0;
    }
    if (buttons != chord->pending) {
        const badger_buttons_t tap = chord->sent ? 0 : chord->pending;
        *chord = (badger_chord_t) {.since = now, .pending = buttons};
        return tap;
    }
    if (buttons && now - chord->since >= BADGER_CHORD_GRACE_US) {
        chord->sent = true;
        return buttons;
    }
    return 0;
}

// Each gameplay poll can produce at most three held keys (one per axis plus
// fire). Releases come first; a reassigned key must not be left down.
static void BadgerHoldKeys(int vertical, int horizontal, int action)
{
    static int held[3];
    const int keys[] = {vertical, horizontal, action};
    for (unsigned i = 0; i < count_of(keys); ++i) {
        if (held[i] != keys[i])
            BadgerPostKey(held[i], false);
    }
    for (unsigned i = 0; i < count_of(keys); ++i) {
        if (held[i] != keys[i])
            BadgerPostKey(keys[i], true);
        held[i] = keys[i];
    }
}

static void BadgerPollDoom(void)
{
    static badger_mode_t previous_mode;
    static gamestate_t previous_gamestate;
    static const void *previous_page;
    static int menu_keys[BADGER_MENU_KEYS];
    static unsigned menu_head, menu_count;
    static badger_buttons_t blocked, previous_buttons, previous_single;
    static badger_chord_t lr, ud;
    static bool command_used;

    if (at_exit_screen)
        return;

    const badger_buttons_t physical = badger_buttons_poll();
    const badger_mode_t mode = BadgerMode();
    const void *page = mode == BADGER_MENU ? M_BadgerMenuPage() : NULL;
    const bool mode_changed = mode != previous_mode || gamestate != previous_gamestate;
    if (mode_changed || page != previous_page)
        menu_count = 0;
    previous_page = page;
    blocked &= physical;
    if (mode_changed) {
        BadgerHoldKeys(0, 0, 0);
        blocked = physical;
        previous_buttons = previous_single = 0;
        lr = ud = (badger_chord_t) {0};
        command_used = false;
#if !NO_USE_SAVE
        if (mode == BADGER_NAME && previous_mode != BADGER_NAME && !stringEntryIndex) {
            const char episode_name[] = {'E', '0' + gameepisode, 'M', '0' + gamemap, 0};
            const char map_name[] = {'M', 'A', 'P', '0' + gamemap / 10, '0' + gamemap % 10, 0};
            const char *name = gamemode == commercial ? map_name : episode_name;
            // These are text input, consumed by the save editor, not held
            // game keys. One event per character fits the small event queue.
            while (*name)
                BadgerPostKey(*name++, true);
        }
#endif
        previous_mode = mode;
        previous_gamestate = gamestate;
        return;
    }

    const uint32_t now = time_us_32();
    const badger_buttons_t buttons = physical & ~blocked;
    const badger_buttons_t pressed = buttons & ~previous_buttons;
    previous_buttons = buttons;
    const bool was_lr = lr.latched, was_ud = ud.latched;
    const badger_buttons_t horizontal = BadgerChord(&lr, buttons, BADGER_LR_CHORD, now);
    const badger_buttons_t vertical = BadgerChord(&ud, buttons, BADGER_UD_CHORD, now);
    const badger_buttons_t single = horizontal | vertical | (buttons & BADGER_BUTTON_B);
    const badger_buttons_t tapped = single & ~previous_single;
    previous_single = single;
    const bool lr_started = lr.latched && !was_lr;
    const bool ud_started = ud.latched && !was_ud;

    if (mode == BADGER_PLAY || mode == BADGER_MAP) {
        if (lr.latched || was_lr) {
            BadgerHoldKeys(0, 0, 0);
            if (lr_started)
                command_used = false;
            if (!command_used && lr.held && !lr_started && pressed) {
                int command = 0;
                if (pressed & BADGER_BUTTON_B) {
                    if (mode == BADGER_MAP) {
                        command = key_map_follow;
                    } else {
                        if (!key_nextweapon)
                            key_nextweapon = KEY_HOME;
                        command = key_nextweapon;
                    }
                } else if (pressed & BADGER_BUTTON_UP) {
                    command = key_map_toggle;
                } else if (pressed & BADGER_BUTTON_DOWN) {
                    command = key_pause;
                }
                BadgerTapKey(command);
                command_used = command != 0;
            }
            if (!lr.latched) {
                if (!command_used) {
                    (void)I_BadgerTakeMenuInput();
                    BadgerTapKey(key_menu_activate);
                }
                blocked |= physical;
                ud = (badger_chord_t) {0};
            }
            return;
        }

        const bool map = mode == BADGER_MAP;
        BadgerHoldKeys(
            ud.held ? (map ? key_map_zoomout : key_use)
                : vertical & BADGER_BUTTON_UP ? (map ? key_map_north : key_up)
                : vertical & BADGER_BUTTON_DOWN ? (map ? key_map_south : key_down) : 0,
            horizontal & BADGER_BUTTON_A ? (map ? key_map_west : key_left)
                : horizontal & BADGER_BUTTON_C ? (map ? key_map_east : key_right) : 0,
            buttons & BADGER_BUTTON_B ? (map ? key_map_zoomin : key_fire) : 0);
        return;
    }

    // Back/Close cancels queued navigation and need not wait for the panel.
    if (lr_started || ud_started) {
        menu_count = 0;
        (void)I_BadgerTakeMenuInput();
        BadgerTapKey(mode == BADGER_NAME ? KEY_ESCAPE
                   : mode == BADGER_PROMPT ? key_menu_abort
                   : lr_started ? key_menu_activate : key_menu_back);
        return;
    }

    // Keep taps in order through panel BUSY. A confirm follows, rather than
    // overtakes, any earlier cursor steps. Back and page changes discard them.
    const badger_buttons_t step = mode == BADGER_MENU ? tapped & BADGER_UD_CHORD : 0;
    const int key = step & BADGER_BUTTON_UP ? key_menu_up
                  : step & BADGER_BUTTON_DOWN ? key_menu_down
                  : mode == BADGER_MENU && ud.pending && !ud.sent ? 0
                  : tapped & BADGER_BUTTON_B
                    ? (mode == BADGER_NAME ? KEY_ENTER
                       : mode == BADGER_PROMPT ? key_menu_confirm : key_menu_forward)
                  : mode == BADGER_MENU && (tapped & BADGER_BUTTON_A) ? key_menu_left
                  : mode == BADGER_MENU && (tapped & BADGER_BUTTON_C) ? key_menu_right : 0;
    if (key && menu_count < BADGER_MENU_KEYS) {
        menu_keys[(menu_head + menu_count) % BADGER_MENU_KEYS] = key;
        ++menu_count;
    }
    if (menu_count && I_BadgerTakeMenuInput()) {
        BadgerTapKey(menu_keys[menu_head]);
        menu_head = (menu_head + 1) % BADGER_MENU_KEYS;
        --menu_count;
    }
}
#endif

void I_InputInit(void) {
#if BADGER_DOOM
    badger_buttons_init();
#elif PICO_NO_HARDWARE
    platform_key_down = pico_key_down;
    platform_key_up = pico_key_up;
    platform_quit = pico_quit;
#elif USB_SUPPORT
    tusb_init();
    irq_set_priority(USBCTRL_IRQ, 0xc0);
#endif
}

void I_GetEvent() {
#if USB_SUPPORT && !BADGER_DOOM
    tuh_task();
#endif
    return I_GetEventTimeout(50);
}

void I_GetEventTimeout(int key_timeout) {
#if BADGER_DOOM
    BadgerPollDoom();
#elif PICO_ON_DEVICE && !NO_USE_UART
    if (uart_is_readable(uart_default)) {
        char c = uart_getc(uart_default);
        if (c == 26 && uart_is_readable_within_us(uart_default, key_timeout)) {
            c = uart_getc(uart_default);
            static int modifiers = 0;
            switch (c) {
                case 0:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint scancode = (uint8_t) uart_getc(uart_default);
                        if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT) {
                            modifiers |= WITH_SHIFT;
                        }
                        pico_key_down(scancode, 0, modifiers);
                    }
                    return;
                case 1:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint scancode = (uint8_t) uart_getc(uart_default);
                        if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT) {
                            modifiers &= ~WITH_SHIFT;
                        }
                        pico_key_up(scancode, 0, modifiers);
                    }
                    return;
                case 2:
                case 3:
                case 5:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    return;
                case 4:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    return;
            }
        }
    }
#endif
}

#if USB_SUPPORT && !BADGER_DOOM

#define MAX_REPORT  4
#define debug_printf(fmt,...) ((void)0)

// Each HID instance can has multiple reports
static struct
{
    uint8_t report_count;
    tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const * report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
    debug_printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);

    // Interface protocol (hid_interface_protocol_enum_t)
    const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    debug_printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);
//    printf("%d USB: device %d connected, protocol %s\n", time_us_32() - t0 , dev_addr, protocol_str[itf_protocol]);

    // By default host stack will use activate boot protocol on supported interface.
    // Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
    if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
    {
        hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
        debug_printf("HID has %u reports \r\n", hid_info[instance].report_count);
    }

    // request to receive report
    // tuh_hid_report_received_cb() will be invoked when report is available
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
        debug_printf("Error: cannot request to receive report\r\n");
    }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    debug_printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
    printf("USB: device %d disconnected\n", dev_addr);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    switch (itf_protocol)
    {
        case HID_ITF_PROTOCOL_KEYBOARD:
            TU_LOG2("HID receive boot keyboard report\r\n");
            process_kbd_report( (hid_keyboard_report_t const*) report );
            break;

#if !NO_USE_MOUSE
            case HID_ITF_PROTOCOL_MOUSE:
      TU_LOG2("HID receive boot mouse report\r\n");
      process_mouse_report( (hid_mouse_report_t const*) report );
    break;
#endif

        default:
            // Generic report requires matching ReportID and contents with previous parsed report info
            process_generic_report(dev_addr, instance, report, len);
            break;
    }

    // continue to request to receive report
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
        debug_printf("Error: cannot request to receive report\r\n");
    }
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
    for(uint8_t i=0; i<6; i++)
    {
        if (report->keycode[i] == keycode)  return true;
    }

    return false;
}

static void check_mod(int mod, int prev_mod, int mask, int scancode) {
    if ((mod^prev_mod)&mask) {
        if (mod & mask)
            pico_key_down(scancode, 0, 0);
        else
            pico_key_up(scancode, 0, 0);
    }
}

static void process_kbd_report(hid_keyboard_report_t const *report)
{
    static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released

    //------------- example code ignore control (non-printable) key affects -------------//
    for(uint8_t i=0; i<6; i++)
    {
        if ( report->keycode[i] )
        {
            if ( find_key_in_report(&prev_report, report->keycode[i]) )
            {
                // exist in previous report means the current key is holding
            }else
            {
                // not existed in previous report means the current key is pressed
                bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
                pico_key_down(report->keycode[i], 0, is_shift ? WITH_SHIFT : 0);
            }
        }
        // Check for key depresses (i.e. was present in prev report but not here)
        if (prev_report.keycode[i]) {
            // If not present in the current report then depressed
            if (!find_key_in_report(report, prev_report.keycode[i]))
            {
                bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
                pico_key_up(prev_report.keycode[i], 0, is_shift ? WITH_SHIFT : 0);
            }
        }
    }
    // synthesize events for modifier keys
    static const uint8_t mods[] = {
            KEYBOARD_MODIFIER_LEFTCTRL, SDL_SCANCODE_LCTRL,
            KEYBOARD_MODIFIER_RIGHTCTRL, SDL_SCANCODE_RCTRL,
            KEYBOARD_MODIFIER_LEFTALT, SDL_SCANCODE_LALT,
            KEYBOARD_MODIFIER_RIGHTALT, SDL_SCANCODE_RALT,
            KEYBOARD_MODIFIER_LEFTSHIFT, SDL_SCANCODE_LSHIFT,
            KEYBOARD_MODIFIER_RIGHTSHIFT, SDL_SCANCODE_RSHIFT,
    };
    for(int i=0;i<count_of(mods); i+= 2) {
        check_mod(report->modifier, prev_report.modifier, mods[i], mods[i+1]);
    }
    prev_report = *report;
}

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

#if !NO_USE_MOUSE
static void process_mouse_report(hid_mouse_report_t const * report)
{
    static hid_mouse_report_t prev_report = { 0 };

    uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
    if ( button_changed_mask & report->buttons)
    {
        debug_printf(" %c%c%c ",
                     report->buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-',
                     report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-',
                     report->buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-');
    }

//    cursor_movement(report->x, report->y, report->wheel);
}
#endif

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    (void) dev_addr;

    uint8_t const rpt_count = hid_info[instance].report_count;
    tuh_hid_report_info_t* rpt_info_arr = hid_info[instance].report_info;
    tuh_hid_report_info_t* rpt_info = NULL;

    if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
    {
        // Simple report without report ID as 1st byte
        rpt_info = &rpt_info_arr[0];
    }else
    {
        // Composite report, 1st byte is report ID, data starts from 2nd byte
        uint8_t const rpt_id = report[0];

        // Find report id in the arrray
        for(uint8_t i=0; i<rpt_count; i++)
        {
            if (rpt_id == rpt_info_arr[i].report_id )
            {
                rpt_info = &rpt_info_arr[i];
                break;
            }
        }

        report++;
        len--;
    }

    if (!rpt_info)
    {
        debug_printf("Couldn't find the report info for this report !\r\n");
        return;
    }

    // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
    // - Keyboard                     : Desktop, Keyboard
    // - Mouse                        : Desktop, Mouse
    // - Gamepad                      : Desktop, Gamepad
    // - Consumer Control (Media Key) : Consumer, Consumer Control
    // - System Control (Power key)   : Desktop, System Control
    // - Generic (vendor)             : 0xFFxx, xx
    if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
    {
        switch (rpt_info->usage)
        {
            case HID_USAGE_DESKTOP_KEYBOARD:
                TU_LOG1("HID receive keyboard report\r\n");
                // Assume keyboard follow boot report layout
                process_kbd_report( (hid_keyboard_report_t const*) report );
                break;

#if !NO_USE_MOUSE
                case HID_USAGE_DESKTOP_MOUSE:
        TU_LOG1("HID receive mouse report\r\n");
        // Assume mouse follow boot report layout
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;
#endif

            default: break;
        }
    }
}

#endif