// Watch face tile: digital or analog clock plus status corners.

#include "ui_internal.h"
#include "target.h"
#include "../MyMesh.h"

static lv_obj_t* digital = NULL;
static lv_obj_t* sec_arc = NULL;
static lv_obj_t* time_lbl = NULL;
static lv_obj_t* date_lbl = NULL;

static lv_obj_t* analog = NULL;
static lv_obj_t* hand_h = NULL;
static lv_obj_t* hand_m = NULL;
static lv_obj_t* hand_s = NULL;
static lv_obj_t* analog_date = NULL;

static lv_obj_t* conn_lbl = NULL;    // top-left: bluetooth / gps
static lv_obj_t* batt_lbl = NULL;    // top-right
static lv_obj_t* unread_lbl = NULL;  // bottom-left
static lv_obj_t* name_lbl = NULL;    // bottom-right
static lv_obj_t* pin_lbl = NULL;     // pairing PIN while no phone has connected

static const char* DAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char* MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char* DIAL_LABELS[] = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "", NULL };

static void unread_click_cb(lv_event_t* e) { nav_show_tile(TILE_CHATS); chats_refresh(); }

static lv_obj_t* corner_label(lv_obj_t* tile, lv_align_t align, int x, int y, const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(tile);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(l, align, x, y);
  lv_label_set_text(l, "");
  return l;
}

#define DIAL_SIZE 222
#define DIAL_C    (DIAL_SIZE / 2)

static lv_point_precise_t hand_pts[3][2];

static lv_obj_t* make_hand(lv_obj_t* parent, int width, lv_color_t color) {
  lv_obj_t* l = lv_line_create(parent);
  lv_obj_set_pos(l, 0, 0);
  lv_obj_set_size(l, DIAL_SIZE, DIAL_SIZE);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  lv_obj_set_style_line_color(l, color, 0);
  return l;
}

// value in 0..60 around the dial; tail = length behind the hub
static void set_hand(lv_obj_t* line, int idx, float value, int length, int tail) {
  float a = (value / 60.0f) * 2 * PI - PI / 2;
  float c = cosf(a), s = sinf(a);
  hand_pts[idx][0].x = DIAL_C - c * tail;
  hand_pts[idx][0].y = DIAL_C - s * tail;
  hand_pts[idx][1].x = DIAL_C + c * length;
  hand_pts[idx][1].y = DIAL_C + s * length;
  lv_line_set_points(line, hand_pts[idx], 2);
}

