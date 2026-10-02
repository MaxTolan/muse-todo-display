# Muse Todo Display

A touchscreen desk todo display for Muse, powered by an ESP32. It shows the daily routine with tappable checkboxes — tap to complete, and Muse records it.

## Hardware

- **Waveshare ESP32-S3 5" capacitive touch display** (800x480) — the screen. Newegg order 415262659, arrives ~Oct 13–15, 2026.
- **Lamicall phone dock stand** — holds it upright on the desk. Order #TJZ8A5TQ5.

## What it does

- Shows daily todos with large tappable checkboxes; tapping a completed item dismisses it.
- Muse pushes the todo list and completion states to the screen.
- Taps notify Muse, so completions get recorded against daily tracking.
- Steps (10,000) and cardio (30 min) auto-complete from Apple HealthKit.
- Protein (180 g) and fiber (40 g) auto-complete once a MyFitnessPal connection exists.
- Also surfaces other dated todos/reminders, not just the daily routine.

## Daily items

| Item | How it completes |
| --- | --- |
| Rosary | Tap |
| Bible in a Year podcast | Tap |
| 10,000 steps | Auto (HealthKit) |
| Vitamins AM | Tap |
| Vitamins PM | Tap |
| 30 min cardio | Auto (HealthKit) |
| Sunlight | Tap |
| PT exercises | Tap |
| 180 g protein | Auto (MyFitnessPal, when available) |
| 40 g fiber | Auto (MyFitnessPal, when available) |

## Build notes (Meta Muse Gadgets docs)

- SDK: [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) (Apache 2.0), ESP32 Device SDK. Released Oct 2, 2026.
- **The 5" Waveshare board is not in the SDK board list** — board support must be added: copy the closest `devices/sdkconfig.*`, set chip/pins/flash/PSRAM. Closest reference: `ESP32-S3-Touch-AMOLED-1.75C`.
- Requires ESP-IDF v6.0.1 (`idf.py build`, `tools/board.sh <board> build`).
- Needs an SDK token from gadgets.muse.ai (Account > SDK tokens); pair in the Muse app via Settings > Devices (Developer mode on), device shows as `MuseGadget-XXXXXX`.
- `esp32/simulator/` runs the UI on desktop without hardware — useful for UI iteration before the board arrives.
- `AGENTS.md` in the SDK repo has the full build/flash instructions for coding agents.

## Roadmap

1. Board arrives → add 5" board support to the SDK, build, flash.
2. Pair with the Muse app (SDK token).
3. Todo UI: checkbox list, tap-to-complete callbacks.
4. Agent integration: Muse pushes todos, receives completions.
5. HealthKit auto-checks for steps/cardio.

## Status

Hardware ordered Oct 2, 2026. Firmware not started — waiting on board delivery and confirmation of the agent-to-device display/touch-callback API.
