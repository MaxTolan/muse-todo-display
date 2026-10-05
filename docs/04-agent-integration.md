# Agent integration

This is the part with the biggest unknown. Read carefully and do not guess.

## The intended data flow

1. **Agent → device:** each morning (and on updates), the owner's Muse agent pushes the day's
   todo list to the display: item IDs, display names, and which items are auto-completed vs
   manual. Format and transport TBD (see below).
2. **Device → agent:** when the owner taps a checkbox, the device reports the item ID and a
   timestamp back to the agent, which records the completion against the owner's daily tracking.
3. **Agent → device (state):** the agent pushes completion states for auto items (steps,
   cardio) when HealthKit data confirms them.

The device is a **dumb terminal with touch**: the agent owns the list, the schedule, and all
persistence. The device renders and reports taps. Do not build on-device scheduling, accounts,
or cloud sync.

## ⚠️ The open question: how does the agent reach the display?

As of Oct 5, 2026, the Meta Muse Gadgets program (announced Oct 2, 2026) has **not** published
a documented, working path for a user's Muse agent to push content to a registered gadget's
display. The owner is actively watching for this. Consequences for you:

- **Do NOT write integration code against a guessed protocol.** No invented REST endpoints, no
  assumed MQTT topics, no reverse-engineered app traffic.
- Build the UI against a **local stub interface** with exactly these three operations:
  - `set_todo_list(items)` — items: `{id, label, auto: bool, done: bool}`
  - `mark_item_done(id)` — agent-driven state change (auto items)
  - `on_item_tapped(id, timestamp)` — callback the UI fires; the real transport plugs in here
- When the official agent-to-device path is confirmed, the stub gets replaced by the real
  transport with no UI changes. Keep the seam clean.

## Pairing (known, from the SDK docs)

- Get an SDK token from gadgets.muse.ai (Account > SDK tokens).
- Pair in the Muse app: Settings > Devices, Developer mode ON; the device advertises as
  `MuseGadget-XXXXXX`.
- Requires ESP-IDF v6.0.1 (`idf.py build`, `tools/board.sh <board> build`).
- `esp32/simulator/` runs the UI on desktop without hardware — use it for UI iteration.

## SDK reference

- Repo: https://github.com/facebookincubator/muse-gadget-sdk (Apache 2.0)
- Its `AGENTS.md` has the full build/flash instructions for coding agents. Follow them.
