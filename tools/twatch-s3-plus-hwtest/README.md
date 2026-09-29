# T-Watch S3 Plus hardware test

A standalone bring-up sketch. It powers the AXP2101 rails, scans both I²C buses, cycles display
colours, reports touch points over USB serial (and draws them on screen), initialises the SX1262,
and counts GPS bytes.

```sh
cd tools/twatch-s3-plus-hwtest
pio run -t upload && pio device monitor
```
