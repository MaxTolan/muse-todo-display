# Board-support port (do this first)

The 5" Waveshare board is NOT in the muse-gadget-sdk's supported-board list (list verified from
the SDK README, commit b1a3822: AMOLED-1.75C/1.75, ESP32-BOX-3, reTerminal E1001, SenseCAP
Indicator/Watcher, HA Voice PE, ideaspark, AiPi Lite, M5Stack StickS3/StopWatch/StickC Plus2,
Cardputer ADV). Nothing else works until this port exists.

## What the port consists of

Following the SDK's existing convention (see the SDK `esp32/` README "Boards" table and the
`devices/` feature table):

1. **New board entry under `tools/board.sh`** (suggested name: `waveshare-5inch-lcd`) with
   sdkconfig defaults: ESP32-S3 target, 16 MB flash, OPI PSRAM enabled. Use a few-MB app
   partition — leave room for the framebuffer in PSRAM, not flash.
2. **Device descriptor in `devices/`** mapping:
   - Display: RGB565 panel via `esp_lcd_panel_rgb` with the pin map and timing in `01-hardware.md`.
     **No display init table is needed** (ST7262 is pure timing-driven, unlike ST7701/ST77916).
   - Touch: `esp_lcd_touch_gt911` on the I2C pins from `01-hardware.md`, with the ~60 ms
     debounce described there.
   - Backlight: a small CH422G output driver (ON/OFF only). Do NOT reuse TCA9554/PCA9554 code —
     the CH422G protocol is non-standard (fixed 7-bit address per function, no register pointer).
3. **SDK settings UI**: the board can't dim, so expose a backlight TOGGLE, not a brightness
   slider. (Open question: add it to the SDK settings UI, or leave backlight always-on for v1 —
   see `05-open-questions.md`.)

## UI layout note

800x480 is a landscape widescreen; the SDK's touch UI layout was designed for round/small
AMOLEDs. Expect a layout pass for the todo list — this is normal, not a bug in your port.

## Acceptance criteria

- `idf.py` / `tools/board.sh waveshare-5inch-lcd build` completes against ESP-IDF v6.0.1.
- Flashing the SDK's stock demo UI shows a stable picture with working touch on the 5" board.
- Backlight toggles on/off without crashing the I2C bus.
- Simulator (`esp32/simulator/`) still builds and runs (don't break the desktop path).
