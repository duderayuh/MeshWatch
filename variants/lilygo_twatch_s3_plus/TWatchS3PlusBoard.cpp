#include <Arduino.h>
#include "TWatchS3PlusBoard.h"

void TWatchS3PlusBoard::begin() {
  ESP32Board::begin();
  power_init();

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

bool TWatchS3PlusBoard::power_init() {
  axp = new XPowersAXP2101(Wire, PIN_BOARD_SDA, PIN_BOARD_SCL, I2C_PMU_ADD);
  PMU = axp;
  if (!PMU->init()) {
    MESH_DEBUG_PRINTLN("Warning: Failed to find AXP2101 power management");
    delete axp;
    PMU = NULL;
    axp = NULL;
    return false;
  }

  axp->setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

  // Power rails per the T-Watch S3 Plus PowerManage table:
  //   ALDO2 = display backlight, ALDO3 = display + touch, ALDO4 = LoRa,
  //   BLDO2 = DRV2605. The GNSS is on BLDO1 (boards with BOOT/RST keys on
  //   the case) or DCDC3 (earlier boards), so both are enabled like LilyGoLib.
  PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);  // LoRa radio
  PMU->enablePowerOutput(XPOWERS_ALDO4);
  PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);  // display + touch
  PMU->enablePowerOutput(XPOWERS_ALDO3);
  PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);  // display backlight
  PMU->enablePowerOutput(XPOWERS_ALDO2);
  PMU->setPowerChannelVoltage(XPOWERS_BLDO2, 3300);  // DRV2605 haptic
  PMU->enablePowerOutput(XPOWERS_BLDO2);
  PMU->setPowerChannelVoltage(XPOWERS_BLDO1, 3300);  // GNSS (newer boards)
  PMU->enablePowerOutput(XPOWERS_BLDO1);
  PMU->setPowerChannelVoltage(XPOWERS_DCDC3, 3300);  // GNSS (earlier boards)
  PMU->enablePowerOutput(XPOWERS_DCDC3);

  PMU->disablePowerOutput(XPOWERS_DCDC2);
  PMU->disablePowerOutput(XPOWERS_DCDC4);
  PMU->disablePowerOutput(XPOWERS_DCDC5);
  PMU->disablePowerOutput(XPOWERS_ALDO1);    // unused on the Plus
  PMU->disablePowerOutput(XPOWERS_DLDO1);
  PMU->disablePowerOutput(XPOWERS_DLDO2);
  PMU->disablePowerOutput(XPOWERS_VBACKUP);

  // Only the crown (power key) presses raise the IRQ line.
  PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  PMU->clearIrqStatus();
  PMU->enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
  pinMode(PIN_PMU_IRQ, INPUT_PULLUP);

  axp->setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
  axp->setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);
  axp->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
  axp->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

  // No thermistor on the watch; an enabled TS check would block charging.
  PMU->disableTSPinMeasure();
  axp->enableBattDetection();
  axp->enableCellbatteryCharge();   // main Li-ion charger (REG18 bit1)
  axp->enableGauge();               // fuel gauge for battery %
  PMU->enableSystemVoltageMeasure();
  PMU->enableVbusVoltageMeasure();
  PMU->enableBattVoltageMeasure();

  axp->setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
  return true;
}

PowerKeyEvent TWatchS3PlusBoard::pollPowerKey() {
  // The IRQ line doesn't reliably assert on these boards, so poll the status
  // register over I2C, rate-limited.
  static unsigned long next_poll = 0;
  if (PMU == NULL || (long)(millis() - next_poll) < 0) return PowerKeyEvent::none;
  next_poll = millis() + 100;

  PMU->getIrqStatus();
  PowerKeyEvent ev = PowerKeyEvent::none;
  if (PMU->isPekeyShortPressIrq()) ev = PowerKeyEvent::short_press;
  else if (PMU->isPekeyLongPressIrq()) ev = PowerKeyEvent::long_press;
  PMU->clearIrqStatus();
  return ev;
}
