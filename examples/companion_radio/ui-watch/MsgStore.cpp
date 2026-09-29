#include "MsgStore.h"
#include <esp32-hal-psram.h>

#define MSGSTORE_FILE     "/mw_msgs"
#define MSGSTORE_MAGIC    0x4D57534DUL   // 'MWSM'
#define MSGSTORE_VERSION  1
#define SAVE_DELAY_MILLIS 8000

struct StoreHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t rec_size;
  uint16_t count;
  uint16_t reserved;
};

bool MsgStore::begin(fs::FS& fs) {
  _fs = &fs;
  _msgs = (StoredMsg*) ps_calloc(MSGSTORE_CAPACITY, sizeof(StoredMsg));
  if (_msgs == NULL) _msgs = (StoredMsg*) calloc(MSGSTORE_CAPACITY, sizeof(StoredMsg));
  if (_msgs == NULL) return false;

  File f = _fs->open(MSGSTORE_FILE, "r");
  if (!f) return true;

  StoreHeader hdr;
  if (f.read((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr) && hdr.magic == MSGSTORE_MAGIC
      && hdr.version == MSGSTORE_VERSION && hdr.rec_size == sizeof(StoredMsg)) {
    int n = min((int)hdr.count, MSGSTORE_CAPACITY);
    for (int i = 0; i < n; i++) {
      if (f.read((uint8_t*)&_msgs[i], sizeof(StoredMsg)) != sizeof(StoredMsg)) break;
      _msgs[i].text[sizeof(_msgs[i].text) - 1] = 0;
      _msgs[i].sender[sizeof(_msgs[i].sender) - 1] = 0;
      if (_msgs[i].status == MSG_STATUS_SENDING) _msgs[i].status = MSG_STATUS_FAILED;  // retries didn't survive reboot
      _count++;
    }
    _head = _count % MSGSTORE_CAPACITY;
  }
  f.close();
  return true;
}

void MsgStore::saveNow() {
  if (_fs == NULL || _msgs == NULL) return;
  File f = _fs->open(MSGSTORE_FILE, "w");
  if (f) {
    StoreHeader hdr = { MSGSTORE_MAGIC, MSGSTORE_VERSION, sizeof(StoredMsg), (uint16_t)_count, 0 };
    f.write((const uint8_t*)&hdr, sizeof(hdr));
    for (int i = 0; i < _count; i++) {   // written oldest-first, so a reload is linear
      f.write((const uint8_t*)&at(i), sizeof(StoredMsg));
    }
    f.close();
  }
  _dirty = false;
}

void MsgStore::loop() {
  if (_dirty && (long)(millis() - _save_due) >= 0) saveNow();
}

void MsgStore::markDirty() {
  if (!_dirty) {
    _dirty = true;
    _save_due = millis() + SAVE_DELAY_MILLIS;
  }
}

StoredMsg& MsgStore::add() {
  StoredMsg& m = _msgs[_head];
  memset(&m, 0, sizeof(m));
  _head = (_head + 1) % MSGSTORE_CAPACITY;
  if (_count < MSGSTORE_CAPACITY) _count++;
  markDirty();
  return m;
}

int MsgStore::totalUnread() {
  int n = 0;
  for (int i = 0; i < _count; i++) {
    if (at(i).flags & MSG_FLAG_UNREAD) n++;
  }
  return n;
}

void MsgStore::markRead(const ConvKey& k) {
  bool changed = false;
  for (int i = 0; i < _count; i++) {
    StoredMsg& m = at(i);
    if ((m.flags & MSG_FLAG_UNREAD) && k.matches(m)) {
      m.flags &= ~MSG_FLAG_UNREAD;
      changed = true;
    }
  }
  if (changed) markDirty();
}

StoredMsg* MsgStore::findByAck(uint32_t ack) {
  for (int i = _count - 1; i >= 0; i--) {
    StoredMsg& m = at(i);
    if ((m.flags & MSG_FLAG_OUTGOING) && m.ack == ack) return &m;
  }
  return NULL;
}

int MsgStore::listConversations(ConvSummary out[], int max) {
  int n = 0;
  for (int i = _count - 1; i >= 0; i--) {
    StoredMsg& m = at(i);
    int j;
    for (j = 0; j < n; j++) {
      if (out[j].key.matches(m)) break;
    }
    if (j == n) {
      if (n >= max) continue;   // still need to count unread for listed ones
      out[n].key.kind = m.kind;
      out[n].key.chan = m.chan;
      memcpy(out[n].key.key, m.key, 6);
      out[n].last_idx = i;
      out[n].unread = 0;
      n++;
    }
    if (m.flags & MSG_FLAG_UNREAD) out[j].unread++;
  }
  return n;
}

void MsgStore::clearConversation(const ConvKey& k) {
  // compact into a linear copy, keeping order (in-place would clobber a wrapped ring)
  StoredMsg* tmp = (StoredMsg*) ps_calloc(MSGSTORE_CAPACITY, sizeof(StoredMsg));
  if (tmp == NULL) return;
  int w = 0;
  for (int i = 0; i < _count; i++) {
    if (!k.matches(at(i))) tmp[w++] = at(i);
  }
  memcpy(_msgs, tmp, sizeof(StoredMsg) * MSGSTORE_CAPACITY);
  free(tmp);
  _count = w;
  _head = w % MSGSTORE_CAPACITY;
  markDirty();
}
