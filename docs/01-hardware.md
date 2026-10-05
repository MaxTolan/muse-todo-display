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

## Order info

- Board: Newegg order 415262659, $99.57, estimated delivery Oct 13–15, 2026.
- Stand: Amazon order #TJZ8A5TQ5 / store order 4110015-20057, $11.99, shipped Oct 4, 2026.

## Known hardware quirks (same board, third-party ports)

- **WiFi SoftAP/captive-portal setup is unreliable** on this board; STA (`WiFi.begin(ssid,pass)`)
  works fine. Likely cause: the RGB panel DMA continuously reads the framebuffer from PSRAM,
  competing with AP beacon timing. Seed credentials via STA or USB serial config — do not rely
  on an on-device AP portal.
- GT911 occasionally reports a single spurious "no touch" mid-hold (I2C read glitch). If tap
  handling flickers, debounce: treat touch as ended only after ~60 ms with no contact.
- Framebuffer lives in PSRAM; keep a bounce-buffer strategy in mind for tear-free LVGL flushes.

## Sources

- Pin map + timing + CH422G/GT911 notes: https://github.com/chuck3cz/ws-lcd5-meteoplaneradar
- 5B variant (1024x600) spec: https://manuals.plus/ae/1005007588150098.pdf
