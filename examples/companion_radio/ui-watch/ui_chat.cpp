// Chats tile, conversation view, quick replies and the compose keyboard.

#include "ui_internal.h"
#include "../MyMesh.h"

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 8
#endif

#define MAX_CHAT_ROWS     48
#define CONV_SHOW_LAST    40
#define MAX_TEXT_LEN      150

static const char* QUICK_REPLIES[] = {
  "Hi!", "Hello", "OK", "Yes", "No", "Thanks!", "Copy that", "Test",
  "On my way", "Be there soon", "Running late", "I'm here", "Where are you?",
  "All good here", "Can't talk now", "Call me", "Need help",
  "Good morning", "Good night", "See you soon",
};
#define NUM_QUICK_REPLIES (sizeof(QUICK_REPLIES) / sizeof(QUICK_REPLIES[0]))

// ---------------------------------------------------------------- chats tile

static lv_obj_t* chats_list = NULL;
static ConvKey row_keys[MAX_CHAT_ROWS];

static void chat_row_cb(lv_event_t* e) {
  ConvKey* k = (ConvKey*) lv_event_get_user_data(e);
  conversation_open(*k);
}

void chats_create(lv_obj_t* tile) {
  lv_obj_set_style_pad_all(tile, 6, 0);
  lv_obj_set_style_pad_row(tile, 6, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(tile, LV_DIR_VER);
  ui_title(tile, LV_SYMBOL_ENVELOPE "  Chats");
  chats_list = ui_column(tile);
}

void chats_refresh() {
  if (chats_list == NULL) return;
  lv_obj_clean(chats_list);

  ConvSummary convs[MAX_CHAT_ROWS];
  int n = ui_task.store.listConversations(convs, MAX_CHAT_ROWS);

  // channels without any traffic yet
  for (int idx = 0; idx < MAX_GROUP_CHANNELS && n < MAX_CHAT_ROWS; idx++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(idx, ch) || ch.name[0] == 0) continue;
    ConvKey k = ConvKey::channel(idx);
    bool listed = false;
    for (int i = 0; i < n && !listed; i++) listed = convs[i].key == k;
    if (!listed) {
      convs[n].key = k;
      convs[n].last_idx = -1;
      convs[n].unread = 0;
      n++;
    }
  }

  if (n == 0) {
    ui_label(chats_list, "No chats yet.\nPick someone from Contacts, or add channels from the phone app.",
             &lv_font_montserrat_14, COL_MUTED);
    return;
  }

  for (int i = 0; i < n; i++) {
    row_keys[i] = convs[i].key;
    char title[40], sub[96], age[8];
    conv_title(convs[i].key, title, sizeof(title));
    if (convs[i].last_idx >= 0) {
      StoredMsg& m = ui_task.store.at(convs[i].last_idx);
      fmt_age(age, sizeof(age), m.ts);
      if (m.flags & MSG_FLAG_OUTGOING) snprintf(sub, sizeof(sub), "%s " LV_SYMBOL_BULLET " You: %s", age, m.text);
      else if (m.kind == MSG_KIND_CHANNEL && m.sender[0]) snprintf(sub, sizeof(sub), "%s " LV_SYMBOL_BULLET " %s: %s", age, m.sender, m.text);
      else snprintf(sub, sizeof(sub), "%s " LV_SYMBOL_BULLET " %s", age, m.text);
    } else {
      snprintf(sub, sizeof(sub), "no messages");
    }
    const char* icon = convs[i].key.kind == MSG_KIND_CHANNEL ? LV_SYMBOL_LIST : LV_SYMBOL_CALL;
    ui_row(chats_list, icon, title, sub, convs[i].unread, chat_row_cb, &row_keys[i]);
  }
}

// ---------------------------------------------------------------- compose

static lv_obj_t* compose_scr = NULL;
static lv_obj_t* compose_ta = NULL;
static void (*compose_done)(const char* text, void* ctx) = NULL;
static void* compose_ctx = NULL;

bool compose_is_open() { return compose_scr != NULL; }

static void compose_deleted_cb(lv_event_t* e) { compose_scr = NULL; compose_ta = NULL; }

static void compose_kb_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char* text = lv_textarea_get_text(compose_ta);
    if (text && text[0] && compose_done) compose_done(text, compose_ctx);
    nav_pop();
  } else if (code == LV_EVENT_CANCEL) {
    nav_pop();
  }
}

