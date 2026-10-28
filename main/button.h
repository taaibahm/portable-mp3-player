#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Called from the button task (not the ISR) after debouncing
typedef void (*button_callback_t)(int gpio, bool pressed);

// Button wired between gpio and GND (uses the internal pull-up).
// Can be called once per button; cb may be NULL to only log presses.
esp_err_t button_add(int gpio, button_callback_t cb);
