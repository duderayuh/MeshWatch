// Mesh status/tools tile and the settings tile.

#include "ui_internal.h"
#include "../MyMesh.h"
#include "target.h"
#include <helpers/TxtDataHelpers.h>

// ---------------------------------------------------------------- mesh tile

static lv_obj_t* mesh_status = NULL;
static lv_obj_t* gps_lbl = NULL;
static lv_obj_t* radio_lbl = NULL;
static lv_obj_t* power_lbl = NULL;
static lv_obj_t* phone_lbl = NULL;

static void advert_cb(lv_event_t* e) {
  bool flood = (bool)(intptr_t) lv_event_get_user_data(e);
  bool ok = the_mesh.uiSendAdvert(flood);
  lv_label_set_text(mesh_status, ok ? (flood ? "Flood advert sent" : "Advert sent to nearby nodes") : "Advert failed");
  ui_task.buzz(true);
}

static void heard_open_cb(lv_event_t* e) {
  lv_obj_t* body;
  lv_obj_t* scr = nav_new_screen("Heard recently", &body);
  AdvertPath heard[16];
  int n = the_mesh.getRecentlyHeard(heard, 16);
  int shown = 0;
  for (int i = 0; i < n; i++) {
    if (heard[i].name[0] == 0 || heard[i].recv_timestamp == 0) continue;
    char sub[40], age[8], hops[12];
    fmt_age(age, sizeof(age), heard[i].recv_timestamp);
    fmt_hops(hops, sizeof(hops), heard[i].path_len);
    snprintf(sub, sizeof(sub), "%s " LV_SYMBOL_BULLET " %s", age, hops);
    ui_row(body, LV_SYMBOL_WIFI, heard[i].name, sub, 0, NULL, NULL);
    shown++;
  }
  if (shown == 0) ui_label(body, "Nothing heard yet.", &lv_font_montserrat_14, COL_MUTED);
  nav_push(scr);
}

static lv_obj_t* info_card(lv_obj_t* parent) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = ui_label(card, "", &lv_font_montserrat_14, COL_TEXT);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  return l;
}

void mesh_create(lv_obj_t* tile) {
  lv_obj_set_style_pad_all(tile, 6, 0);
  lv_obj_set_style_pad_row(tile, 6, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(tile, LV_DIR_VER);
  ui_title(tile, LV_SYMBOL_WIFI "  Mesh");

  ui_button(tile, LV_SYMBOL_UPLOAD "  Advert", advert_cb, (void*)(intptr_t)0);
  lv_obj_t* b = ui_button(tile, LV_SYMBOL_SHUFFLE "  Flood advert", advert_cb, (void*)(intptr_t)1);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD_HI), 0);
  mesh_status = ui_label(tile, "", &lv_font_montserrat_12, COL_MUTED);

  b = ui_button(tile, LV_SYMBOL_LIST "  Heard recently", heard_open_cb, NULL);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD_HI), 0);

  gps_lbl = info_card(tile);
  radio_lbl = info_card(tile);
  power_lbl = info_card(tile);
  phone_lbl = info_card(tile);
}

void mesh_refresh() {
  if (gps_lbl == NULL) return;
  char buf[160];

  LocationProvider* lp = ui_task.sensors() ? ui_task.sensors()->getLocationProvider() : NULL;
  SensorManager* s = ui_task.sensors();
  if (lp == NULL) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " GPS not available");
  } else if (!lp->isEnabled()) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " GPS off (Settings)");
  } else if (!lp->isValid()) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " Searching... %ld sats", lp->satellitesCount());
  } else {
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " Fix, %ld sats\n%.5f, %.5f\nAlt %.0f m", lp->satellitesCount(),
             s->node_lat, s->node_lon, s->node_altitude);
  }
  lv_label_set_text(gps_lbl, buf);

  NodePrefs* p = ui_task.nodePrefs();
  snprintf(buf, sizeof(buf), LV_SYMBOL_AUDIO " %.3f MHz\nSF%d  BW%.1f  CR%d  %d dBm", p->freq, p->sf, p->bw, p->cr,
           p->tx_power_dbm);
  lv_label_set_text(radio_lbl, buf);

  uint32_t up = millis() / 1000;
  int pct = board.getBattPercent();
  snprintf(buf, sizeof(buf), LV_SYMBOL_BATTERY_FULL " %d%%  %.2f V%s\nUptime %uh %02um", pct < 0 ? 0 : pct,
           board.getBattMilliVolts() / 1000.0f, board.isCharging() ? "  charging" : "",
           (unsigned)(up / 3600), (unsigned)((up / 60) % 60));
  lv_label_set_text(power_lbl, buf);

  // Always show the PIN: the phone counts as "connected" before pairing completes.
  if (!ui_task.isBluetoothEnabled()) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " Bluetooth off");
  } else {
    snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " %s\nPairing PIN %06u", ui_task.hasConnection() ? "Phone connected" : "Waiting for phone",
             (unsigned)the_mesh.getBLEPin());
  }
  lv_label_set_text(phone_lbl, buf);
}

