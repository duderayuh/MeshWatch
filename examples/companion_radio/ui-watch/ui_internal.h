#pragma once

// Helpers shared by the ui_*.cpp screen files.

#include <lvgl.h>
#include "UITask.h"
#include "ui_screens.h"

#define SCR_W 240
#define SCR_H 240

#define COL_BG        0x000000
#define COL_CARD      0x1C1C1E
#define COL_CARD_HI   0x2C2C2E
#define COL_TEXT      0xFFFFFF
#define COL_MUTED     0x8E8E93
#define COL_OK        0x30D158
#define COL_WARN      0xFFD60A
#define COL_BAD       0xFF453A

enum TileIndex { TILE_FACE = 0, TILE_CHATS, TILE_CONTACTS, TILE_MESH, TILE_SETTINGS, NUM_TILES };

lv_color_t accent();

// Navigation: the tileview "home" screen plus a stack of detail screens.
lv_obj_t* nav_new_screen(const char* title, lv_obj_t** body_out);   // header with back button + scrollable body
void nav_push(lv_obj_t* scr);
void nav_pop();
lv_obj_t* nav_home_tile(int idx);
void nav_show_tile(int idx);

// Widgets
lv_obj_t* ui_title(lv_obj_t* parent, const char* text);
lv_obj_t* ui_row(lv_obj_t* parent, const char* icon, const char* title, const char* subtitle, int badge,
                 lv_event_cb_t cb, void* user_data);
lv_obj_t* ui_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user_data);
lv_obj_t* ui_label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color);
lv_obj_t* ui_column(lv_obj_t* parent);   // transparent full-width flex column

// Formatting
void fmt_clock(char* buf, size_t sz, uint32_t epoch);          // "14:05" / "2:05p"
void fmt_age(char* buf, size_t sz, uint32_t epoch);            // "now", "5m", "3h", "2d"
void fmt_hops(char* buf, size_t sz, uint8_t path_len);         // "direct", "2 hops", "flood"
bool contact_distance(int32_t lat_e6, int32_t lon_e6, double* km_out, double* bearing_out);
const char* contact_type_icon(uint8_t type);
const char* conv_title(const ConvKey& k, char* buf, size_t sz);

// Screens implemented across files
void face_create(lv_obj_t* tile);
void face_refresh();
void chats_create(lv_obj_t* tile);
void chats_refresh();
void contacts_create(lv_obj_t* tile);
void contacts_refresh();
void mesh_create(lv_obj_t* tile);
void mesh_refresh();
void settings_create(lv_obj_t* tile);
void conversation_open(const ConvKey& k);
void conversation_refresh();
bool conversation_is_open(const ConvKey& k);
void compose_open(const char* title, const char* initial, int max_len, void (*on_done)(const char* text, void* ctx), void* ctx);
void contact_detail_open(const uint8_t* key_prefix);
