# Device board adapter — evidence and limits

`firmware/device/board.cpp` provides real GPIO, SPI/SSD1677 and SDMMC/FAT VFS operations. It has not been flashed or tested on the physical board. A successful build is not a screen, button or SD test.

## Pinned reference inspected

All FolloUp files below were fetched read-only through the GitHub connector at commit `1682b7e7d7ff9c6b23ab7e903c44b241f095b402` on 2026-11-04. The upstream project was not compiled or modified.

| Evidence | Values used |
|---|---|
| [waveshare_board_config.h](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/board/include/waveshare_board_config.h) | 800×480, SPI3 SCK11/MOSI12/no MISO, CS10/DC9/RST46/BUSY3; rocker4/5/6 and BOOT0 active low; SDMMC CLK16/CMD17/D0=15/D1=7/D2=8/D3=18 |
| [epaper_panel.h](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/epaper_panel/include/epaper_panel.h) | BUSY active high, reset low2ms/high50ms, busy timeout10s |
| [epaper_panel.cpp](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/epaper_panel/epaper_panel.cpp) | SPI mode0 20MHz, manual CS/DC, internal DMA staging, minimum one RTOS tick during polling |
| [ssd1677_driver.cpp](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/epaper_panel/ssd1677_driver.cpp) | InitFull(false), booster/output/entry/window/border/temperature values, full image planes0x24/0x26, update0x22=0xF7 then0x20 |
| [render_utils.cpp](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/epaper_ui/render_utils.cpp) | DrawRawPixel: row-major, bit0x80>>(x&7), clearing bit means black |
| [sd_card.cpp](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/sd_card/sd_card.cpp) | ESP-IDF esp_vfs_fat_sdmmc_mount, default host/slot, 4-bit GPIO matrix, optional internal pullups |
| [waveshare_board.cpp](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/board/waveshare_board.cpp) | Upstream touches AXP2101 rails and charger; these operations deliberately excluded pending revision verification |

The board identity and revision still require physical confirmation against the [manufacturer documentation](https://docs.waveshare.com/ESP32-S3-ePaper-3.97) and schematic referenced in `hardware.md`.

## Interface and execution

- Caller owns and serializes all calls. `beginBoard(error)` configures input pullups and screen GPIO/SPI; it does not display an image or initialize a PMIC. An occupied SPI3 bus is refused rather than assumed to have matching pins. Resources remain allocated for the application lifetime.
- `display(frame,error)` requires a stable readable 48,000-byte frame. Landscape raw coordinates are 800×480 with 100 bytes per row; bit7 is the leftmost pixel; 1=white, 0=black. Framebuffer row0 uses the reference top-row convention and is written from RAM cursor (0,479), decrementing gate Y. Physical USB-down portrait rotation belongs to the renderer; there is no automatic rotation here.
- Each display performs reset, normal full initialization, writes both complete planes and drives the normal full OTP waveform. Cursor is explicitly reset between planes. No partial waveform, forced-temperature fast waveform, retained image optimization or background refresh is claimed. Frequent full refreshes will visibly flash.
- Each of the three BUSY waits is capped at10s and sleeps at least one RTOS tick between polls. A timeout is returned to the caller; a later display retries from hardware reset. This is a per-wait bound, so a complete failure path may approach30s. The SPI transfers use the SDK synchronous polling API with 1024-byte internal DMA staging.
- `mountNotes(error)` mounts the existing filesystem at `/sdcard`, with formatting disabled, default SDMMC frequency, four data lines and internal pullups. It then creates `/sdcard/compagnon-notes` with mkdir(0700), or verifies that an existing path is a directory. It never formats, erases, lists or recovers existing notes. Recovery is the storage owner's next step. A failed mount is an error, not an empty successful store.
- FAT does not enforce Unix owner permissions: “private” means the HTTP adapter must only expose this directory through its authenticated note handlers, never a generic file-serving route. Physical card possession remains access to its files.
- BOOT0 is input-only in this adapter and remains a boot strapping control. PWR/PMIC IRQ, sleep, charging, battery readings, audio and sensors are outside this slice.

## Power limitation

No PMIC register is read or written, and no charger profile is assumed. The reference identifies AXP2101 at0x34 while other board documents name TG28. Correctly powered peripherals are a prerequisite: persistent BUSY or failed SD initialization may indicate disabled peripheral rails. This adapter does not guarantee a cold battery boot until the actual board/PMIC revision and essential rail configuration are verified. It must not silently reproduce the reference charger or backup-cell charging writes.

## License and validation

The SSD1677 sequence and transfer design are adaptations of the pinned FolloUp reference, whose root [LICENSE](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/LICENSE) is GNU GPL version3. The complete unchanged license is retained in `firmware/device/vendor/FolloUp-LICENSE.txt`; the adapted files declare `GPL-3.0-only`. No upstream copyright notice was present in the inspected source files; the project and exact paths remain attributed above. Distribution of the linked firmware must satisfy GPLv3, including corresponding source and compatible licensing of the complete combined work.

Validation on 2026-11-04: `board.cpp` cross-compiled successfully as a translation unit using the installed Arduino-ESP32 **3.3.6** ESP32-S3 toolchain and its actual SDK flags from `build/http-probe-objects/compile_commands.json`, additionally enabling `-Wall -Wextra -Werror -std=gnu++17`. No warnings. Older project statements about installed3.3.8 are stale. Full application linkage is tracked by the integrating task. There was no upstream build and no flash. Hardware verification remains required for orientation/polarity, refresh completion, key levels, SD access and cold boot power.
