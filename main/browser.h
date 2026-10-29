#pragma once

#include "button.h"
#include "sd_card.h"

// Shows entries as a scrolling list on the OLED. The array must stay valid
// for as long as the browser is in use.
void browser_init(const sd_entry_t *entries, int count);

// Tap moves the selection down (wrapping at the end), holding for
// BUTTON_LONG_PRESS_MS opens the selected item, and a tap goes back.
void browser_handle_button(button_event_t event);
