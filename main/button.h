#pragma once

#include "esp_err.h"

// Button wired between this GPIO and GND (uses the internal pull-up)
#define PIN_NUM_BUTTON 4

esp_err_t button_init(void);
