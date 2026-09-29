#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/ContactInfo.h>
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    newContactMessage,
    ack
};

class AbstractUITask {
protected:
  mesh::MainBoard* _board;
  MultiSerialInterface* _interfaceManager;
  bool _connected;

  AbstractUITask(mesh::MainBoard* board, MultiSerialInterface* interfaceManager) : _board(board), _interfaceManager(interfaceManager) {
    _connected = false;
  }

public:
  void setHasConnection(bool connected) { _connected = connected; }
  bool hasConnection() const { return _connected; }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isBluetoothEnabled() const { return _interfaceManager->isBluetoothEnabled(); }
  void enableBluetooth() { _interfaceManager->enableBluetooth(); }
  void disableBluetooth() { _interfaceManager->disableBluetooth(); }
  virtual void msgRead(int msgcount) = 0;
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) = 0;
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  virtual void loop() = 0;

  // Richer events for UIs that keep their own message history (all optional).
  // path_len is 0xFF for direct-routed packets, snr is in dB.
  virtual void onContactMsg(const ContactInfo& from, uint8_t path_len, uint32_t sender_timestamp, const char* text, float snr) { }
  virtual void onChannelMsg(uint8_t channel_idx, const char* channel_name, uint8_t path_len, uint32_t timestamp, const char* text, float snr) { }
  virtual void onAckRecv(uint32_t ack_crc) { }
  // Messages the connected app sent through this radio (so an on-device history stays complete).
  virtual void onAppSentDirect(const ContactInfo& to, const char* text, uint8_t attempt, uint32_t expected_ack) { }
  virtual void onAppSentChannel(uint8_t channel_idx, const char* text, int len) { }
};
