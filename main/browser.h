#pragma once

#include "button.h"
#include "sd_card.h"

// Shows entries as a scrolling list on the OLED and starts the browser task.
// The array must stay valid for as long as the browser is in use.
void browser_init(const sd_entry_t *entries, int count);

// Tap moves the selection down (wrapping at the end). Holding for
// BUTTON_LONG_PRESS_MS plays a WAV file or opens the details of anything
// else, and a tap stops playback or goes back to the list.
// Safe to call from any task; events are queued for the browser task.
void browser_handle_button(button_event_t event);

// Pass to player_init() so the browser returns to the list when a file ends
void browser_on_playback_done(void);
