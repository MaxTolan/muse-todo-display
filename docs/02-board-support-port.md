# Board-support port (do this first)

The 5" Waveshare board isn't in the muse-gadget-sdk yet, but the SDK already supports the
**Waveshare ESP32-S3-Touch-LCD-7**, which shares this board's panel interface, pins, GT911 touch,
CH422G expander, flash and PSRAM (details in `01-hardware.md`). The port is a copy of the LCD-7
port with a few values changed — not a new driver.

Follow `esp32/devices/AGENTS.md` in the SDK (the "add a board" recipe for boards with the full UI).

## What the port consists of

Boards with the full UI use three pieces. Copy each from the LCD-7 equivalent:

1. **Overlay** `devices/sdkconfig.muse-waveshare-s3-lcd5` from
   `devices/sdkconfig.muse-waveshare-s3-lcd7`. Set the new board's Kconfig symbol (e.g.
   `CONFIG_MUSE_BOARD_WAVESHARE_S3_LCD5=y`) and add it to `components/muse/Kconfig` wherever
   `MUSE_BOARD_WAVESHARE_S3_LCD7` appears (the board choice, the board-name default, and the
   UART-console default), plus the `elseif` in `components/muse/CMakeLists.txt`. Keep: ESP32-S3, 16 MB flash, octal PSRAM, `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`.
   Check the console setting (`CONFIG_ESP_CONSOLE_UART_DEFAULT` on the LCD-7) against the
   5" board's USB bridge.
2. **Board file** `components/muse/boards/board_waveshare_s3_lcd5.c` from
   `board_waveshare_s3_lcd7.c`. Change:
   - `vsync_back_porch` and `vsync_front_porch` to 16 (LCD-7 uses 8).
   - `.name = "Waveshare ESP32-S3-Touch-LCD-5"`, `.diagonal_in = 5.0f`, and `avatar_px` if the
     avatar looks too large.
   - Anything the 5" demo shows differently in the CH422G bit map (touch reset, backlight, LCD reset).
   - Keep: the GT911 address-select dance on GPIO4, `poll_buttons` returning 0 (GPIO0 is
     an RGB data line), touch-confirmed pairing, and backlight as an on/off switch in
     `set_brightness`.
3. **Helper entry** in `tools/muse/board.sh` (e.g. `lcd5) profile=waveshare-s3-lcd5; target=esp32s3 ;;`),
   the USB descriptor in `tools/muse/ports.py` if the bridge differs, and a row in
   `devices/README.md`.

Backlight: the LCD-7 port already maps the SDK's brightness setting to on/off, so the settings
UI needs no new toggle.

## ⚠️ Variant check first

If the board turns out to be the 1024x600 "5B", stop: resolution and timing differ and the
copy isn't valid as-is. Check the sticker at unboxing.

## UI layout note

800x480 is landscape; the SDK's avatar UI is centred for round/small screens, but the LCD-7
port already runs it at 800x480, so expect the stock UI to look like the LCD-7's.

## Acceptance criteria

- `tools/muse/board.sh build lcd5` completes against ESP-IDF v6.0.1, with the size check under
  the slot limit.
- Flashing shows the stock UI with a stable picture and working touch; touch confirms pairing.
- Boot log names the board (`muse: board: Waveshare ESP32-S3-Touch-LCD-5`).
- Backlight switches on/off without upsetting the I2C bus.
- The SDK host tests pass, and the simulator (`esp32/simulator/`) still builds.
