#pragma once

#include "esp_err.h"

// How long a button must be held before BUTTON_EVENT_LONG_PRESS fires
#define BUTTON_LONG_PRESS_MS 3000

typedef enum {
    BUTTON_EVENT_PRESSED,     // Pin went low (after debouncing)
    BUTTON_EVENT_RELEASED,    // Pin went high again
    BUTTON_EVENT_SHORT_PRESS, // Released before BUTTON_LONG_PRESS_MS
    BUTTON_EVENT_LONG_PRESS,  // Still held at BUTTON_LONG_PRESS_MS (fires while held)
} button_event_t;

// Called from the button task (not the ISR)
typedef void (*button_callback_t)(int gpio, button_event_t event);

// Button wired between gpio and GND (uses the internal pull-up).
// Can be called once per button; cb may be NULL to only log presses.
esp_err_t button_add(int gpio, button_callback_t cb);
