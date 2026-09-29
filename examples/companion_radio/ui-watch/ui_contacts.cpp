// Contacts tile and contact detail screen.

#include "ui_internal.h"
#include "../MyMesh.h"
#include <helpers/AdvertDataHelpers.h>
#include <esp32-hal-psram.h>

#define MAX_CONTACT_ROWS 50

enum ContactFilter { FILTER_ALL = 0, FILTER_PEOPLE, FILTER_INFRA, NUM_FILTERS };
static const char* FILTER_NAMES[NUM_FILTERS] = { "All", "People", "Nodes" };

static lv_obj_t* contacts_list = NULL;
static lv_obj_t* filter_btns[NUM_FILTERS];
static int filter = FILTER_ALL;
static uint8_t row_keys[MAX_CONTACT_ROWS][6];

struct ContactSortEntry {
  uint32_t lastmod;
  uint16_t idx;
};

static int sort_recent(const void* a, const void* b) {
  uint32_t la = ((const ContactSortEntry*)a)->lastmod, lb = ((const ContactSortEntry*)b)->lastmod;
  return la < lb ? 1 : (la > lb ? -1 : 0);
}

static bool filter_match(uint8_t type) {
  if (filter == FILTER_PEOPLE) return type == ADV_TYPE_CHAT;
  if (filter == FILTER_INFRA) return type != ADV_TYPE_CHAT;
  return true;
}

static void contact_row_cb(lv_event_t* e) {
  contact_detail_open((const uint8_t*) lv_event_get_user_data(e));
}

static void filter_cb(lv_event_t* e) {
  filter = (int)(intptr_t) lv_event_get_user_data(e);
  contacts_refresh();
}

