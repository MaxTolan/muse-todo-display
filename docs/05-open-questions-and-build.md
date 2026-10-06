# Open questions (ask the owner — do not guess)

1. **Board variant**: 800x480 or 1024x600 "5B"? Resolved at unboxing (~Oct 13–15). The port in
   `02-board-support-port.md` assumes 800x480.
2. **Scheduled command calls**: does Muse call a device command on its own schedule? Answered
   by the spike in `04-agent-integration.md`.
3. **Task entry (issue #2)**: what does it mean on this device? Until specified, no on-device
   text entry.
4. **Celebration artwork**: the two-second top-hat Muse animation is specified; the exact
   artwork is up to whoever implements it — keep it simple and charming.
5. **Code location**: is this repo a fork of the SDK, or does it bring the SDK in (submodule /
   pinned commit) with the todo code as a component? And does the todo screen replace the
   SDK's avatar screen or sit alongside it (e.g. avatar while pairing/idle, list otherwise)?
6. **Simulator**: change the SDK simulator to 800x480 for layout work, or do layout on hardware?

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

# Build, flash, and iterate

- Toolchain: ESP-IDF v6.0.1. Boards with the full UI build with `tools/muse/board.sh build <board>`
  (this board: `lcd5`, once the port exists; the LCD-7 builds with `lcd7`).
- The SDK's `esp32/AGENTS.md` is the authoritative build/flash reference — read it before
  touching the build system. Each build keeps its `sdkconfig` in its build directory; delete it
  after changing overlays.
- UI iteration: `esp32/simulator/` runs the production UI on desktop, but in a fixed 412x412
  window (see open question 6). Its headless scenario mode is useful for testing behavior.
- Hardware iteration: once the board arrives, flash the board-support port first (acceptance
  criteria in `02-board-support-port.md`), then the todo UI.
- Before handing back work: board build passes with the size check, SDK host tests pass, and a
  flashed board boots without panics.
- Commit small and working. `main` should always build.
