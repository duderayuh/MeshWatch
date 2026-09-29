// Navigation, shared widgets, formatting and the screens_* entry points.

#include "ui_internal.h"
#include "../MyMesh.h"
#include <helpers/AdvertDataHelpers.h>
#include <math.h>

#define NAV_MAX_DEPTH 6
#define NAV_ANIM_MS   180
#define NOTIFY_MS     6000

static lv_obj_t* home_scr = NULL;
static lv_obj_t* tileview = NULL;
static lv_obj_t* tiles[NUM_TILES];
static lv_obj_t* nav_stack[NAV_MAX_DEPTH];
static int nav_depth = 0;

static lv_obj_t* notify_panel = NULL;
static lv_timer_t* notify_timer = NULL;
static ConvKey notify_conv;

bool compose_is_open();

lv_color_t accent() { return lv_color_hex(ACCENT_COLORS[ui_task.prefs.accent % NUM_ACCENTS]); }

// ---------------------------------------------------------------- navigation

static void back_btn_cb(lv_event_t* e) { nav_pop(); }

static void gesture_cb(lv_event_t* e) {
  if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) nav_pop();
}

lv_obj_t* nav_new_screen(const char* title, lv_obj_t** body_out) {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, NULL);

  lv_obj_t* back = lv_button_create(scr);
  lv_obj_set_size(back, 44, 34);
  lv_obj_set_pos(back, 4, 3);
  lv_obj_set_style_bg_color(back, lv_color_hex(COL_CARD_HI), 0);
  lv_obj_set_style_radius(back, 17, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* bl = lv_label_create(back);
  lv_label_set_text(bl, LV_SYMBOL_LEFT);
  lv_obj_center(bl);

  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, title);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, SCR_W - 60);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_pos(t, 54, 8);

  lv_obj_t* body = lv_obj_create(scr);
  lv_obj_set_pos(body, 0, 40);
  lv_obj_set_size(body, SCR_W, SCR_H - 40);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_radius(body, 0, 0);
  lv_obj_set_style_pad_all(body, 6, 0);
  lv_obj_set_style_pad_row(body, 6, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_ACTIVE);
  if (body_out) *body_out = body;
  return scr;
}

void nav_push(lv_obj_t* scr) {
  if (nav_depth >= NAV_MAX_DEPTH) {   // shouldn't happen; drop the oldest
    lv_obj_delete(nav_stack[0]);
    memmove(nav_stack, nav_stack + 1, sizeof(nav_stack[0]) * (NAV_MAX_DEPTH - 1));
    nav_depth--;
  }
  nav_stack[nav_depth++] = scr;
  lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, NAV_ANIM_MS, 0, false);
}

void nav_pop() {
  if (nav_depth == 0) return;
  nav_depth--;
  lv_obj_t* prev = nav_depth > 0 ? nav_stack[nav_depth - 1] : home_scr;
  lv_screen_load_anim(prev, LV_SCR_LOAD_ANIM_MOVE_RIGHT, NAV_ANIM_MS, 0, true);   // deletes the popped screen
  if (prev == home_scr) {
    chats_refresh();
    face_refresh();
  } else {
    conversation_refresh();   // no-op unless the conversation is what we returned to
  }
}

lv_obj_t* nav_home_tile(int idx) { return tiles[idx]; }

void nav_show_tile(int idx) {
  lv_tileview_set_tile_by_index(tileview, idx, 0, LV_ANIM_OFF);
}

void screens_go_home() {
  if (nav_depth > 0) {
    lv_screen_load(home_scr);
    while (nav_depth > 0) lv_obj_delete(nav_stack[--nav_depth]);
  }
  if (notify_panel) {
    lv_obj_delete(notify_panel);
    notify_panel = NULL;
  }
  if (notify_timer) {
    lv_timer_delete(notify_timer);
    notify_timer = NULL;
  }
  nav_show_tile(TILE_FACE);
  chats_refresh();
  face_refresh();
}

// ---------------------------------------------------------------- widgets

lv_obj_t* ui_column(lv_obj_t* parent) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_width(c, lv_pct(100));
  lv_obj_set_height(c, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_set_style_pad_row(c, 6, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t* ui_title(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(l, accent(), 0);
  return l;
}

lv_obj_t* ui_label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  if (font) lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

lv_obj_t* ui_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user_data) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, 40);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

