#pragma once

// Raise-to-wake from the BMA423's raw accelerometer. (The chip's own
// wrist-tilt feature engine never fired on test hardware, so the gesture is
// detected here: arm hanging or screen away, then quickly held in a looking pose.)

#include <Arduino.h>
#include <Wire.h>

#ifdef HAS_BMA423
  #include <SensorBMA423.hpp>
#endif

class Motion {
#ifdef HAS_BMA423
  SensorBMA423 _sensor;
#endif
  bool _ok = false;
  bool _raised = false;
  bool _fired = false;
  unsigned long _look_since = 0;
  unsigned long _next_poll = 0;
  unsigned long _last_away = 0;   // last time the screen pointed well away from the viewer

public:
  bool begin(TwoWire& wire);
  void loop();                    // call often; samples at 20 Hz
  bool isReady() const { return _ok; }
  bool takeRaise() { bool r = _raised; _raised = false; return r; }   // wrist raised since last call?
};
