#include "Motion.h"
#include <math.h>

#define MOTION_POLL_MILLIS 50
#define BMA423_ADDR        0x19

// The BMA423 reads z = -1 g with the watch lying face up (measured on hardware).
#ifndef MOTION_FACE_SIGN
  #define MOTION_FACE_SIGN   -1
#endif
// Thresholds from recorded wrist raises: arm hanging reads y ~ +1 g; looking
// at the watch reads z ~ -0.6..-0.85 g with the wrist rolled (x ~ +0.5).
#define LOOK_UP_MIN        0.45f    // screen tilted >= ~27 deg toward up
#define LOOK_Y_MAX         0.35f    // band no longer pointing down
#define AWAY_UP_MAX        0.30f    // screen turned away
#define AWAY_Y_MIN         0.70f    // arm hanging
#define RAISE_WINDOW_MS    1500     // away -> look must happen this quickly
#define LOOK_HOLD_MS       200      // ...and be held this long (ignores arm swings)

bool Motion::begin(TwoWire& wire) {
#ifdef HAS_BMA423
  if (!_sensor.begin(wire, BMA423_ADDR)) return false;   // absent, or a BMA456 on newer boards
  _sensor.configAccelerometer(_sensor.RANGE_2G, _sensor.ODR_50HZ, _sensor.BW_NORMAL_AVG4,
                              _sensor.PERF_CONTINUOUS_MODE);
  _sensor.enableAccelerometer();
  _ok = true;
#endif
  return _ok;
}

void Motion::loop() {
#ifdef HAS_BMA423
  if (!_ok || (long)(millis() - _next_poll) < 0) return;
  _next_poll = millis() + MOTION_POLL_MILLIS;

  int16_t x, y, z;
  if (!_sensor.getAccelerometer(x, y, z)) return;
  float mag = sqrtf((float)x * x + (float)y * y + (float)z * z);
  if (mag < 1.0f) return;
  float up = MOTION_FACE_SIGN * z / mag;

  float ny = y / mag;
  unsigned long now = millis();
  if (up < AWAY_UP_MAX || ny > AWAY_Y_MIN) _last_away = now;

  bool look = up > LOOK_UP_MIN && ny < LOOK_Y_MAX;
  if (!look) {
    _look_since = 0;
    _fired = false;
  } else if (_look_since == 0) {
    _look_since = now;
  } else if (!_fired && now - _look_since >= LOOK_HOLD_MS && _last_away != 0
             && _look_since - _last_away < RAISE_WINDOW_MS) {
    _raised = true;
    _fired = true;   // once per look
  }
#endif
}
