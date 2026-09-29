#pragma once

#include <Arduino.h>
#include <FS.h>

// On-watch chat history. A fixed ring of messages lives in PSRAM and is
// written to flash lazily, so conversations survive reboots.

#ifndef MSGSTORE_CAPACITY
  #define MSGSTORE_CAPACITY 256
#endif

#define MSG_KIND_DIRECT   0
#define MSG_KIND_CHANNEL  1

#define MSG_FLAG_OUTGOING 0x01
#define MSG_FLAG_UNREAD   0x02

enum MsgStatus : uint8_t {
  MSG_STATUS_NONE = 0,     // received message
  MSG_STATUS_SENDING,      // direct message waiting for ACK
  MSG_STATUS_DELIVERED,
  MSG_STATUS_FAILED,
  MSG_STATUS_SENT,         // channel message (no ACKs on channels)
};

struct StoredMsg {
  uint32_t ts;          // our clock (epoch secs)
  uint32_t ack;         // expected ACK for outgoing direct messages
  uint8_t  kind;        // MSG_KIND_*
  uint8_t  flags;       // MSG_FLAG_*
  uint8_t  status;      // MsgStatus
  uint8_t  hops;        // 0xFF = direct route
  int8_t   snr4;        // SNR * 4
  uint8_t  chan;        // channel index (channel messages)
  uint8_t  key[6];      // contact pub key prefix (direct messages)
  char     sender[24];  // display name of sender (channel: parsed from "name: text")
  char     text[164];
};

// Identifies one conversation: a channel or a contact.
struct ConvKey {
  uint8_t kind;
  uint8_t chan;
  uint8_t key[6];

  bool matches(const StoredMsg& m) const {
    if (m.kind != kind) return false;
    return kind == MSG_KIND_CHANNEL ? m.chan == chan : memcmp(m.key, key, 6) == 0;
  }
  bool operator==(const ConvKey& o) const {
    return kind == o.kind && (kind == MSG_KIND_CHANNEL ? chan == o.chan : memcmp(key, o.key, 6) == 0);
  }
  static ConvKey channel(uint8_t idx) { ConvKey k; k.kind = MSG_KIND_CHANNEL; k.chan = idx; memset(k.key, 0, 6); return k; }
  static ConvKey direct(const uint8_t* prefix) { ConvKey k; k.kind = MSG_KIND_DIRECT; k.chan = 0; memcpy(k.key, prefix, 6); return k; }
};

struct ConvSummary {
  ConvKey key;
  int last_idx;         // index of the newest message (for preview)
  int unread;
};

class MsgStore {
  StoredMsg* _msgs = NULL;   // ring buffer, oldest at _head when full
  int _head = 0;             // next write position
  int _count = 0;
  fs::FS* _fs = NULL;
  bool _dirty = false;
  unsigned long _save_due = 0;

public:
  bool begin(fs::FS& fs);
  void loop();               // performs the lazy save
  void saveNow();

  int count() const { return _count; }
  // i = 0 is the oldest message
  StoredMsg& at(int i) { return _msgs[(_head - _count + i + MSGSTORE_CAPACITY) % MSGSTORE_CAPACITY]; }

  StoredMsg& add();          // returns a zeroed slot (evicting the oldest when full)
  void markDirty();

  int  totalUnread();
  void markRead(const ConvKey& k);
  StoredMsg* findByAck(uint32_t ack);

  // Newest-first list of conversations that have messages.
  int listConversations(ConvSummary out[], int max);
  void clearConversation(const ConvKey& k);
};