void compose_open(const char* title, const char* initial, int max_len, void (*on_done)(const char* text, void* ctx), void* ctx) {
  compose_done = on_done;
  compose_ctx = ctx;

  compose_scr = nav_new_screen(title, NULL);
  lv_obj_add_event_cb(compose_scr, compose_deleted_cb, LV_EVENT_DELETE, NULL);
  // the scrollable body from nav_new_screen isn't used here
  lv_obj_delete(lv_obj_get_child(compose_scr, 2));

  compose_ta = lv_textarea_create(compose_scr);
  lv_obj_set_pos(compose_ta, 4, 40);
  lv_obj_set_size(compose_ta, SCR_W - 8, 58);
  lv_textarea_set_max_length(compose_ta, max_len);
  lv_textarea_set_text(compose_ta, initial ? initial : "");
  lv_obj_set_style_text_font(compose_ta, &lv_font_montserrat_14, 0);
  lv_obj_add_state(compose_ta, LV_STATE_FOCUSED);

  lv_obj_t* kb = lv_keyboard_create(compose_scr);
  lv_obj_set_size(kb, SCR_W, SCR_H - 102);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_all(kb, 2, 0);
  lv_obj_set_style_pad_gap(kb, 2, 0);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, LV_PART_ITEMS);
  lv_keyboard_set_textarea(kb, compose_ta);
  lv_obj_add_event_cb(kb, compose_kb_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kb, compose_kb_cb, LV_EVENT_CANCEL, NULL);

  nav_push(compose_scr);
}

// ---------------------------------------------------------------- conversation

static lv_obj_t* conv_scr = NULL;
static lv_obj_t* conv_body = NULL;
static ConvKey conv_key;

bool conversation_is_open(const ConvKey& k) {
  return conv_scr != NULL && lv_screen_active() == conv_scr && conv_key == k;
}

static void conv_deleted_cb(lv_event_t* e) { conv_scr = NULL; conv_body = NULL; }

static void send_text(const char* text) {
  if (conv_key.kind == MSG_KIND_CHANNEL) ui_task.sendChannel(conv_key.chan, text);
  else ui_task.sendDirect(conv_key.key, text);
}

static void compose_send_done(const char* text, void* ctx) { send_text(text); }

static void type_btn_cb(lv_event_t* e) {
  compose_open("Message", "", MAX_TEXT_LEN, compose_send_done, NULL);
}

static void quick_pick_cb(lv_event_t* e) {
  const char* text = (const char*) lv_event_get_user_data(e);
  send_text(text);
  nav_pop();
}

static char location_msg[64];

static void quick_location_cb(lv_event_t* e) {
  SensorManager* s = ui_task.sensors();
  snprintf(location_msg, sizeof(location_msg), "My location: %.5f,%.5f", s->node_lat, s->node_lon);
  send_text(location_msg);
  nav_pop();
}

static void quick_btn_cb(lv_event_t* e) {
  lv_obj_t* body;
  lv_obj_t* scr = nav_new_screen("Quick reply", &body);
  SensorManager* s = ui_task.sensors();
  if (s && (s->node_lat != 0 || s->node_lon != 0)) {
    ui_row(body, LV_SYMBOL_GPS, "Send my location", NULL, 0, quick_location_cb, NULL);
  }
  for (size_t i = 0; i < NUM_QUICK_REPLIES; i++) {
    ui_row(body, NULL, QUICK_REPLIES[i], NULL, 0, quick_pick_cb, (void*)QUICK_REPLIES[i]);
  }
  nav_push(scr);
}

static void resend_cb(lv_event_t* e) {
  StoredMsg* m = (StoredMsg*) lv_event_get_user_data(e);
  if (m->status != MSG_STATUS_FAILED || !(m->flags & MSG_FLAG_OUTGOING)) return;
  char text[sizeof(m->text)];
  strcpy(text, m->text);
  send_text(text);
  conversation_refresh();
}

