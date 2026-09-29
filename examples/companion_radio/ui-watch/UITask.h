#pragma once

// MeshWatch: a native 240x240 touch UI for MeshCore companion radios on
// smartwatch hardware (LilyGo T-Watch S3 Plus). Rendered with LVGL.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include <lvgl.h>

#ifdef HAS_DRV2605
  #include <helpers/ui/DRV2605Vibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "MsgStore.h"
#include "Motion.h"

#define MESHWATCH_VERSION "0.1.0"

// Watch-only preferences (separate file from the companion NodePrefs).
struct WatchPrefs {
  uint32_t magic;
  uint8_t  version;
  uint8_t  brightness;      // 16..255
  uint8_t  timeout_idx;     // index into SCREEN_TIMEOUTS
  uint8_t  face;            // 0 = digital, 1 = analog
  uint8_t  use_24h;
  uint8_t  vibrate;         // vibrate on new message
  uint8_t  wake_on_msg;     // turn the screen on for new messages
  uint8_t  notify_channels; // also alert for channel traffic
  int16_t  tz_minutes;      // offset from UTC
  uint8_t  accent;          // index into accent palette
  uint8_t  raise_to_wake;   // v2: lifting the wrist turns the screen on
  uint8_t  reserved[8];
};

#define NUM_SCREEN_TIMEOUTS 5
extern const uint16_t SCREEN_TIMEOUTS[NUM_SCREEN_TIMEOUTS];   // seconds
#define NUM_ACCENTS 5
extern const uint32_t ACCENT_COLORS[NUM_ACCENTS];

struct PendingSend {
  uint32_t ack;            // 0 = slot free
  unsigned long deadline;
  uint32_t timestamp;      // original msg timestamp (retries reuse it)
  uint8_t attempt;
  StoredMsg* msg;
};
#define MAX_PENDING_SENDS 6
#define MAX_SEND_ATTEMPTS 3

class UITask : public AbstractUITask {
  DisplayDriver* _display = NULL;
  SensorManager* _sensors = NULL;
  NodePrefs* _node_prefs = NULL;
#ifdef HAS_DRV2605
  DRV2605Vibration _vibration;
#endif

  lv_display_t* _lv_disp = NULL;
  lv_indev_t* _lv_touch = NULL;
  bool _screen_on = true;
  bool _swallow_touch = false;     // ignore the touch that woke the screen
  unsigned long _last_activity = 0;
  unsigned long _next_lv_tick = 0;
  unsigned long _next_status_refresh = 0;
  unsigned long _next_touch_poll = 0;
  bool _lvgl_ready = false;

  PendingSend _pending[MAX_PENDING_SENDS];

  Motion _motion;

  void initLvgl();
  void panelSleep(bool sleep);
  void loadPrefs();
  void checkPendingSends();
  void armAppPending(StoredMsg* m, uint32_t expected_ack);
  void alertIncoming(const char* title, const char* text, const ConvKey& conv);

  static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
  static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data);

public:
  WatchPrefs prefs;
  MsgStore store;

  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial) {
    memset(_pending, 0, sizeof(_pending));
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // AbstractUITask
  void msgRead(int msgcount) override { }
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override { }
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;
  void onContactMsg(const ContactInfo& from, uint8_t path_len, uint32_t sender_timestamp, const char* text, float snr) override;
  void onChannelMsg(uint8_t channel_idx, const char* channel_name, uint8_t path_len, uint32_t timestamp, const char* text, float snr) override;
  void onAckRecv(uint32_t ack_crc) override;
  void onAppSentDirect(const ContactInfo& to, const char* text, uint8_t attempt, uint32_t expected_ack) override;
  void onAppSentChannel(uint8_t channel_idx, const char* text, int len) override;

  // Used by the screens
  NodePrefs* nodePrefs() { return _node_prefs; }
  SensorManager* sensors() { return _sensors; }
  void savePrefs();
  void applyBrightness();
  void screenOn();
  void screenOff();
  bool isScreenOn() const { return _screen_on; }
  void touchActivity() { _last_activity = millis(); }
  void buzz(bool force = true);

  bool sendDirect(const uint8_t* key_prefix, const char* text);
  bool sendChannel(uint8_t channel_idx, const char* text);

  bool hasMotion() const { return _motion.isReady(); }

  uint32_t now();                         // UTC epoch secs
  bool timeValid();
  void localTime(struct tm* out);         // now() adjusted for tz
};

extern UITask ui_task;