lv_obj_t* ui_row(lv_obj_t* parent, const char* icon, const char* title, const char* subtitle, int badge,
                 lv_event_cb_t cb, void* user_data) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD_HI), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 8, 0);
  lv_obj_set_style_pad_column(b, 8, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);

  if (icon) {
    lv_obj_t* il = lv_label_create(b);
    lv_label_set_text(il, icon);
    lv_obj_set_style_text_color(il, accent(), 0);
    lv_obj_set_width(il, 20);
  }

  lv_obj_t* col = lv_obj_create(b);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, 1, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* tl = lv_label_create(col);
  lv_label_set_text(tl, title);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tl, lv_pct(100));
  lv_obj_set_style_text_color(tl, lv_color_hex(COL_TEXT), 0);

  if (subtitle && subtitle[0]) {
    lv_obj_t* sl = lv_label_create(col);
    lv_label_set_text(sl, subtitle);
    lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sl, lv_pct(100));
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(COL_MUTED), 0);
  }

  if (badge > 0) {
    lv_obj_t* bd = lv_label_create(b);
    lv_label_set_text_fmt(bd, "%d", badge > 99 ? 99 : badge);
    lv_obj_set_style_bg_color(bd, accent(), 0);
    lv_obj_set_style_bg_opa(bd, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bd, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(bd, 7, 0);
    lv_obj_set_style_pad_ver(bd, 2, 0);
    lv_obj_set_style_text_color(bd, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_font(bd, &lv_font_montserrat_12, 0);
  }
  return b;
}

// ---------------------------------------------------------------- formatting

void fmt_clock(char* buf, size_t sz, uint32_t epoch) {
  time_t t = (time_t)epoch + (int32_t)ui_task.prefs.tz_minutes * 60;
  struct tm tm;
  gmtime_r(&t, &tm);
  if (ui_task.prefs.use_24h) {
    snprintf(buf, sz, "%02d:%02d", tm.tm_hour, tm.tm_min);
  } else {
    int h = tm.tm_hour % 12;
    snprintf(buf, sz, "%d:%02d%c", h == 0 ? 12 : h, tm.tm_min, tm.tm_hour < 12 ? 'a' : 'p');
  }
}

void fmt_age(char* buf, size_t sz, uint32_t epoch) {
  uint32_t now = ui_task.now();
  if (epoch == 0 || epoch > now + 60) { snprintf(buf, sz, "?"); return; }
  uint32_t d = now - epoch;
  if (d < 60) snprintf(buf, sz, "now");
  else if (d < 3600) snprintf(buf, sz, "%um", (unsigned)(d / 60));
  else if (d < 86400) snprintf(buf, sz, "%uh", (unsigned)(d / 3600));
  else snprintf(buf, sz, "%ud", (unsigned)(d / 86400));
}

void fmt_hops(char* buf, size_t sz, uint8_t path_len) {
  if (path_len == 0xFF) { snprintf(buf, sz, "flood"); return; }
  int n = path_len & 63;
  if (n == 0) snprintf(buf, sz, "direct");
  else snprintf(buf, sz, "%d hop%s", n, n == 1 ? "" : "s");
}

bool contact_distance(int32_t lat_e6, int32_t lon_e6, double* km_out, double* bearing_out) {
  SensorManager* s = ui_task.sensors();
  if ((lat_e6 == 0 && lon_e6 == 0) || s == NULL || (s->node_lat == 0 && s->node_lon == 0)) return false;
  double lat1 = s->node_lat * DEG_TO_RAD, lon1 = s->node_lon * DEG_TO_RAD;
  double lat2 = (lat_e6 / 1e6) * DEG_TO_RAD, lon2 = (lon_e6 / 1e6) * DEG_TO_RAD;
  double dlat = lat2 - lat1, dlon = lon2 - lon1;
  double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat1) * cos(lat2) * sin(dlon / 2) * sin(dlon / 2);
  *km_out = 6371.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
  if (bearing_out) {
    double y = sin(dlon) * cos(lat2);
    double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon);
    double b = atan2(y, x) * RAD_TO_DEG;
    *bearing_out = b < 0 ? b + 360 : b;
  }
  return true;
}

const char* contact_type_icon(uint8_t type) {
  switch (type) {
    case ADV_TYPE_REPEATER: return LV_SYMBOL_WIFI;
    case ADV_TYPE_ROOM:     return LV_SYMBOL_HOME;
    case ADV_TYPE_SENSOR:   return LV_SYMBOL_EYE_OPEN;
    default:                return LV_SYMBOL_CALL;
  }
}

const char* conv_title(const ConvKey& k, char* buf, size_t sz) {
  if (k.kind == MSG_KIND_CHANNEL) {
    ChannelDetails ch;
    if (the_mesh.getChannel(k.chan, ch) && ch.name[0]) snprintf(buf, sz, "%s", ch.name);   // hashtag channels carry their own '#' 
    else snprintf(buf, sz, "Channel %d", k.chan);
  } else {
    ContactInfo* c = the_mesh.lookupContactByPubKey(k.key, 6);
    if (c) snprintf(buf, sz, "%s", c->name);
    else snprintf(buf, sz, "%02x%02x%02x", k.key[0], k.key[1], k.key[2]);
  }
  return buf;
}

// ---------------------------------------------------------------- notifications

static void notify_close() {
  if (notify_timer) { lv_timer_delete(notify_timer); notify_timer = NULL; }
  if (notify_panel) { lv_obj_delete(notify_panel); notify_panel = NULL; }
}