// ---------------------------------------------------------------- settings tile

static lv_obj_t* name_row_lbl = NULL;

static lv_obj_t* setting_card(lv_obj_t* parent, const char* label, bool stacked) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_gap(card, 6, 0);
  lv_obj_set_flex_flow(card, stacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  ui_label(card, label, &lv_font_montserrat_14, COL_TEXT);
  return card;
}

static lv_obj_t* setting_switch(lv_obj_t* parent, const char* label, bool on, lv_event_cb_t cb) {
  lv_obj_t* card = setting_card(parent, label, false);
  lv_obj_t* sw = lv_switch_create(card);
  lv_obj_set_size(sw, 46, 24);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
  return sw;
}

static lv_obj_t* setting_dropdown(lv_obj_t* parent, const char* label, const char* options, int selected, lv_event_cb_t cb) {
  lv_obj_t* card = setting_card(parent, label, false);
  lv_obj_t* dd = lv_dropdown_create(card);
  lv_dropdown_set_options(dd, options);
  lv_dropdown_set_selected(dd, selected);
  lv_obj_set_width(dd, 110);
  lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
  lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
  return dd;
}

static bool sw_on(lv_event_t* e) { return lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED); }
static int dd_sel(lv_event_t* e) { return lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e)); }

static void brightness_cb(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*) lv_event_get_target(e);
  ui_task.prefs.brightness = lv_slider_get_value(sl);
  ui_task.applyBrightness();
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) ui_task.savePrefs();
}
static void timeout_cb(lv_event_t* e) { ui_task.prefs.timeout_idx = dd_sel(e); ui_task.savePrefs(); }
static void face_cb(lv_event_t* e) { ui_task.prefs.face = dd_sel(e); ui_task.savePrefs(); face_refresh(); }
static void rebuild_async(void* arg) { screens_rebuild(); }
static void accent_cb(lv_event_t* e) {
  ui_task.prefs.accent = dd_sel(e);
  ui_task.savePrefs();
  lv_async_call(rebuild_async, NULL);   // can't delete the screen from inside its own event
}
static void raise_cb(lv_event_t* e) { ui_task.prefs.raise_to_wake = sw_on(e); ui_task.savePrefs(); }
static void h24_cb(lv_event_t* e) { ui_task.prefs.use_24h = sw_on(e); ui_task.savePrefs(); }
static void tz_cb(lv_event_t* e) { ui_task.prefs.tz_minutes = dd_sel(e) * 30 - 12 * 60; ui_task.savePrefs(); }
static void vibrate_cb(lv_event_t* e) { ui_task.prefs.vibrate = sw_on(e); ui_task.savePrefs(); if (sw_on(e)) ui_task.buzz(); }
static void wake_cb(lv_event_t* e) { ui_task.prefs.wake_on_msg = sw_on(e); ui_task.savePrefs(); }
static void chan_alert_cb(lv_event_t* e) { ui_task.prefs.notify_channels = sw_on(e); ui_task.savePrefs(); }

static void ble_cb(lv_event_t* e) {
  if (sw_on(e)) ui_task.enableBluetooth();
  else ui_task.disableBluetooth();
}

static void gps_cb(lv_event_t* e) {
  bool on = sw_on(e);
  if (ui_task.sensors()) ui_task.sensors()->setSettingValue("gps", on ? "1" : "0");
  ui_task.nodePrefs()->gps_enabled = on;
  the_mesh.savePrefs();
}

static void share_loc_cb(lv_event_t* e) {
  ui_task.nodePrefs()->advert_loc_policy = sw_on(e) ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
  the_mesh.savePrefs();
}

static void name_done(const char* text, void* ctx) {
  StrHelper::strncpy(ui_task.nodePrefs()->node_name, text, sizeof(ui_task.nodePrefs()->node_name));
  the_mesh.savePrefs();
  if (name_row_lbl) lv_label_set_text(name_row_lbl, text);
}

