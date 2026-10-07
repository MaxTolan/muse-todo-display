# Open questions (ask the owner — do not guess)

1. **Board variant**: 800x480 or 1024x600 "5B"? Resolved at unboxing (~Oct 13–15). The port in
   `02-board-support-port.md` assumes 800x480.
2. **Scheduled command calls**: does Muse call a device command on its own schedule? Answered
   by the spike in `04-agent-integration.md`.
3. **Task entry (issue #2)**: what does it mean on this device? Until specified, no on-device
   text entry.
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

## Decided by the implementer (flag if you disagree)

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
- **Fonts:** Montserrat 16–48 (LVGL built-ins). The firmware overlay must enable
  `CONFIG_LV_FONT_MONTSERRAT_{24,32,40,48}` in addition to the SDK's sizes. These fonts only
  cover ASCII, so accented letters and emoji in labels won't render yet.

# Build, flash, and iterate

- Toolchain: ESP-IDF v6.0.1. Boards with the full UI build with `tools/muse/board.sh build <board>`
  (this board: `lcd5`, once the port exists; the LCD-7 builds with `lcd7`).
- The SDK's `esp32/AGENTS.md` is the authoritative build/flash reference — read it before
  touching the build system. Each build keeps its `sdkconfig` in its build directory; delete it
  after changing overlays.
- UI iteration: `tools/sim.sh run` opens this repo's 800x480 simulator; `tools/sim.sh test`
  runs the model and scenario tests; `tools/sim.sh previews` re-renders `docs/previews/`.
  Details in `sim/README.md`.
- Hardware iteration: once the board arrives, flash the board-support port first (acceptance
  criteria in `02-board-support-port.md`), then the todo UI.
- Before handing back work: board build passes with the size check, SDK host tests pass, and a
  flashed board boots without panics.
- Commit small and working. `main` should always build.
