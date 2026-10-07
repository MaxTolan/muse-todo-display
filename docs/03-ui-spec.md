# UI spec

Design goal: **cute**, calm, glanceable, tappable. The owner looks at it across the desk and
taps items done. Large touch targets (minimum ~64 px), high contrast, no clutter, no tiny text.

## Style: make it cute (owner's request — have fun with it)

The owner wants this to be a delightful little desk object, not a utilitarian list. Be playful
within the calm/glanceable rules:

- Soft, friendly shapes: rounded rows and checkboxes, chunky type, a warm palette that goes
  with the pink stand.
- Small moments of joy: a squishy checkbox press, a happy strike-through, rows that wave
  goodbye or hop away when they leave, a tiny top-hat Muse peeking in now and then.
- Personality in the copy: friendly idle text ("waiting for today's list"), a cheerful
  "all done" screen.
- Keep it light on motion: animations are short, never loop forever over the list, and never
  get in the way of reading or tapping.

**Render previews whenever possible.** Every UI change should come with a picture the owner can
look at: headless simulator screenshots (`muse_simulator --headless --scenario … --screenshot …`)
for logic/states, or `MUSE_BENCH=1` builds with `tools/muse/snap.py` once the board is here.
Show previews of each state (open list, undo window, crossed out/sending, message bar,
all done, idle) before calling UI work done.

## Screen layout (800x480 landscape)

- **Top bar:** current day/date and a small count ("5 of 9 done"). Time comes from SNTP,
  time zone America/Chicago.
- **Main area:** scrollable checklist. Each row: a large checkbox on the left, the item label
  in large type. Items from a non-manual source show a small tag with the source (e.g. "auto"
  or "HealthKit").
- **Message bar** (bottom): shows Muse's replies and messages Muse sends to the screen.
  One message at a time, plain text, wraps to at most two lines (truncate with "…" beyond that).
  It stays until replaced by a newer message or until its time is up, then clears:
  **30 seconds** for a message under 50 characters, **5 minutes** otherwise (owner's choice,
  Oct 7). Tapping it clears it sooner.
- The completion celebration plays over the main area (see below).

## Item lifecycle

1. **Tap.** Tapping a manual item's row fills the checkbox and starts the **undo window**
   (default 5 s). The row shows a small "Undo" button. Debounce: ignore repeat taps on the same
   row within ~300 ms.
2. **Undo.** Tapping "Undo" inside the window returns the item to open. Nothing was sent.
3. **Crossed out.** When the window ends, the label is struck through and dimmed, with a small
   "sending…" note. The completion goes into the outbox (see `04-agent-integration.md`) and is
   sent to Muse as soon as there's a connection. Undo is no longer offered.
4. **Reply.** When Muse's reply arrives, it's shown in the message bar as-is and the row fades
   out and the list re-flows.
5. **Offline.** While there's no connection, crossed-out items stay crossed out with
   "waiting for connection" and are sent when it comes back — including after a reboot.
6. **If Muse rejects or the send fails for good:** show whatever Muse said, or "couldn't reach
   Muse" in the message bar, and still remove the item. The owner deals with it if it happens;
   don't build anything more elaborate.

Items Muse marks done (HealthKit and other auto sources) skip the outbox: the row shows
crossed out with its source tag for a few seconds, then fades out. No message is sent.

Only items with `source: "manual"` are tappable. Auto items are display-only until Muse marks them.

## List updates

- Each push from Muse **replaces** the whole list. Muse can push at any time of day, not only
  in the morning.
- The device **never clears or resets the list on its own** — Muse owns the day rollover.
- **Local state wins until Muse has replied:** if a pushed list contains an item that's in its
  undo window or in the outbox, keep showing it as tapped/crossed out. Otherwise a mid-day push
  would bring back an item the owner just checked off.
- Items not in the new list disappear (unless they're in the outbox — the outbox still sends).

## Celebration (issue #1)

- When the last open item becomes done (by tap or by Muse), play a two-second top-hat Muse
  celebration animation (the agent's avatar: fuzzy form, black top hat — keep it simple, e.g. a
  bouncing hat-tip). Then show an "all done" state.
- It plays once per transition to "nothing left open". If a later push adds open items, the list
  comes back; completing those plays the celebration again.
- A push that arrives with everything already done shows "all done" without the animation.

## Idle / no list

If no list has arrived yet (or Muse is unreachable and there's no list), show a calm idle
screen — time/date and "waiting for today's list" — never an error dump.

## Item types (examples — the firmware must not hard-code these)

The device renders whatever Muse sends. Behaviour depends only on `source`
(`manual` → tappable; anything else → display-only, shown with a source tag).

| Item | `source` | Notes |
| --- | --- | --- |
| Rosary | `manual` | |
| Bible in a Year podcast | `manual` | |
| 10,000 steps | `healthkit` | Muse marks done from Apple HealthKit |
| Vitamins AM | `manual` | |
| Vitamins PM | `manual` | |
| 30 min cardio (140+ bpm) | `healthkit` | Muse marks done from HealthKit |
| Sunlight | `manual` | |
| PT exercises | `manual` | |
| 180 g protein | `myfitnesspal` | Muse only sends it once a MyFitnessPal connection exists |
| 40 g fiber | `myfitnesspal` | Same |
| Dated todos/reminders | any | One-off items Muse includes (e.g. "return book to UPS") |

Long item names must wrap or truncate gracefully — never overflow the row.

## What NOT to build

- No settings screens beyond what's needed for Wi-Fi/pairing (SDK-provided).
- No on-device text entry or keyboards, unless issue #2 (task entry) is specified otherwise.
- No phone-mount, MagSafe, or ring accessories — desk stand only, permanently.
- No battery-life claims anywhere in the UI or docs.
- No on-device day rollover or scheduling.