static void name_cb(lv_event_t* e) {
  compose_open("Node name", ui_task.nodePrefs()->node_name, sizeof(ui_task.nodePrefs()->node_name) - 1, name_done, NULL);
}

static void confirm_power_cb(lv_event_t* e) {
  bool reboot = (bool)(intptr_t) lv_event_get_user_data(e);
  ui_task.store.saveNow();
  if (reboot) ESP.restart();
  else board.powerOff();
}

static void power_menu_cb(lv_event_t* e) {
  lv_obj_t* body;
  lv_obj_t* scr = nav_new_screen("Power", &body);
  ui_label(body, "Hold the crown for 6 s to force power off.\nPress the crown to turn back on.",
           &lv_font_montserrat_14, COL_MUTED);
  ui_button(body, LV_SYMBOL_REFRESH "  Restart", confirm_power_cb, (void*)(intptr_t)1);
  lv_obj_t* b = ui_button(body, LV_SYMBOL_POWER "  Power off", confirm_power_cb, (void*)(intptr_t)0);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_BAD), 0);
  nav_push(scr);
}

void settings_create(lv_obj_t* tile) {
  WatchPrefs& p = ui_task.prefs;
  NodePrefs* np = ui_task.nodePrefs();

  lv_obj_set_style_pad_all(tile, 6, 0);
  lv_obj_set_style_pad_row(tile, 6, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(tile, LV_DIR_VER);
  ui_title(tile, LV_SYMBOL_SETTINGS "  Settings");

  lv_obj_t* card = setting_card(tile, "Brightness", true);
  lv_obj_t* sl = lv_slider_create(card);
  lv_obj_set_width(sl, lv_pct(95));
  lv_slider_set_range(sl, 16, 255);
  lv_slider_set_value(sl, p.brightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(sl, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(sl, brightness_cb, LV_EVENT_RELEASED, NULL);

  setting_dropdown(tile, "Screen off", "10 s\n15 s\n30 s\n1 min\n5 min", p.timeout_idx, timeout_cb);
  setting_dropdown(tile, "Face", "Digital\nAnalog", p.face, face_cb);
  setting_dropdown(tile, "Accent", "Cyan\nGreen\nOrange\nRed\nPurple", p.accent, accent_cb);
  setting_switch(tile, "24-hour clock", p.use_24h, h24_cb);

  // 53 half-hour steps, each at most "\nUTC-12:00" (10 chars)
  static char tz_opts[53 * 12];
  int len = 0;
  for (int m = -12 * 60; m <= 14 * 60; m += 30) {
    int a = m < 0 ? -m : m;
    len += snprintf(tz_opts + len, sizeof(tz_opts) - len, "%sUTC%c%d:%02d", len ? "\n" : "",
                    m < 0 ? '-' : '+', a / 60, a % 60);
    if (len >= (int)sizeof(tz_opts)) break;
  }
  setting_dropdown(tile, "Time zone", tz_opts, (p.tz_minutes + 12 * 60) / 30, tz_cb);

  if (ui_task.hasMotion()) setting_switch(tile, "Raise to wake", p.raise_to_wake, raise_cb);
  setting_switch(tile, "Vibrate", p.vibrate, vibrate_cb);
  setting_switch(tile, "Wake on message", p.wake_on_msg, wake_cb);
  setting_switch(tile, "Channel alerts", p.notify_channels, chan_alert_cb);
  setting_switch(tile, "Bluetooth", ui_task.isBluetoothEnabled(), ble_cb);
  setting_switch(tile, "GPS", np->gps_enabled, gps_cb);
  setting_switch(tile, "Share location", np->advert_loc_policy != ADVERT_LOC_NONE, share_loc_cb);

  card = setting_card(tile, "Name", false);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, name_cb, LV_EVENT_CLICKED, NULL);
  name_row_lbl = ui_label(card, np->node_name, &lv_font_montserrat_14, COL_MUTED);
  lv_label_set_long_mode(name_row_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name_row_lbl, 130);
  lv_obj_set_style_text_align(name_row_lbl, LV_TEXT_ALIGN_RIGHT, 0);

  lv_obj_t* b = ui_button(tile, LV_SYMBOL_POWER "  Power", power_menu_cb, NULL);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD_HI), 0);

  char about[128];
  snprintf(about, sizeof(about), "MeshWatch " MESHWATCH_VERSION "\nMeshCore " FIRMWARE_VERSION "\nBLE PIN %06u",
           (unsigned)the_mesh.getBLEPin());
  ui_label(tile, about, &lv_font_montserrat_12, COL_MUTED);
}
