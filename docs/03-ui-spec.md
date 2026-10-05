# UI spec

Design goal: calm, glanceable, tappable. The owner looks at it across the desk and taps items
done. Large touch targets (minimum ~64 px), high contrast, no clutter, no tiny text.

## Screen layout (800x480 landscape)

- Top bar: current day/date and a small count ("5 of 9 done").
- Main area: scrollable checklist. Each row: a large checkbox square on the left, the item
  name in large type. Auto-completed items (steps, cardio) show a small "auto" tag.
- Bottom area: reserved for the completion celebration overlay (see below).

## Behaviors

1. **Tap to complete.** Tapping a row's checkbox marks it done with a brief visual press
   feedback (e.g. checkbox fills). Debounce taps: ignore repeat taps on the same row within
   ~300 ms.
2. **Completed items disappear.** The row fades/slides out and the list re-flows. The device
   reports the completion to the Muse agent (see `04-agent-integration.md`); the device does
   NOT need to persist completion state beyond the current day — the agent is the source of
   truth for the list.
3. **Celebration.** When the last item is completed, play a two-second top-hat Muse
   celebration animation (the agent's avatar: fuzzy form, black top hat — keep it simple, e.g.
   a bouncing hat-tip). Then show an "all done" state until the next day's list arrives.
   (Tracks GitHub issues #1 and #2.)
4. **List updates.** When the agent pushes a new/updated list, replace the current list and
   re-render. Preserve nothing from the old list except items the agent marks carried over.
5. **Offline/idle.** If no list has arrived yet (or the agent is unreachable), show a calm
   idle screen — e.g. time/date and "waiting for today's list" — never an error dump.

## Item types (must all render)

| Item | Completion source | Notes |
| --- | --- | --- |
| Rosary | Tap | Manual |
| Bible in a Year podcast | Tap | Manual |
| 10,000 steps | Auto (agent) | Agent marks done from Apple HealthKit |
| Vitamins AM | Tap | Manual |
| Vitamins PM | Tap | Manual |
| 30 min cardio (140+ bpm) | Auto (agent) | Agent marks done from HealthKit |
| Sunlight | Tap | Manual |
| PT exercises | Tap | Manual |
| 180 g protein | Auto (agent, later) | Joins only after a MyFitnessPal connection exists — not yet |
| 40 g fiber | Auto (agent, later) | Same condition as protein — not yet |
| Dated todos/reminders | Tap or auto | One-off items the agent includes (e.g. "return book to UPS") |

Long item names must wrap or truncate gracefully — never overflow the row.

## What NOT to build

- No settings screens beyond what's needed for Wi-Fi/pairing (SDK-provided).
- No on-device text entry or keyboards.
- No phone-mount, MagSafe, or ring accessories — desk stand only, permanently.
- No battery-life claims anywhere in the UI or docs.
