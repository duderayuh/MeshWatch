# MeshWatch

An open-source smartwatch firmware for [MeshCore](https://github.com/meshcore-dev/MeshCore) on the
**LilyGo T-Watch S3 Plus**. It is the MeshCore companion radio (so the phone app keeps working over
Bluetooth) with a native 240×240 touch interface, so the watch is also useful on its own.

## Features

- **Watch faces:** digital (with a seconds ring) or analog. Corners show Bluetooth and GPS status,
  battery, unread messages and node name.
- **Chats:** channels and direct messages with message bubbles, hop count and SNR, and delivery
  ticks for DMs. Failed DMs retry automatically (the last try falls back to flood routing) and can be
  resent with a tap. History is stored on the watch and survives reboots.
- **Replying:** 20 quick replies, send-my-GPS-location, or an on-screen keyboard.
- **Contacts:** sorted by last heard, filterable (All / People / Nodes). Each shows hops, age and
  distance. The detail page adds bearing, reset path and share contact.
- **Mesh:** advert (zero-hop or flood), recently heard, GPS fix, radio settings, battery, and the
  Bluetooth pairing PIN.
- **Alerts:** vibration plus a pop-up for new messages (channel alerts are optional).
- **Power:** raise to wake, tap to wake, and the crown toggles the screen. Screen timeout is
  configurable, and the CPU drops to 80 MHz while the screen is off.
- **Settings:** brightness, timeout, face, accent colour, 12/24 h, time zone, vibration, Bluetooth,
  GPS, location sharing, node name, restart and power off.
- **Phone app:** messages you send from the MeshCore app also appear in the watch's history.

## Install

Download `meshwatch-<version>-lilygo-twatch-s3-plus-merged.bin` from the
[Releases](../../releases) page and flash it at offset `0x0`:

```sh
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX write-flash 0x0 meshwatch-*-merged.bin
```

Web flashers that accept a custom `.bin` also work, such as the "Custom firmware" button on
[flasher.meshcore.io](https://flasher.meshcore.io). If the watch doesn't answer, put it in download
mode: hold **BOOT**, tap **RST**, then release **BOOT** (these are the recessed buttons on the case
edge).

**Back up the stock firmware first** if you want to be able to go back:

```sh
esptool --port /dev/cu.usbmodemXXXX read-flash 0 0x1000000 stock_backup.bin
```

## First use

1. The Bluetooth pairing PIN is **random on every boot**. It's shown on the watch face until a
   phone pairs, and it's always shown on the Mesh tile.
2. Pair from the MeshCore app. The app sets the clock and the radio preset for your region. If a
   previous pairing failed, "forget" the device in your phone's Bluetooth settings first.
3. Set your time zone in **Settings**. If GPS is turned on in Settings, the clock is also set from
   GPS once it has a fix.

## Controls

| Action | How |
| --- | --- |
| Screen on / off | Short press on the crown |
| Wake | Raise your wrist, or tap the screen |
| Switch tiles | Swipe left or right: Face, Chats, Contacts, Mesh, Settings |
| Back | The `<` button, or swipe right |
| Force power off | Hold the crown for 6 s |

## Build

```sh
pio run -e LilyGo_TWatchS3Plus_meshwatch                  # build
pio run -e LilyGo_TWatchS3Plus_meshwatch -t upload        # build + flash
```

The board support is in `variants/lilygo_twatch_s3_plus/` and the UI is in
`examples/companion_radio/ui-watch/`. `tools/twatch-s3-plus-hwtest/` is a standalone bring-up sketch
that checks the power rails, I²C, display, touch, radio and GPS.

### Hardware notes

These were found while bringing up the watch and may help anyone porting to it:

- **SPI:** the radio must be on `FSPI`. `SPIClass()` defaults to `HSPI`, which is SPI3 on the
  ESP32-S3, the same host LovyanGFX uses for the display. Sharing it caused crashes and hangs.
- **Orientation:** the display needs a 180° rotation, and the touch controller is mounted 180°
  relative to the panel.
- **Crown:** the AXP2101 IRQ line doesn't reliably assert on key presses, so the key status is
  polled over I²C.
- **Screen sleep:** LovyanGFX's `sleep()` also sleeps the touch controller, which breaks tap to wake,
  so the panel is slept with raw `SLPIN`/`SLPOUT` commands.
- **DMA:** `pushImageDMA` left the panel blank on this board, so flushing is synchronous, at 80 MHz
  SPI with pre-swapped RGB565.
- **Raise to wake:** the BMA423's own wrist-tilt and step-counter features never fired on the test
  unit, so raise to wake is detected from raw accelerometer data. Thresholds come from recorded wrist
  raises.
- **GPS:** the GPS runs at 38400 baud. The LoRa TCXO is 1.8 V.

## Limitations

- Messages sent **from the watch** don't show up in the phone app, because the companion protocol has
  no way to report messages the device sent itself. Received messages still sync to the app as usual.
- Room-server login and maps are not implemented yet.
- Units with a BMA456 accelerometer (newer batches) have no raise to wake yet.
- The battery percentage relies on the AXP2101 detecting the cell. If the watch shuts off when USB is
  unplugged, check the battery connector.

## Credits

Built on [MeshCore](https://github.com/meshcore-dev/MeshCore) (MIT, Scott Powell / rippleradios.com).
The T-Watch S3 Plus board port started from work by
[pelgraine](https://github.com/pelgraine/MeshCore/tree/twatch-s3-plus) and
[djkazic](https://github.com/meshcore-dev/MeshCore/pull/2799), with pin research from
[GrayHatGuy](https://github.com/meshcore-dev/MeshCore/issues/2678), LilyGoLib and Meshtastic. It uses
[LVGL](https://lvgl.io), [LovyanGFX](https://github.com/lovyan03/LovyanGFX),
[XPowersLib](https://github.com/lewisxhe/XPowersLib) and
[SensorLib](https://github.com/lewisxhe/SensorLib).
