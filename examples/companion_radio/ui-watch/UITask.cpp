#include "UITask.h"
#include "ui_screens.h"
#include "../MyMesh.h"
#include "target.h"
#include <SPIFFS.h>
#include <esp32-hal-psram.h>
#include <helpers/TxtDataHelpers.h>

#define PREFS_FILE   "/mw_prefs"
#define PREFS_MAGIC  0x4D575046UL   // 'MWPF'
#define PREFS_VERSION 2

#define LCD_W 240
#define LCD_H 240
#define DRAW_BUF_LINES 40

#define CPU_MHZ_ACTIVE 240
#define CPU_MHZ_IDLE    80

// LVGL layout/draw recursion + mesh handling need more than Arduino's default 8 KB loop stack.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

#ifdef MW_DEBUG
  #define MWDBG(...) do { Serial.printf(__VA_ARGS__); Serial.flush(); } while (0)
#else
  #define MWDBG(...)
#endif

const uint16_t SCREEN_TIMEOUTS[NUM_SCREEN_TIMEOUTS] = { 10, 15, 30, 60, 300 };
const uint32_t ACCENT_COLORS[NUM_ACCENTS] = { 0x00B4D8, 0x2ECC71, 0xFF8C00, 0xE63946, 0xB388FF };

static lgfx::LGFX_Device* lcd() { return &display.device(); }

// ---------------------------------------------------------------- LVGL glue

// Pixels are pre-swapped so LovyanGFX can push them without converting.
// (pushImageDMA left the panel black on this board, so transfers are synchronous.)
void UITask::flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  lv_draw_sw_rgb565_swap(px_map, w * h);   // panel wants big-endian RGB565
  lcd()->pushImage(area->x1, area->y1, w, h, (lgfx::swap565_t*)px_map);
  lv_display_flush_ready(disp);
}

