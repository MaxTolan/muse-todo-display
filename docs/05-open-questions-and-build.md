# Open questions (ask the owner — do not guess)

1. **Board variant**: 800x480 or 1024x600 "5B"? Resolved at unboxing (~Oct 13–15). The port in
   `02-board-support-port.md` assumes 800x480.
2. **Scheduled command calls**: does Muse call a device command on its own schedule? Answered
   by the spike in `04-agent-integration.md`.
3. ~~Task entry (issue #2)~~ — decided Oct 9: on-screen keyboard, see below.
4. **Celebration artwork**: the two-second top-hat Muse animation is specified; the exact
   artwork is up to whoever implements it — keep it simple and charming.
5. ~~Code location~~ — decided Oct 7, see below.
6. ~~Simulator~~ — decided Oct 7, see below.

## Decided

- Muse owns the day rollover; the device never clears the list; each push replaces it.
- Muse can push updates any time of day, not only in the morning.
- Completions: undo window, then crossed out, sent as a typed chat turn with the item ID,
  held and retried while offline (persisted), removed when Muse replies.
- Muse's replies and Muse-sent messages are shown in a message bar, as-is. Rejections are not
  handled specially.
- Items carry a free-form `source` field; new sources need no firmware change. Protein and
  fiber simply aren't sent until the MyFitnessPal connection exists.
- Backlight: on/off only (hardware limit); uses the SDK's existing brightness setting as a switch.
- Code location (Oct 7): the SDK is a pinned git submodule at `third_party/muse-gadget-sdk`;
  the todo code is its own component (`components/todo_display`). SDK changes needed for the
  board port and commands go in as small patches/upstream PRs, not a fork.
- Main screen (Oct 7): the todo screen is the home screen; the SDK's avatar/settings screens
  are used for pairing and Wi-Fi setup.
- Simulator (Oct 7): this repo has its own 800x480 simulator (`sim/`) with the same LVGL/SDL
  pins as the SDK's; the SDK simulator is unchanged.

- Task entry (Oct 9, issue #2): an on-screen keyboard, hidden until the owner taps +. The
  device never edits the list itself: it sends Muse an "Add task" chat turn through the outbox
  and Muse pushes the updated list. On-device voice would need an I2S mic (the board has
  none); voice through the Muse app already works with no firmware change.
- Dance on every completion (Oct 9, issue #1): bottom right, above the message bar when it's
  showing.
- Avatar licensing (Oct 9): the SDK README says the Apache license doesn't cover the Jollybot
  avatar (`esp32/avatar`, which includes `muse_pixel.c`). The owner accepts this: personal
  project, never sold. Keep using the SDK's avatar as is.

## Decided by the implementer (flag if you disagree)

- **Before pairing, the todo screen shows** ("waiting for today's list" plus how to pair:
  Muse app > Settings > Devices > Add Device > MuseGadget-XXXXXX). The SDK's own screens take
  over only while pairing is in progress (app connected, or the tap-to-confirm prompt), and
  settings stay one swipe to the left.
- **Screen auto-sleep is turned off** once, on the first boot of this firmware (a desk display
  should stay visible). It can be changed in the SDK's settings afterwards.
- **Patch 0003 (`muse_ext.h`)** keeps all todo code in our component: the SDK only gains generic
  hooks, so `main/` needs no todo-specific code and the SDK checkout builds other boards as before.

- **The last list survives a reboot.** The device persists the list along with the outbox, so a
  power blip doesn't drop back to "waiting for today's list" (in keeping with "the device never
  clears the list on its own"). Items in their undo window at power loss come back open.
- **Look:** cozy dark berry theme (plum background, pink checkboxes and accents to match the
  stand, cream text, mint for HealthKit/auto). The pixel Muse avatar is drawn on black, so a
  dark theme lets it sit on the screen without a box around it.
- **Celebration** plays when the last open item's undo window ends (not at the tap), so undo
  never has to take back a celebration. Removing items without completing them doesn't celebrate.
- **Give-up after 24 h of failed sends** is measured on the wall clock from the first failure,
  so time offline (no send attempts) never counts toward it.
- **Widgets:** the add-task sheet uses LVGL's keyboard and textarea. The SDK leaves them at
  LVGL's defaults (enabled); the firmware overlay should set `CONFIG_LV_USE_KEYBOARD=y` and
  `CONFIG_LV_USE_TEXTAREA=y` explicitly so a future SDK change can't drop them.
- **Fonts:** Montserrat 16–48 (LVGL built-ins). The firmware overlay must enable
  `CONFIG_LV_FONT_MONTSERRAT_{24,32,40,48}` in addition to the SDK's sizes. These fonts only
  cover ASCII, so accented letters and emoji in labels won't render yet.

# Build, flash, and iterate

One-time setup (done on the owner's Mac, Oct 9):

- ESP-IDF v6.0.1 in `~/esp/esp-idf-v6.0.1` (`./install.sh esp32s3`); `tools/fw.sh` finds it
  there, or set `IDF_EXPORT=/path/to/export.sh`.
- Your SDK token, one line, in `sdk_token.local` at the repo root (git-ignored). `tools/fw.sh`
  passes it to the build privately and never prints it. (The SDK's boot banner does print it
  on the serial console; don't paste console logs anywhere public.)

Every time:

```sh
tools/fw.sh flash      # apply our SDK patches, build, flash over USB
tools/fw.sh monitor    # serial console (Ctrl-] quits)
tools/sim.sh test      # host tests: model, out-of-memory, simulator scenarios
```

- The board's USB-C is the ESP32-S3's own USB Serial/JTAG (`303a:1001`); the console is there too.
- On-device screenshots: `MUSE_BENCH=1 tools/fw.sh flash`, then
  `third_party/muse-gadget-sdk/esp32/tools/muse/snap.py /dev/cu.usbmodem101 "" out.png 8`
  (with ESP-IDF's environment active). Flash the normal build again afterwards.
- SDK host tests (`python3 -m unittest discover -s tests` in the SDK's `esp32/`) need
  `managed_components/`, which `board.sh` deletes after each build: run `idf.py reconfigure`
  first. All 181 pass with our patches (Oct 9). On macOS with the newest Command Line Tools,
  set `SDKROOT` to `MacOSX26.5.sdk` (see `tools/sim.sh`).
- A build keeps its `sdkconfig` in the build directory; after changing an overlay
  (`firmware/sdkconfig.todo` or the SDK's), delete `build-muse-waveshare-s3-lcd5/sdkconfig`.
- Commit small and working. `main` should always build.
