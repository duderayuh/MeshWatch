// T-Watch S3 Plus hardware bring-up test.
// Logs every step over USB CDC so a black screen can be narrowed down.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <XPowersLib.h>
#include <RadioLib.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_Watch : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
  lgfx::Touch_FT5x06 _touch;
public:
  LGFX_Watch() {
    { auto c = _bus.config();
      c.spi_host = SPI3_HOST; c.spi_mode = 0; c.freq_write = 40000000; c.freq_read = 16000000;
      c.spi_3wire = true; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 18; c.pin_mosi = 13; c.pin_miso = -1; c.pin_dc = 38;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = 12; c.pin_rst = -1; c.pin_busy = -1;
      c.memory_width = 240; c.memory_height = 320; c.panel_width = 240; c.panel_height = 240;
      c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 2;
      c.readable = false; c.invert = true; c.rgb_order = false; c.dlen_16bit = false; c.bus_shared = false;
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = 45; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config();
      c.x_min = 0; c.x_max = 239; c.y_min = 0; c.y_max = 239;
      c.pin_int = 16; c.pin_rst = -1; c.bus_shared = false; c.offset_rotation = 2;
      c.i2c_port = 1; c.i2c_addr = 0x38; c.pin_sda = 39; c.pin_scl = 40; c.freq = 400000;
      _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};

static LGFX_Watch lcd;
static XPowersAXP2101 pmu;
static SPIClass loraSpi(FSPI);
static SX1262 radio = new Module(5, 9, 8, 7, loraSpi);

static void scan(TwoWire& w, const char* name) {
  Serial.printf("I2C scan %s:", name);
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) Serial.printf(" 0x%02X", a);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 4000) delay(10);
  delay(500);
  Serial.println("\n=== T-Watch S3 Plus hwtest ===");
  Serial.printf("PSRAM: %u bytes, flash %u\n", ESP.getPsramSize(), ESP.getFlashChipSize());

  Wire.begin(10, 11);
  scan(Wire, "Wire(10,11)");

  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 10, 11)) {
    Serial.println("AXP2101: NOT FOUND");
  } else {
    Serial.println("AXP2101: ok");
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();   // backlight
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();   // display + touch
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();   // radio
    pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();   // GPS (newer rev)
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();   // haptic
    pmu.setDC3Voltage(3300);   pmu.enableDC3();     // GPS (older rev)
    pmu.enableBattVoltageMeasure();
    delay(200);
    Serial.printf("rails: ALDO2=%d ALDO3=%d ALDO4=%d BLDO1=%d BLDO2=%d DC3=%d  batt=%dmV vbus=%d\n",
      pmu.isEnableALDO2(), pmu.isEnableALDO3(), pmu.isEnableALDO4(), pmu.isEnableBLDO1(),
      pmu.isEnableBLDO2(), pmu.isEnableDC3(), pmu.getBattVoltage(), pmu.isVbusIn());
  }
  scan(Wire, "Wire(10,11) after rails");

  Serial.println("LCD init...");
  bool ok = lcd.init();
  Serial.printf("LCD init -> %d, w=%d h=%d\n", ok, lcd.width(), lcd.height());
  lcd.setBrightness(200);
  const uint32_t cols[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char* names[] = {"RED", "GREEN", "BLUE", "WHITE"};
  for (int i = 0; i < 4; i++) {
    lcd.fillScreen(cols[i]);
    Serial.printf("fill %s\n", names[i]);
    delay(600);
  }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.drawString("TOP-LEFT", 4, 4);
  lcd.drawString("hwtest", 80, 110);
  lcd.fillCircle(230, 230, 8, TFT_RED);

  Wire1.begin(39, 40);
  scan(Wire1, "Wire1(39,40)");

  loraSpi.begin(3, 4, 1, 5);
  int st = radio.begin(915.0, 250.0, 10, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 16, 1.8, false);
  Serial.printf("SX1262 begin(tcxo 1.8) -> %d\n", st);
  if (st != RADIOLIB_ERR_NONE) {
    st = radio.begin(915.0, 250.0, 10, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 16, 0, false);
    Serial.printf("SX1262 begin(no tcxo) -> %d\n", st);
  }

  Serial1.begin(38400, SERIAL_8N1, 41, 42);
  Serial.println("setup done; touch the screen, GPS bytes follow");
}

void loop() {
  static uint32_t last = 0, gpsBytes = 0;
  while (Serial1.available()) { Serial1.read(); gpsBytes++; }
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    Serial.printf("touch %d,%d\n", x, y);
    lcd.fillCircle(x, y, 4, TFT_GREEN);
  }
  if (millis() - last > 3000) {
    last = millis();
    Serial.printf("alive %lus gps_bytes=%u batt=%dmV\n", millis() / 1000, gpsBytes, pmu.getBattVoltage());
  }
  delay(20);
}