static void notify_timer_cb(lv_timer_t* t) {
  notify_timer = NULL;   // one-shot timers delete themselves
  if (notify_panel) { lv_obj_delete(notify_panel); notify_panel = NULL; }
}

static void notify_click_cb(lv_event_t* e) {
  ConvKey k = notify_conv;
  notify_close();
  screens_go_home();
  conversation_open(k);
}

void screens_show_notification(const char* title, const char* text, const ConvKey& k) {
  notify_close();
  notify_conv = k;

  notify_panel = lv_obj_create(lv_layer_top());
  lv_obj_set_size(notify_panel, SCR_W - 12, LV_SIZE_CONTENT);
  lv_obj_align(notify_panel, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_bg_color(notify_panel, lv_color_hex(COL_CARD_HI), 0);
  lv_obj_set_style_border_color(notify_panel, accent(), 0);
  lv_obj_set_style_border_width(notify_panel, 2, 0);
  lv_obj_set_style_radius(notify_panel, 16, 0);
  lv_obj_set_style_pad_all(notify_panel, 10, 0);
  lv_obj_set_style_pad_row(notify_panel, 2, 0);
  lv_obj_set_flex_flow(notify_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(notify_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(notify_panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(notify_panel, notify_click_cb, LV_EVENT_CLICKED, NULL);

  char hdr[64];
  snprintf(hdr, sizeof(hdr), LV_SYMBOL_ENVELOPE "  %s", title);
  lv_obj_t* tl = ui_label(notify_panel, hdr, &lv_font_montserrat_16, COL_TEXT);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tl, lv_pct(100));
  lv_obj_t* bl = ui_label(notify_panel, text, &lv_font_montserrat_14, 0xD0D0D0);
  lv_label_set_long_mode(bl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(bl, lv_pct(100));
  lv_obj_set_style_max_height(bl, 60, 0);

  notify_timer = lv_timer_create(notify_timer_cb, NOTIFY_MS, NULL);
  lv_timer_set_repeat_count(notify_timer, 1);
}

// ---------------------------------------------------------------- entry points

static void tile_changed_cb(lv_event_t* e) {
  lv_obj_t* tile = lv_tileview_get_tile_active(tileview);
  if (tile == tiles[TILE_CHATS]) chats_refresh();
  else if (tile == tiles[TILE_CONTACTS]) contacts_refresh();
  else if (tile == tiles[TILE_MESH]) mesh_refresh();
}

static void build_home() {
  home_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(home_scr, lv_color_hex(COL_BG), 0);

  tileview = lv_tileview_create(home_scr);
  lv_obj_set_size(tileview, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(tileview, lv_color_hex(COL_BG), 0);
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < NUM_TILES; i++) {
    lv_dir_t dir = (lv_dir_t)((i > 0 ? LV_DIR_LEFT : 0) | (i < NUM_TILES - 1 ? LV_DIR_RIGHT : 0));
    tiles[i] = lv_tileview_add_tile(tileview, i, 0, dir);
  }
  lv_obj_add_event_cb(tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

  face_create(tiles[TILE_FACE]);
  chats_create(tiles[TILE_CHATS]);
  contacts_create(tiles[TILE_CONTACTS]);
  mesh_create(tiles[TILE_MESH]);
  settings_create(tiles[TILE_SETTINGS]);
}

void screens_create() {
  build_home();
  lv_screen_load(home_scr);
  face_refresh();
}

void screens_rebuild() {
  lv_theme_t* th = lv_theme_default_init(lv_display_get_default(), accent(), lv_color_hex(0x444444), true,
                                         &lv_font_montserrat_16);
  lv_display_set_theme(lv_display_get_default(), th);
  notify_close();
  lv_obj_t* old = home_scr;
  while (nav_depth > 0) lv_obj_delete(nav_stack[--nav_depth]);
  build_home();
  lv_screen_load(home_scr);
  lv_obj_delete(old);
  nav_show_tile(TILE_SETTINGS);
  face_refresh();
}

void screens_refresh_status() {
  if (lv_screen_active() != home_scr) return;
  lv_obj_t* tile = lv_tileview_get_tile_active(tileview);
  if (tile == tiles[TILE_FACE]) face_refresh();
  else if (tile == tiles[TILE_MESH]) mesh_refresh();
}

void screens_on_screen_off() { notify_close(); }

bool screens_keep_awake() {
  return compose_is_open() && lv_display_get_inactive_time(NULL) < 60000;
}

bool screens_is_viewing(const ConvKey& k) { return conversation_is_open(k); }

void screens_on_message(const ConvKey& k) {
  if (conversation_is_open(k)) conversation_refresh();
  chats_refresh();
  face_refresh();
}

void screens_on_contacts_changed() {
  if (lv_screen_active() == home_scr && lv_tileview_get_tile_active(tileview) == tiles[TILE_CONTACTS]) {
    contacts_refresh();
  }
}

