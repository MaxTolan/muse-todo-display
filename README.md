# Muse Todo Display

A touchscreen **desk** todo display for Muse (Meta's personal AI agent), powered by an ESP32.
Every morning the owner's Muse agent pushes the day's todo list to the screen; tapping a
checkbox completes the item, completed items disappear, and the device reports back so Muse
records it. Steps and cardio auto-complete from Apple HealthKit.

## Hardware

- **Waveshare ESP32-S3 5" capacitive touch display** (800x480) — Newegg order 415262659,
  arrives ~Oct 13–15, 2026. Full spec, pin map, and quirks: `docs/01-hardware.md`.
- **Lamicall dock stand** (pink) — holds it upright on the desk.

## Documentation (start here if you're a coding agent)

| File | What it covers |
| --- | --- |
| `docs/00-start-here.md` | Intent, priorities, hard constraints, current status |
| `docs/01-hardware.md` | Board spec, pin map, RGB timing, quirks, variant check |
| `docs/02-board-support-port.md` | Adding the 5" board to the muse-gadget-sdk (do first) |
| `docs/03-ui-spec.md` | Checklist UI, tap behavior, celebration, item types |
| `docs/04-agent-integration.md` | Agent↔device data flow + the open API question |
| `docs/05-open-questions-and-build.md` | Decisions for the owner, build/flash workflow |

## Status

Hardware ordered Oct 2, 2026. Firmware not started — waiting on board delivery (~Oct 13–15)
and confirmation of the agent-to-display push API. UI work can start now on the SDK's
desktop simulator.

## SDK

[facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)
(Apache 2.0), ESP32 Device SDK, ESP-IDF v6.0.1. Pairing: SDK token from gadgets.muse.ai,
Muse app Settings > Devices (Developer mode), device advertises as `MuseGadget-XXXXXX`.
