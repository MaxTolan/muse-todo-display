# START HERE (for the coding agent)

You are picking up firmware for **Maxwell Tolan's desk todo display**. Read this file first,
then `01-hardware.md` through `05-open-questions-and-build.md` in order. The owner's intent and
the hard constraints are all in these docs — do not invent features beyond them.

## What the owner wants

A touchscreen **desk** todo display for Muse, the Meta personal AI agent. Muse pushes the todo
list to the screen (each morning, and again during the day whenever something changes). The
owner taps items to complete them; a completed item is crossed out, the device tells Muse in a
short chat message, shows Muse's reply on screen, and then removes the item. Steps and cardio
auto-complete from Apple HealthKit; protein and fiber join later once a MyFitnessPal connection
exists. Muse can also send short messages to the screen.

## What you are building (in priority order)

1. **Board support port** — the 5" board is not in the SDK yet, but the SDK's Waveshare
   ESP32-S3-Touch-LCD-7 port uses the same panel interface, touch controller, I/O expander
   and pins. Clone it (`02-board-support-port.md`). Unblocks everything on hardware.
2. **Integration spike** — confirm Muse can call a custom gadget command on a schedule and
   that a typed chat turn from the device reaches Muse and returns a reply
   (`04-agent-integration.md`, "Spike"). Small, but it de-risks everything after it.
3. **Todo UI** — checklist, tap → undo window → crossed out → Muse reply → removed; message
   bar; all-done celebration (issue #1). (`03-ui-spec.md`)
4. **Agent integration** — `todo.set_list` / `todo.show_message` commands (Muse → device) and
   the completion outbox that sends typed chat turns (device → Muse), with offline hold-and-retry.
   (`04-agent-integration.md`)
5. **Task entry (issue #2)** — scope not yet defined. Do not build until the owner specifies
   it (see `05-open-questions-and-build.md`). Until then the "no on-device keyboard" rule stands.

HealthKit/MyFitnessPal auto-completion is agent-side: the device just renders the state Muse
pushes. New item sources need no firmware change (see the `source` field in `04`).

## Hard constraints (do not violate)

- This is a **desk piece on a stand**. Phone mounts, MagSafe wallets, and phone rings were
  explicitly rejected — never re-pitch them.
- E-ink was ruled out (no touch, slow refresh). Touchscreen only.
- Do not promise battery life figures for anything in this project.
- Make the UI **cute** (the owner wants you to have fun with it) while keeping it calm and
  glanceable: large touch targets, high contrast, no clutter. Render previews of UI work
  whenever possible (`03-ui-spec.md`, "Style").
- **The device never resets or clears the list on its own.** Muse decides when the day rolls
  over; each push replaces the list.
- **Completions must never be lost or double-recorded.** They queue on the device (persisted)
  until Muse replies, and every message carries the item ID so Muse can ignore repeats.
- The SDK is `facebookincubator/muse-gadget-sdk` (Apache 2.0), ESP-IDF v6.0.1. Follow its
  `esp32/AGENTS.md` and `esp32/devices/AGENTS.md`, including "Say Muse, never Hatch" in new
  code and text, and never commit the SDK token.
- GPIO0 (BOOT) is an RGB data line on this board, so the BOOT button can't be read while the
  screen runs. Pairing is confirmed by touch, as on the SDK's LCD-7 port.

## Current status (as of Oct 7, 2026)

- Hardware ordered Oct 2; the 5" board arrives ~Oct 13–15, 2026. Stand has arrived.
- **Todo UI done on the simulator** (priority 3, ahead of hardware): model, screen, celebration
  (issue #1) and an 800x480 simulator with scripted scenarios. Previews: `docs/previews/`.
  Code layout and how to run it: "Repo layout" below and `sim/README.md`.
- Not started: board port (needs the board), integration spike, firmware glue for the
  commands/outbox (`04-agent-integration.md`). The model already implements the outbox logic;
  the firmware only has to wire it to `muse_hatch_text_turn`, turn events and NVS.
- Push direction (Muse → device) uses the SDK's documented custom-command mechanism.
  Device → Muse uses typed chat turns. The one unverified piece is whether Muse will call a
  device command on a schedule — that's the spike.
- Open GitHub issues: #1 (completion celebration — built, see previews 10–12), #2 (task entry).

## Repo layout

- `third_party/muse-gadget-sdk/` — the SDK as a pinned git submodule (owner decision, Oct 7).
  Run `git submodule update --init` after cloning.
- `components/todo_display/` — the todo code as an ESP-IDF component:
  - `todo_model.c` — list, item lifecycle, undo window, outbox, retry/backoff, persistence blob.
    Plain C + cJSON, no LVGL/ESP-IDF, so it's unit-tested on the host.
  - `todo_ui.c` — the LVGL 9.5 screen. Renders the model; owns no state; all animation is
    driven by the time passed in, so scripted runs are pixel-identical every time.
  - `todo_mascot.c` — the SDK's pixel Muse (`avatar/muse_pixel.c`) with a pixel top hat.
- `sim/` — 800x480 SDL simulator (owner decision: our own sim, no SDK changes).
- `tests/` — model unit tests (`test_todo_model.c`) and simulator scenario tests (`test_sim.py`).
- `tools/sim.sh` — build / test / run / render previews.

The todo screen is the device's **main screen**; the SDK's avatar and settings screens stay for
pairing and Wi-Fi setup (owner decision, Oct 7).

## How to work

- Ask the owner about anything in `05-open-questions-and-build.md` before guessing.
- Commit small, working increments. Verify each on the simulator; verify on hardware once it arrives.
- If something isn't covered by the SDK's documented mechanisms (custom commands, typed chat
  turns), stop and ask rather than inventing a protocol.
