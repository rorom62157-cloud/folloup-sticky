# Waveshare ESP32-S3-ePaper-3.97

Reference code inspected: rorom62157-cloud/folloup-sticky, branch folloup-waveshare, commit 1682b7e7d7ff9c6b23ab7e903c44b241f095b402. Main targets a different Seeed board and must not supply pin definitions for this product.

| Peripheral | Pins / interface |
|---|---|
| Rocker up / select / down | GPIO4 / GPIO5 / GPIO6, active low |
| BOOT recording | GPIO0 active low, boot strap at reset |
| PWR | PMIC input; IRQ GPIO38, status via I2C |
| I2C | SDA41 / SCL42 |
| e-paper SSD1677 | SPI3 SCK11 MOSI12 CS10 DC9 RST46 BUSY3 |
| SDMMC 4-bit | CLK16 CMD17 D0=15 D1=7 D2=8 D3=18 |
| Audio ES8311 | MCLK13 BCLK14 WS47 DIN21 DOUT48, PA39 |
| PMIC / RTC / IMU | 0x34 / 0x51 / 0x6B |

16 MiB flash, 8 MiB PSRAM. Mono framebuffer: 48,000 bytes. Audio PCM mono 16kHz/16bit: 32,000 bytes/s, 7,680,000 bytes for four minutes plus header. Stream to SD rather than buffering the clip.

## Buttons and sleep

Reference implements BOOT hold-to-record and PWR software short/long events. Its PMIC is configured for software long IRQ at 1s and independent hardware cut at 6s. Desired 2–3s hold must be reconciled with available PMIC thresholds/event visibility; do not assume PWR is a readable GPIO with release event. Keep hardware recovery available.

GPIO38 wakes light sleep but is not an ESP32-S3 RTC GPIO suitable for EXT0/EXT1 deep wake. Deep wake can use rocker GPIO4/5/6 or BOOT0. Prefer rocker select for deterministic deep wake; validate BOOT-held reset/strap behavior before claiming immediate capture from deep sleep. Consume wake gesture appropriately and avoid immediate re-entry while a key remains pressed.

Docs/schematic name TG28 whereas the official example and FolloUp use AXP2101 programming at 0x34. Verify actual revision; do not blindly change charging/backup-cell charging configuration. Battery capacity and chemistry beyond user-provided board equipment are not measured.

## Sources

- https://docs.waveshare.com/ESP32-S3-ePaper-3.97
- https://files.waveshare.com/wiki/ESP32-S3-ePaper-3.97/ESP32-S3_e-Paper-3.97-schematic.pdf
- https://github.com/rorom62157-cloud/folloup-sticky/blob/folloup-waveshare/components/board/include/waveshare_board_config.h
- https://github.com/rorom62157-cloud/folloup-sticky/blob/folloup-waveshare/components/board/waveshare_board.cpp
- https://github.com/rorom62157-cloud/folloup-sticky/blob/folloup-waveshare/main/power_key_runtime.cpp
- https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/system/sleep_modes.html