// Renders the static dial (ticks + numerals) once onto a canvas. Drawing an
// lv_scale live cost ~45 ms per frame; the canvas is a plain blit.
static lv_obj_t* create_dial_image(lv_obj_t* tile) {
  lv_draw_buf_t* buf = lv_draw_buf_create(DIAL_SIZE, DIAL_SIZE, LV_COLOR_FORMAT_RGB565, 0);
  lv_obj_t* canvas = lv_canvas_create(tile);
  lv_obj_center(canvas);
  if (buf == NULL) return canvas;
  lv_canvas_set_draw_buf(canvas, buf);
  lv_canvas_fill_bg(canvas, lv_color_hex(COL_BG), LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.round_start = line.round_end = 1;

  lv_draw_label_dsc_t label;
  lv_draw_label_dsc_init(&label);
  label.font = &lv_font_montserrat_16;
  label.color = lv_color_hex(COL_TEXT);
  label.align = LV_TEXT_ALIGN_CENTER;

  const float r_out = DIAL_C - 2;
  for (int i = 0; i < 60; i++) {
    bool major = i % 5 == 0;
    float a = (i / 60.0f) * 2 * PI - PI / 2;
    float c = cosf(a), sn = sinf(a);
    float r_in = r_out - (major ? 10 : 4);
    line.width = major ? 3 : 1;
    line.color = lv_color_hex(major ? COL_TEXT : 0x505050);
    line.p1.x = DIAL_C + c * r_out; line.p1.y = DIAL_C + sn * r_out;
    line.p2.x = DIAL_C + c * r_in;  line.p2.y = DIAL_C + sn * r_in;
    lv_draw_line(&layer, &line);

    if (major) {
      float r_txt = r_out - 24;
      int cx = DIAL_C + c * r_txt, cy = DIAL_C + sn * r_txt;
      label.text = DIAL_LABELS[i / 5];
      lv_area_t area = { cx - 14, cy - 9, cx + 14, cy + 9 };
      lv_draw_label(&layer, &label, &area);
    }
  }
  lv_canvas_finish_layer(canvas, &layer);
  return canvas;
}

void face_create(lv_obj_t* tile) {
  lv_obj_set_style_bg_color(tile, lv_color_hex(COL_BG), 0);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

  // ---- digital
  digital = lv_obj_create(tile);
  lv_obj_set_size(digital, SCR_W, SCR_H);
  lv_obj_center(digital);
  lv_obj_set_style_bg_opa(digital, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(digital, 0, 0);
  lv_obj_set_style_pad_all(digital, 0, 0);
  lv_obj_remove_flag(digital, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(digital, LV_OBJ_FLAG_CLICKABLE);

  sec_arc = lv_arc_create(digital);
  lv_obj_set_size(sec_arc, 226, 226);
  lv_obj_center(sec_arc);
  lv_arc_set_rotation(sec_arc, 270);
  lv_arc_set_bg_angles(sec_arc, 0, 360);
  lv_arc_set_range(sec_arc, 0, 60);
  lv_obj_remove_style(sec_arc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(sec_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(sec_arc, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(sec_arc, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(sec_arc, lv_color_hex(COL_CARD), LV_PART_MAIN);
  lv_obj_set_style_arc_color(sec_arc, accent(), LV_PART_INDICATOR);

  time_lbl = lv_label_create(digital);
  lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(time_lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(time_lbl, LV_ALIGN_CENTER, 0, -12);

  date_lbl = lv_label_create(digital);
  lv_obj_set_style_text_font(date_lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(date_lbl, accent(), 0);
  lv_obj_align(date_lbl, LV_ALIGN_CENTER, 0, 30);

  // ---- analog
  analog = create_dial_image(tile);
  lv_obj_remove_flag(analog, LV_OBJ_FLAG_CLICKABLE);

  analog_date = lv_label_create(analog);
  lv_obj_set_style_text_font(analog_date, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(analog_date, accent(), 0);
  lv_obj_align(analog_date, LV_ALIGN_CENTER, 0, 42);

  hand_h = make_hand(analog, 6, lv_color_hex(COL_TEXT));
  hand_m = make_hand(analog, 4, lv_color_hex(COL_TEXT));
  hand_s = make_hand(analog, 2, accent());

  lv_obj_t* hub = lv_obj_create(analog);
  lv_obj_set_size(hub, 12, 12);
  lv_obj_center(hub);
  lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hub, accent(), 0);
  lv_obj_set_style_border_width(hub, 0, 0);

  pin_lbl = lv_label_create(tile);
  lv_obj_set_style_text_font(pin_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(pin_lbl, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(pin_lbl, LV_ALIGN_CENTER, 0, 62);

  // ---- status corners (outside the round dial)
  conn_lbl = corner_label(tile, LV_ALIGN_TOP_LEFT, 6, 4, &lv_font_montserrat_14);
  batt_lbl = corner_label(tile, LV_ALIGN_TOP_RIGHT, -6, 4, &lv_font_montserrat_14);
  name_lbl = corner_label(tile, LV_ALIGN_BOTTOM_RIGHT, -6, -4, &lv_font_montserrat_12);

  unread_lbl = corner_label(tile, LV_ALIGN_BOTTOM_LEFT, 6, -4, &lv_font_montserrat_16);
  lv_obj_set_style_text_color(unread_lbl, accent(), 0);
  lv_obj_add_flag(unread_lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(unread_lbl, 16);
  lv_obj_add_event_cb(unread_lbl, unread_click_cb, LV_EVENT_CLICKED, NULL);
}

static const char* batt_symbol(int pct) {
  if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
  if (pct >= 60) return LV_SYMBOL_BATTERY_3;
  if (pct >= 35) return LV_SYMBOL_BATTERY_2;
  if (pct >= 10) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

void face_refresh() {
  if (time_lbl == NULL) return;
  bool is_analog = ui_task.prefs.face == 1;
  if (is_analog) {
    lv_obj_add_flag(digital, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(analog, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(digital, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(analog, LV_OBJ_FLAG_HIDDEN);
  }

  struct tm tm;
  ui_task.localTime(&tm);
  bool valid = ui_task.timeValid();
  char buf[40];

  if (valid) snprintf(buf, sizeof(buf), "%s %d %s", DAYS[tm.tm_wday], tm.tm_mday, MONTHS[tm.tm_mon]);
  else snprintf(buf, sizeof(buf), "time not set");

  if (is_analog) {
    lv_label_set_text(analog_date, buf);
    set_hand(hand_h, 0, (tm.tm_hour % 12) * 5 + tm.tm_min / 12.0f, 55, 0);
    set_hand(hand_m, 1, tm.tm_min + tm.tm_sec / 60.0f, 80, 0);
    set_hand(hand_s, 2, tm.tm_sec, 92, 16);
  } else {
    lv_label_set_text(date_lbl, buf);
    if (valid) {
      fmt_clock(buf, sizeof(buf), ui_task.now());
      lv_label_set_text(time_lbl, buf);
    } else {
      lv_label_set_text(time_lbl, "--:--");
    }
    lv_arc_set_value(sec_arc, tm.tm_sec);
  }

  // connectivity corner
  // bright = phone connected, muted = advertising / no fix
  char conn[48] = "";
  if (ui_task.isBluetoothEnabled()) strcat(conn, LV_SYMBOL_BLUETOOTH);
  LocationProvider* lp = ui_task.sensors() ? ui_task.sensors()->getLocationProvider() : NULL;
  if (lp && lp->isEnabled()) {
    char g[24];
    if (lp->isValid()) snprintf(g, sizeof(g), " " LV_SYMBOL_GPS "%ld", lp->satellitesCount());
    else snprintf(g, sizeof(g), " " LV_SYMBOL_GPS "?");
    strcat(conn, g);
  }
  lv_obj_set_style_text_color(conn_lbl, ui_task.hasConnection() ? accent() : lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(conn_lbl, conn);

  int pct = board.getBattPercent();
  if (pct >= 0) {
    snprintf(buf, sizeof(buf), "%s%d%% %s", board.isCharging() ? LV_SYMBOL_CHARGE " " : "", pct, batt_symbol(pct));
    lv_obj_set_style_text_color(batt_lbl, lv_color_hex(pct < 15 && !board.isCharging() ? COL_BAD : COL_MUTED), 0);
  } else {
    snprintf(buf, sizeof(buf), board.isVbusIn() ? LV_SYMBOL_USB : "");
  }
  lv_label_set_text(batt_lbl, buf);

  int unread = ui_task.store.totalUnread();
  if (unread > 0) lv_label_set_text_fmt(unread_lbl, LV_SYMBOL_ENVELOPE " %d", unread);
  else lv_label_set_text(unread_lbl, "");

  lv_label_set_text(name_lbl, ui_task.nodePrefs()->node_name);

  // The PIN is random each boot on devices with a display, so surface it until a phone has paired.
  static bool phone_seen = false;
  if (ui_task.hasConnection()) phone_seen = true;
  if (ui_task.isBluetoothEnabled() && !phone_seen && the_mesh.getBLEPin() != 0) {
    lv_label_set_text_fmt(pin_lbl, LV_SYMBOL_BLUETOOTH " PIN %06u", (unsigned)the_mesh.getBLEPin());
    lv_obj_remove_flag(pin_lbl, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(pin_lbl, LV_OBJ_FLAG_HIDDEN);
  }
}