void contacts_create(lv_obj_t* tile) {
  lv_obj_set_style_pad_all(tile, 6, 0);
  lv_obj_set_style_pad_row(tile, 6, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(tile, LV_DIR_VER);
  ui_title(tile, LV_SYMBOL_CALL "  Contacts");

  lv_obj_t* bar = lv_obj_create(tile);
  lv_obj_set_size(bar, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_set_style_pad_column(bar, 4, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < NUM_FILTERS; i++) {
    lv_obj_t* b = lv_button_create(bar);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 30);
    lv_obj_set_style_radius(b, 15, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, filter_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, FILTER_NAMES[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
    filter_btns[i] = b;
  }

  contacts_list = ui_column(tile);
}

void contacts_refresh() {
  if (contacts_list == NULL) return;
  lv_obj_clean(contacts_list);
  for (int i = 0; i < NUM_FILTERS; i++) {
    lv_obj_set_style_bg_color(filter_btns[i], i == filter ? accent() : lv_color_hex(COL_CARD_HI), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(filter_btns[i], 0), lv_color_hex(i == filter ? COL_BG : COL_TEXT), 0);
  }

  int total = the_mesh.getNumContacts();
  if (total <= 0) {
    ui_label(contacts_list, "No contacts yet.\nThey appear as adverts are heard.", &lv_font_montserrat_14, COL_MUTED);
    return;
  }

  ContactSortEntry* entries = (ContactSortEntry*) ps_malloc(sizeof(ContactSortEntry) * total);
  if (entries == NULL) return;
  int n = 0;
  ContactInfo c;
  for (int i = 0; i < total; i++) {
    if (the_mesh.getContactByIdx(i, c) && filter_match(c.type)) {
      entries[n].lastmod = c.lastmod;
      entries[n].idx = i;
      n++;
    }
  }
  qsort(entries, n, sizeof(ContactSortEntry), sort_recent);

  int shown = n < MAX_CONTACT_ROWS ? n : MAX_CONTACT_ROWS;
  for (int i = 0; i < shown; i++) {
    if (!the_mesh.getContactByIdx(entries[i].idx, c)) continue;
    memcpy(row_keys[i], c.id.pub_key, 6);
    char sub[64], age[8], hops[12];
    fmt_age(age, sizeof(age), c.lastmod);
    fmt_hops(hops, sizeof(hops), c.out_path_len);
    double km;
    if (contact_distance(c.gps_lat, c.gps_lon, &km, NULL)) {
      snprintf(sub, sizeof(sub), "%s " LV_SYMBOL_BULLET " %s " LV_SYMBOL_BULLET " %.1fkm", age, hops, km);
    } else {
      snprintf(sub, sizeof(sub), "%s " LV_SYMBOL_BULLET " %s", age, hops);
    }
    ui_row(contacts_list, contact_type_icon(c.type), c.name, sub, 0, contact_row_cb, row_keys[i]);
  }
  if (n > shown) {
    char more[40];
    snprintf(more, sizeof(more), "+%d more", n - shown);
    ui_label(contacts_list, more, &lv_font_montserrat_12, COL_MUTED);
  }
  free(entries);
}

// ---------------------------------------------------------------- detail

static uint8_t detail_key[6];
static lv_obj_t* detail_status = NULL;

static void detail_deleted_cb(lv_event_t* e) { detail_status = NULL; }

static void detail_msg_cb(lv_event_t* e) { conversation_open(ConvKey::direct(detail_key)); }

static void detail_reset_cb(lv_event_t* e) {
  bool ok = the_mesh.uiResetPath(detail_key);
  if (detail_status) lv_label_set_text(detail_status, ok ? "Path reset - next message floods" : "Failed");
}

static void detail_share_cb(lv_event_t* e) {
  bool ok = the_mesh.uiShareContact(detail_key);
  if (detail_status) lv_label_set_text(detail_status, ok ? "Shared with nearby nodes" : "Failed");
}

static const char* type_name(uint8_t type) {
  switch (type) {
    case ADV_TYPE_CHAT:     return "Companion";
    case ADV_TYPE_REPEATER: return "Repeater";
    case ADV_TYPE_ROOM:     return "Room server";
    case ADV_TYPE_SENSOR:   return "Sensor";
    default:                return "Unknown";
  }
}

static const char* compass_point(double bearing) {
  static const char* pts[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  return pts[((int)((bearing + 22.5) / 45.0)) & 7];
}

void contact_detail_open(const uint8_t* key_prefix) {
  memcpy(detail_key, key_prefix, 6);
  ContactInfo* c = the_mesh.lookupContactByPubKey(detail_key, 6);
  if (c == NULL) return;

  lv_obj_t* body;
  lv_obj_t* scr = nav_new_screen(c->name, &body);
  lv_obj_add_event_cb(scr, detail_deleted_cb, LV_EVENT_DELETE, NULL);

  char buf[96], age[8], hops[12];
  fmt_age(age, sizeof(age), c->lastmod);
  fmt_hops(hops, sizeof(hops), c->out_path_len);
  snprintf(buf, sizeof(buf), "%s %s\nHeard %s ago " LV_SYMBOL_BULLET " %s", contact_type_icon(c->type), type_name(c->type),
           age, c->out_path_len == 0xFF ? "no path (flood)" : hops);
  ui_label(body, buf, &lv_font_montserrat_14, COL_TEXT);

  double km, bearing;
  if (contact_distance(c->gps_lat, c->gps_lon, &km, &bearing)) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " %.2f km %s %.0f deg", km, compass_point(bearing), bearing);
    ui_label(body, buf, &lv_font_montserrat_16, ACCENT_COLORS[ui_task.prefs.accent % NUM_ACCENTS]);
  } else if (c->gps_lat != 0 || c->gps_lon != 0) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " %.5f, %.5f", c->gps_lat / 1e6, c->gps_lon / 1e6);
    ui_label(body, buf, &lv_font_montserrat_14, COL_MUTED);
  }

  snprintf(buf, sizeof(buf), "Key %02x%02x%02x%02x%02x%02x...", detail_key[0], detail_key[1], detail_key[2],
           detail_key[3], detail_key[4], detail_key[5]);
  ui_label(body, buf, &lv_font_montserrat_12, COL_MUTED);

  if (c->type == ADV_TYPE_CHAT) ui_button(body, LV_SYMBOL_ENVELOPE "  Message", detail_msg_cb, NULL);
  lv_obj_t* b = ui_button(body, LV_SYMBOL_REFRESH "  Reset path", detail_reset_cb, NULL);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD_HI), 0);
  b = ui_button(body, LV_SYMBOL_UPLOAD "  Share contact", detail_share_cb, NULL);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD_HI), 0);

  detail_status = ui_label(body, "", &lv_font_montserrat_12, COL_MUTED);
  nav_push(scr);
}