static void add_bubble(lv_obj_t* parent, StoredMsg& m) {
  bool out = m.flags & MSG_FLAG_OUTGOING;

  lv_obj_t* wrap = lv_obj_create(parent);
  lv_obj_set_width(wrap, lv_pct(100));
  lv_obj_set_height(wrap, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wrap, 0, 0);
  lv_obj_set_style_pad_all(wrap, 0, 0);
  lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wrap, out ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(wrap, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* b = lv_obj_create(wrap);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(b, 196, 0);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 6, 0);
  lv_obj_set_style_pad_row(b, 2, 0);
  lv_obj_set_style_bg_color(b, out ? accent() : lv_color_hex(COL_CARD_HI), 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  uint32_t fg = out ? COL_BG : COL_TEXT;

  if (!out && m.kind == MSG_KIND_CHANNEL && m.sender[0]) {
    lv_obj_t* s = ui_label(b, m.sender, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s, accent(), 0);
  }

  lv_obj_t* t = ui_label(b, m.text, &lv_font_montserrat_16, fg);
  lv_obj_set_style_max_width(t, 176, 0);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);

  char meta[48], clock[12], hops[12];
  fmt_clock(clock, sizeof(clock), m.ts);
  if (out) {
    const char* st = "";
    switch (m.status) {
      case MSG_STATUS_SENDING:   st = "  ..."; break;
      case MSG_STATUS_DELIVERED: st = "  " LV_SYMBOL_OK; break;
      case MSG_STATUS_FAILED:    st = "  " LV_SYMBOL_CLOSE " tap to resend"; break;
      case MSG_STATUS_SENT:      st = "  sent"; break;
    }
    snprintf(meta, sizeof(meta), "%s%s", clock, st);
    if (m.status == MSG_STATUS_FAILED) {
      lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(b, resend_cb, LV_EVENT_CLICKED, &m);
      lv_obj_set_style_bg_color(b, lv_color_hex(0x5A1F1F), 0);
      fg = COL_TEXT;
      lv_obj_set_style_text_color(t, lv_color_hex(fg), 0);
    }
  } else {
    fmt_hops(hops, sizeof(hops), m.hops);
    snprintf(meta, sizeof(meta), "%s " LV_SYMBOL_BULLET " %s " LV_SYMBOL_BULLET " %ddB", clock, hops, m.snr4 / 4);
  }
  lv_obj_t* ml = ui_label(b, meta, &lv_font_montserrat_12, out ? 0x202020 : COL_MUTED);
  if (m.status == MSG_STATUS_FAILED) lv_obj_set_style_text_color(ml, lv_color_hex(0xFF9A9A), 0);
}

void conversation_refresh() {
  if (conv_scr == NULL || conv_body == NULL) return;
  lv_obj_clean(conv_body);

  int total = ui_task.store.count();
  int shown = 0, first = total;
  for (int i = total - 1; i >= 0 && shown < CONV_SHOW_LAST; i--) {
    if (conv_key.matches(ui_task.store.at(i))) { first = i; shown++; }
  }
  if (shown == 0) {
    ui_label(conv_body, "No messages yet.\nSay hi!", &lv_font_montserrat_14, COL_MUTED);
  }
  for (int i = first; i < total; i++) {
    StoredMsg& m = ui_task.store.at(i);
    if (conv_key.matches(m)) add_bubble(conv_body, m);
  }
  ui_task.store.markRead(conv_key);

  lv_obj_update_layout(conv_body);
  lv_obj_scroll_to_y(conv_body, LV_COORD_MAX, LV_ANIM_OFF);
}

void conversation_open(const ConvKey& k) {
  conv_key = k;
  char title[40];
  conv_title(k, title, sizeof(title));
  conv_scr = nav_new_screen(title, &conv_body);
  lv_obj_add_event_cb(conv_scr, conv_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_obj_set_height(conv_body, SCR_H - 40 - 46);

  lv_obj_t* bar = lv_obj_create(conv_scr);
  lv_obj_set_size(bar, SCR_W, 46);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 4, 0);
  lv_obj_set_style_pad_column(bar, 6, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* q = ui_button(bar, LV_SYMBOL_LIST " Quick", quick_btn_cb, NULL);
  lv_obj_set_flex_grow(q, 1);
  lv_obj_set_height(q, 38);
  lv_obj_set_style_bg_color(q, lv_color_hex(COL_CARD_HI), 0);
  lv_obj_t* t = ui_button(bar, LV_SYMBOL_KEYBOARD " Type", type_btn_cb, NULL);
  lv_obj_set_flex_grow(t, 1);
  lv_obj_set_height(t, 38);

  nav_push(conv_scr);
  conversation_refresh();
}
