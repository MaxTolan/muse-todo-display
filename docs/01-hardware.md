# Hardware

## The board: Waveshare ESP32-S3 5" capacitive-touch display

- MCU: ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM
- Display: 800x480, ST7262, RGB parallel 16-bit (RGB565), **pure timing-driven — no init sequence**.
  In ESP-IDF this is `esp_lcd_rgb_panel` (`esp_lcd_panel_rgb`), not a QSPI/SPI panel driver.
- Touch: GT911, I2C (SDA=GPIO8, SCL=GPIO9, INT=GPIO4), default address 0x5D, no dedicated reset pin.
- Backlight: **ON/OFF only**, driven through a CH422G I/O expander — no PWM dimming (hardware limit).
  CH422G uses a non-standard I2C protocol: each function is its own fixed 7-bit address
  (0x24 mode/dir, 0x38 push-pull outputs, 0x23 open-drain, 0x26 input read), no register pointer.
  Do NOT reuse TCA9554/PCA9554 code for it.

### Exact pin map (from Waveshare's own `esp_panel_board_custom_conf.h`)

- HSYNC=46, VSYNC=3, DE=5, PCLK=7
- D0–D7: 14, 38, 18, 17, 10, 39, 0, 45
- D8–D15: 48, 47, 21, 1, 2, 42, 41, 40
- RGB timing: pclk 16 MHz, HPW=4, HBP=8, HFP=8, VPW=4, VBP=16, VFP=16, pclk_active_neg=1

### ⚠️ Variant check at unboxing (do this first)

There is an upgraded "5B" revision with **1024x600** resolution. Check the package/sticker to
confirm which revision shipped before locking the panel timing table — the timing values above
are for the 800x480 unit.

## The stand

Lamicall phone/tablet dock stand (pink), holds the board upright on the desk. Arrived before the board.

## Delivery

- Board: from Newegg, estimated delivery Oct 13–15, 2026.
- Stand: from Amazon, arrived.

## Known hardware quirks (same board, third-party ports)

- **WiFi SoftAP/captive-portal setup is unreliable** on this board (RGB panel DMA competes
  with AP timing). This doesn't affect us: the SDK provisions Wi-Fi over BLE from the Muse app
  and runs in STA mode. For bench work, `CONFIG_HOMEHUB_WIFI_SSID`/`_PASSWORD` skip BLE
  provisioning. Never add an AP portal.
- GT911 occasionally reports a single spurious "no touch" mid-hold (I2C read glitch). If tap
  handling flickers, debounce: treat touch as ended only after ~60 ms with no contact.
- Framebuffer lives in PSRAM; the SDK's LCD-7 port already uses a bounce buffer
  (`LCD_W * 10` px) and `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` — reuse both.
- **GPIO0 (BOOT) is RGB data line D6**, so the BOOT button can't be polled while the panel
  runs. Pairing is confirmed by touch instead (same as the SDK's LCD-7 port).

## Same family as the SDK's LCD-7 port

The SDK's Waveshare ESP32-S3-Touch-LCD-7 port (`components/muse/boards/board_waveshare_s3_lcd7.c`)
matches this board on: 800x480 RGB565, HSYNC/VSYNC/DE/PCLK = 46/3/5/7, 16 MHz pclk, HPW/HBP/HFP
= 4/8/8, VPW = 4, GT911 on I2C 8/9 with INT on GPIO4, CH422G at 0x24/0x38, 16 MB flash + 8 MB
octal PSRAM, and the same D0–D15 data pins. Known difference: vertical porches (VBP/VFP 16/16
here vs 8/8 on the LCD-7). Confirm the CH422G bit map (touch reset, backlight, LCD reset) and
the USB bridge against Waveshare's 5" demo before trusting the copy.

## Sources

- Pin map + timing + CH422G/GT911 notes: https://github.com/chuck3cz/ws-lcd5-meteoplaneradar
- SDK LCD-7 port: https://github.com/facebookincubator/muse-gadget-sdk/blob/main/esp32/components/muse/boards/board_waveshare_s3_lcd7.c
- 5B variant (1024x600) spec: https://manuals.plus/ae/1005007588150098.pdf
