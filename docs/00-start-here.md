# START HERE (for the coding agent)

You are picking up firmware for **Maxwell Tolan's desk todo display**. Read this file first,
then `01-hardware.md` through `06-build-and-flash.md` in order. The owner's intent and the
hard constraints are all in these docs — do not invent features beyond them.

## What the owner wants

A touchscreen **desk** todo display for Muse, the Meta personal AI agent. Every morning the
owner's Muse agent pushes the day's todo list to the screen; the owner taps checkboxes to
complete items, completed items disappear, and the device reports completions back so Muse
records them. Steps and cardio auto-complete from Apple HealthKit; protein and fiber join
later once a MyFitnessPal connection exists.

## What you are building (in priority order)

1. **Board support port**: the 5" Waveshare board is NOT in the muse-gadget-sdk board list.
   Add it (`02-board-support-port.md`). This is the first deliverable and unblocks everything.
2. **Todo UI on the device**: checkbox list, tap-to-complete, completed items vanish, a
   two-second top-hat Muse celebration animation on the last completion. (`03-ui-spec.md`)
3. **Agent integration**: receive the todo list from the Muse agent, report completions back.
   ⚠️ The agent-to-display API is not yet confirmed — read `04-agent-integration.md` and do
   NOT write integration code against a guessed API. Ask the owner before committing to an approach.
4. **HealthKit auto-checks** for steps/cardio (handled mostly agent-side; device just shows state).

## Hard constraints (do not violate)

- This is a **desk piece on a stand**. Phone mounts, MagSafe wallets, and phone rings were
  explicitly rejected — never re-pitch them.
- E-ink was ruled out (no touch, slow refresh). Touchscreen only.
- Do not promise battery life figures for anything in this project.
- Keep the UI calm and glanceable: large touch targets, high contrast, no clutter.
- The SDK is `facebookincubator/muse-gadget-sdk` (Apache 2.0), ESP-IDF v6.0.1. Follow its
  `esp32/` conventions and its `AGENTS.md`.

## Current status (as of Oct 5, 2026)

- Hardware ordered Oct 2; the 5" board arrives ~Oct 13–15, 2026.
- Firmware not started. The `esp32/simulator/` in the SDK runs the UI on desktop — use it for
  UI iteration before the board lands.
- Two open GitHub issues exist: #1 (completion celebration), #2 (task entry). They track
  UI work, not hardware.

## How to work

- Ask the owner about anything in `05-open-questions.md` before guessing.
- Commit small, working increments. Verify each on the simulator; verify on hardware once it arrives.
- If the SDK's documented API doesn't cover something you need (especially agent push),
  stop and ask rather than inventing a protocol.
