#pragma once

#include <lvgl.h>
#include "MsgStore.h"

// Screen layer of the MeshWatch UI. UITask owns power/input/mesh events and
// calls into these; the screens call back into ui_task for actions.

void screens_create();
void screens_rebuild();                       // after theme/accent changes
void screens_refresh_status();                // ~1 Hz while the screen is on
void screens_on_screen_off();
void screens_go_home();
bool screens_keep_awake();
bool screens_is_viewing(const ConvKey& k);
void screens_on_message(const ConvKey& k);
void screens_on_contacts_changed();
void screens_show_notification(const char* title, const char* text, const ConvKey& k);