void UITask::touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
  UITask* self = (UITask*) lv_indev_get_user_data(indev);
  int32_t x, y;
  bool pressed = self->_screen_on && lcd()->getTouch(&x, &y);
  if (self->_swallow_touch) {
    if (!pressed) self->_swallow_touch = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (pressed) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    self->_last_activity = millis();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static uint32_t lvTick() { return millis(); }

void UITask::initLvgl() {
  lv_init();
  lv_tick_set_cb(lvTick);

  size_t buf_bytes = LCD_W * DRAW_BUF_LINES * sizeof(uint16_t);
  void* buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = NULL;   // single buffer: flushes are synchronous anyway
  if (buf1 == NULL) buf1 = ps_malloc(buf_bytes);

  _lv_disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_flush_cb(_lv_disp, flushCb);
  lv_display_set_buffers(_lv_disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  _lv_touch = lv_indev_create();
  lv_indev_set_type(_lv_touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(_lv_touch, touchReadCb);
  lv_indev_set_user_data(_lv_touch, this);

  lv_theme_t* th = lv_theme_default_init(_lv_disp, lv_color_hex(ACCENT_COLORS[prefs.accent % NUM_ACCENTS]),
                                         lv_color_hex(0x444444), true, &lv_font_montserrat_16);
  lv_display_set_theme(_lv_disp, th);
  _lvgl_ready = true;
}

// ---------------------------------------------------------------- prefs

void UITask::loadPrefs() {
  memset(&prefs, 0, sizeof(prefs));
  File f = SPIFFS.open(PREFS_FILE, "r");
  if (f) {
    f.read((uint8_t*)&prefs, sizeof(prefs));
    f.close();
  }
  if (prefs.magic == PREFS_MAGIC && prefs.version == 1) {   // v1 -> v2: new fields were reserved (zero)
    prefs.version = 2;
    prefs.raise_to_wake = 1;
  }
  if (prefs.magic != PREFS_MAGIC || prefs.version != PREFS_VERSION) {
    memset(&prefs, 0, sizeof(prefs));
    prefs.magic = PREFS_MAGIC;
    prefs.version = PREFS_VERSION;
    prefs.brightness = 160;
    prefs.timeout_idx = 1;   // 15s
    prefs.face = 0;
    prefs.use_24h = 1;
    prefs.vibrate = 1;
    prefs.wake_on_msg = 1;
    prefs.notify_channels = 0;
    prefs.tz_minutes = 0;
    prefs.accent = 0;
    prefs.raise_to_wake = 1;
  }
  if (prefs.brightness < 16) prefs.brightness = 16;
  if (prefs.timeout_idx >= NUM_SCREEN_TIMEOUTS) prefs.timeout_idx = 1;
}

void UITask::savePrefs() {
  File f = SPIFFS.open(PREFS_FILE, "w");
  if (f) {
    f.write((const uint8_t*)&prefs, sizeof(prefs));
    f.close();
  }
}

// ---------------------------------------------------------------- lifecycle

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;

  loadPrefs();
  store.begin(SPIFFS);

#ifdef HAS_DRV2605
  _vibration.begin();
#endif
  _motion.begin(Wire);
  MWDBG("[mw] motion sensor %s\n", _motion.isReady() ? "ok" : "not found");

  MWDBG("[mw] begin, reset reason=%d stack free=%u\n", (int)esp_reset_reason(), (unsigned)uxTaskGetStackHighWaterMark(NULL));
  if (_display == NULL) return;

  setCpuFrequencyMhz(CPU_MHZ_ACTIVE);
  _display->turnOn();
  initLvgl();
  applyBrightness();
  MWDBG("[mw] lvgl ready, free internal=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  screens_create();
  MWDBG("[mw] screens created\n");
  _last_activity = millis();
}

void UITask::applyBrightness() {
  if (_screen_on) lcd()->setBrightness(prefs.brightness);
}

// Raw ST7789 SLPIN/SLPOUT. LovyanGFX's sleep() also sleeps the touch
// controller, which would make tap-to-wake impossible.
void UITask::panelSleep(bool sleep) {
  lgfx::LGFX_Device* d = lcd();
  d->startWrite();
  d->writeCommand(sleep ? 0x10 : 0x11);
  d->endWrite();
  if (!sleep) delay(5);   // ST7789 needs 5 ms after SLPOUT before new commands
}

void UITask::screenOn() {
  MWDBG("[mw] screenOn (was %d)\n", _screen_on);
  if (_screen_on || _display == NULL) return;
  setCpuFrequencyMhz(CPU_MHZ_ACTIVE);
  panelSleep(false);
  _screen_on = true;
  screens_refresh_status();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(_lv_disp);
  lcd()->setBrightness(prefs.brightness);
  _last_activity = millis();
}

void UITask::screenOff() {
  MWDBG("[mw] screenOff (was %d)\n", _screen_on);
  if (!_screen_on || _display == NULL) return;
  lcd()->setBrightness(0);
  panelSleep(true);
  _screen_on = false;
  screens_on_screen_off();
  setCpuFrequencyMhz(CPU_MHZ_IDLE);
}

void UITask::buzz(bool force) {
#ifdef HAS_DRV2605
  _vibration.trigger(force);
#endif
}

uint32_t UITask::now() { return rtc_clock.getCurrentTime(); }

bool UITask::timeValid() { return now() > 1735689600UL; }   // after 2025-01-01

void UITask::localTime(struct tm* out) {
  time_t t = (time_t)now() + (int32_t)prefs.tz_minutes * 60;
  gmtime_r(&t, out);
}

void UITask::loop() {
  store.loop();
  checkPendingSends();
  _motion.loop();
#ifdef MW_DEBUG
  static unsigned long stat_next = 10000;
  if ((long)(millis() - stat_next) >= 0) {
    stat_next = millis() + 10000;
    uint8_t r[3];
    const uint8_t regs[3] = {0x00, 0x01, 0x18};   // PMU status 1/2, charger ctrl
    for (int i = 0; i < 3; i++) {
      Wire.beginTransmission(0x34); Wire.write(regs[i]); Wire.endTransmission(false);
      Wire.requestFrom(0x34, 1); r[i] = Wire.available() ? Wire.read() : 0xEE;
    }
    MWDBG("[mw] pmu sts1=%02x sts2=%02x chgctl=%02x\n", r[0], r[1], r[2]);
    MWDBG("[mw] stat radio %.3f/%.1f/SF%d rx flood=%u direct=%u tx flood=%u direct=%u | batt %umV %d%% chg=%d vbus=%d | contacts=%d\n",
          _node_prefs->freq, _node_prefs->bw, _node_prefs->sf, (unsigned)the_mesh.getNumRecvFlood(), (unsigned)the_mesh.getNumRecvDirect(),
          (unsigned)the_mesh.getNumSentFlood(), (unsigned)the_mesh.getNumSentDirect(), (unsigned)board.getBattMilliVolts(),
          board.getBattPercent(), board.isCharging(), board.isVbusIn(), the_mesh.getNumContacts());
  }
#endif
  if (_display == NULL || !_lvgl_ready) return;

  // crown (AXP2101 power key)
  PowerKeyEvent key = board.pollPowerKey();
  if (key != PowerKeyEvent::none) MWDBG("[mw] crown %d\n", (int)key);
  if (key == PowerKeyEvent::short_press) {
    if (_screen_on) {
      screens_go_home();
      screenOff();
    } else {
      screenOn();
    }
  }

  if (_motion.takeRaise() && !_screen_on && prefs.raise_to_wake) {
    MWDBG("[mw] wrist raise\n");
    screenOn();
  }

  if (!_screen_on) {
    // tap to wake; the waking touch is not passed on to LVGL
    if ((long)(millis() - _next_touch_poll) >= 0) {
      _next_touch_poll = millis() + 120;
      int32_t x, y;
      if (lcd()->getTouch(&x, &y)) {
        _swallow_touch = true;
        screenOn();
      }
    }
    return;
  }

  if ((long)(millis() - _next_status_refresh) >= 0) {
    _next_status_refresh = millis() + 1000;
    screens_refresh_status();
  }

  if ((long)(millis() - _next_lv_tick) >= 0) {
    uint32_t wait = lv_timer_handler();
    if (wait > 20) wait = 20;
    _next_lv_tick = millis() + wait;
  }

  if (millis() - _last_activity > (unsigned long)SCREEN_TIMEOUTS[prefs.timeout_idx] * 1000UL
      && !screens_keep_awake()) {
    screens_go_home();
    screenOff();
  }
}

// ---------------------------------------------------------------- mesh events

void UITask::notify(UIEventType t) {
  // Message alerts are driven by onContactMsg/onChannelMsg; other events just refresh lists.
  if (t == UIEventType::newContactMessage && _lvgl_ready) screens_on_contacts_changed();
}

void UITask::alertIncoming(const char* title, const char* text, const ConvKey& conv) {
  if (!_lvgl_ready) return;
  bool viewing = _screen_on && screens_is_viewing(conv);
  screens_on_message(conv);
  if (viewing) {
    store.markRead(conv);
    _last_activity = millis();
    return;
  }
  if (prefs.vibrate) buzz(true);
  if (!_screen_on && prefs.wake_on_msg) screenOn();
  if (_screen_on) {
    screens_show_notification(title, text, conv);
    _last_activity = millis();
  }
}

void UITask::onContactMsg(const ContactInfo& from, uint8_t path_len, uint32_t sender_timestamp, const char* text, float snr) {
  MWDBG("[mw] rx dm from %s path=%02x snr=%.1f: %s\n", from.name, path_len, snr, text);
  StoredMsg& m = store.add();
  m.ts = timeValid() ? now() : sender_timestamp;
  m.kind = MSG_KIND_DIRECT;
  m.flags = MSG_FLAG_UNREAD;
  m.status = MSG_STATUS_NONE;
  m.hops = path_len;
  m.snr4 = (int8_t)constrain(snr * 4, -128, 127);
  memcpy(m.key, from.id.pub_key, 6);
  StrHelper::strncpy(m.sender, from.name, sizeof(m.sender));
  StrHelper::strncpy(m.text, text, sizeof(m.text));

  alertIncoming(m.sender, m.text, ConvKey::direct(m.key));
}

void UITask::onChannelMsg(uint8_t channel_idx, const char* channel_name, uint8_t path_len, uint32_t timestamp, const char* text, float snr) {
  MWDBG("[mw] rx ch%u %s path=%02x snr=%.1f: %s\n", channel_idx, channel_name, path_len, snr, text);
  StoredMsg& m = store.add();
  m.ts = timeValid() ? now() : timestamp;
  m.kind = MSG_KIND_CHANNEL;
  m.flags = MSG_FLAG_UNREAD;
  m.status = MSG_STATUS_NONE;
  m.hops = path_len;
  m.snr4 = (int8_t)constrain(snr * 4, -128, 127);
  m.chan = channel_idx;

  // group messages arrive as "sender: text"
  const char* sep = strstr(text, ": ");
  if (sep && sep - text < (int)sizeof(m.sender)) {
    memcpy(m.sender, text, sep - text);
    m.sender[sep - text] = 0;
    StrHelper::strncpy(m.text, sep + 2, sizeof(m.text));
  } else {
    m.sender[0] = 0;
    StrHelper::strncpy(m.text, text, sizeof(m.text));
  }

  ConvKey k = ConvKey::channel(channel_idx);
  if (prefs.notify_channels) {
    alertIncoming(channel_name, text, k);
  } else if (_lvgl_ready) {
    screens_on_message(k);
    if (_screen_on && screens_is_viewing(k)) store.markRead(k);
  }
}

void UITask::onAckRecv(uint32_t ack_crc) {
  MWDBG("[mw] ack %08x\n", (unsigned)ack_crc);
  for (int i = 0; i < MAX_PENDING_SENDS; i++) {
    PendingSend& p = _pending[i];
    if (p.ack != 0 && p.ack == ack_crc) {
      p.msg->status = MSG_STATUS_DELIVERED;
      p.ack = 0;
      store.markDirty();
      if (_lvgl_ready) screens_on_message(ConvKey::direct(p.msg->key));
      return;
    }
  }
}

void UITask::onAppSentDirect(const ContactInfo& to, const char* text, uint8_t attempt, uint32_t expected_ack) {
  if (attempt > 0) {
    // the app is retrying: re-arm the existing entry instead of storing a duplicate
    for (int i = store.count() - 1; i >= 0; i--) {
      StoredMsg& old = store.at(i);
      if ((old.flags & MSG_FLAG_OUTGOING) && old.kind == MSG_KIND_DIRECT && memcmp(old.key, to.id.pub_key, 6) == 0
          && strncmp(old.text, text, sizeof(old.text) - 1) == 0) {
        for (int j = 0; j < MAX_PENDING_SENDS; j++) {
          if (_pending[j].msg == &old) _pending[j].ack = 0;
        }
        old.ack = expected_ack;
        old.status = expected_ack ? MSG_STATUS_SENDING : MSG_STATUS_SENT;
        if (expected_ack) armAppPending(&old, expected_ack);
        store.markDirty();
        if (_lvgl_ready) screens_on_message(ConvKey::direct(old.key));
        return;
      }
    }
  }
  StoredMsg& m = store.add();
  m.ts = now();
  m.kind = MSG_KIND_DIRECT;
  m.flags = MSG_FLAG_OUTGOING;
  m.status = expected_ack ? MSG_STATUS_SENDING : MSG_STATUS_SENT;
  m.ack = expected_ack;
  memcpy(m.key, to.id.pub_key, 6);
  StrHelper::strncpy(m.sender, _node_prefs->node_name, sizeof(m.sender));
  StrHelper::strncpy(m.text, text, sizeof(m.text));
  if (expected_ack) armAppPending(&m, expected_ack);
  if (_lvgl_ready) screens_on_message(ConvKey::direct(m.key));
}

// Tracks the ACK for an app-sent message. No local retries: the app manages
// its own, so attempt starts at the limit and a timeout just marks it failed.
void UITask::armAppPending(StoredMsg* m, uint32_t expected_ack) {
  int slot = 0;
  for (int i = 0; i < MAX_PENDING_SENDS; i++) {
    if (_pending[i].ack == 0) { slot = i; break; }
    if (_pending[i].deadline < _pending[slot].deadline) slot = i;
  }
  _pending[slot] = { expected_ack, millis() + 30000, 0, MAX_SEND_ATTEMPTS, m };
}

void UITask::onAppSentChannel(uint8_t channel_idx, const char* text, int len) {
  StoredMsg& m = store.add();
  m.ts = now();
  m.kind = MSG_KIND_CHANNEL;
  m.flags = MSG_FLAG_OUTGOING;
  m.status = MSG_STATUS_SENT;
  m.chan = channel_idx;
  StrHelper::strncpy(m.sender, _node_prefs->node_name, sizeof(m.sender));
  int n = min(len, (int)sizeof(m.text) - 1);
  memcpy(m.text, text, n);
  m.text[n] = 0;
  if (_lvgl_ready) screens_on_message(ConvKey::channel(channel_idx));
}

// ---------------------------------------------------------------- sending

bool UITask::sendDirect(const uint8_t* key_prefix, const char* text) {
  StoredMsg& m = store.add();
  m.ts = now();
  m.kind = MSG_KIND_DIRECT;
  m.flags = MSG_FLAG_OUTGOING;
  m.hops = 0;
  memcpy(m.key, key_prefix, 6);
  StrHelper::strncpy(m.sender, _node_prefs->node_name, sizeof(m.sender));
  StrHelper::strncpy(m.text, text, sizeof(m.text));

  uint32_t ts = rtc_clock.getCurrentTimeUnique();
  uint32_t expected_ack = 0, est_timeout = 0;
  int result = the_mesh.uiSendText(key_prefix, ts, 0, m.text, expected_ack, est_timeout);
  MWDBG("[mw] tx dm result=%d ack=%08x timeout=%ums\n", result, (unsigned)expected_ack, (unsigned)est_timeout);
  if (result == MSG_SEND_FAILED) {
    m.status = MSG_STATUS_FAILED;
    return false;
  }
  m.ack = expected_ack;
  m.status = expected_ack ? MSG_STATUS_SENDING : MSG_STATUS_SENT;

  if (expected_ack) {
    int slot = 0;
    for (int i = 0; i < MAX_PENDING_SENDS; i++) {
      if (_pending[i].ack == 0) { slot = i; break; }
      if (_pending[i].deadline < _pending[slot].deadline) slot = i;   // evict the soonest-to-expire
    }
    if (_pending[slot].ack != 0) _pending[slot].msg->status = MSG_STATUS_FAILED;
    _pending[slot] = { expected_ack, millis() + est_timeout, ts, 0, &m };
  }
  return true;
}

void UITask::checkPendingSends() {
  for (int i = 0; i < MAX_PENDING_SENDS; i++) {
    PendingSend& p = _pending[i];
    if (p.ack == 0 || (long)(millis() - p.deadline) < 0) continue;

    // the message may have been evicted from the ring meanwhile
    if (p.msg->ack != p.ack) { p.ack = 0; continue; }

    if (p.attempt + 1 < MAX_SEND_ATTEMPTS) {
      p.attempt++;
      if (p.attempt == MAX_SEND_ATTEMPTS - 1) {
        the_mesh.uiResetPath(p.msg->key);   // last try: fall back to flood routing
      }
      uint32_t expected_ack = 0, est_timeout = 0;
      int result = the_mesh.uiSendText(p.msg->key, p.timestamp, p.attempt, p.msg->text, expected_ack, est_timeout);
      MWDBG("[mw] retry %u result=%d ack=%08x\n", p.attempt, result, (unsigned)expected_ack);
      if (result != MSG_SEND_FAILED && expected_ack) {
        p.ack = expected_ack;
        p.msg->ack = expected_ack;
        p.deadline = millis() + est_timeout;
        continue;
      }
    }
    p.msg->status = MSG_STATUS_FAILED;
    p.ack = 0;
    store.markDirty();
    if (_lvgl_ready) screens_on_message(ConvKey::direct(p.msg->key));
  }
}

bool UITask::sendChannel(uint8_t channel_idx, const char* text) {
  StoredMsg& m = store.add();
  m.ts = now();
  m.kind = MSG_KIND_CHANNEL;
  m.flags = MSG_FLAG_OUTGOING;
  m.chan = channel_idx;
  StrHelper::strncpy(m.sender, _node_prefs->node_name, sizeof(m.sender));
  StrHelper::strncpy(m.text, text, sizeof(m.text));
  bool ok = the_mesh.uiSendChannelText(channel_idx, m.text);
  MWDBG("[mw] tx ch%u ok=%d\n", channel_idx, ok);
  m.status = ok ? MSG_STATUS_SENT : MSG_STATUS_FAILED;
  return ok;
}
