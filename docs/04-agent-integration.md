# Agent integration

The device is a **dumb terminal with touch**: Muse owns the list, the schedule, the day
rollover, and the record of what got done. The device renders what Muse sends, reports taps,
and shows Muse's replies. No on-device scheduling, accounts, or cloud sync.

Both directions use mechanisms the SDK documents. Don't invent anything beyond them.

## Muse → device: custom gadget commands

The SDK lets a gadget advertise commands that Muse calls by name (`esp32/AGENTS.md`, "Adding a
command"): advertise in `build_register_json()` in `main/noise_control.cpp`, handle in
`on_ws_command()` in `main/app.c`. Muse reads the descriptions, so write them for Muse.

Commands to add (gate both places behind one Kconfig option, e.g. `CONFIG_TODO_DISPLAY`):

### `todo.set_list`

Replaces the whole list. Muse calls it each morning and whenever anything changes during the
day (HealthKit completions, new reminders, edits).

- Required `items` (string): a JSON array of items. Passed as a string because command
  parameters are typed as string/integer/boolean.
- Optional `date` (string): the day this list is for, shown in the top bar.
- Item shape:

  ```json
  {"id": "vitamins-am@2026-10-05", "label": "Vitamins AM", "source": "manual", "done": false}
  ```

  - `id` — unique **per occurrence**, not per habit: daily items need the date in the ID so
    today's completion can't be mistaken for yesterday's. Muse generates IDs; the device only
    echoes them back.
  - `source` — free-form string: `manual`, `healthkit`, `myfitnesspal`, or anything new. Only
    `manual` items are tappable; any other value is shown as a tag. New sources need no
    firmware change.
  - `done` — `true` for items Muse already considers complete.
- Returns `{"ok": true, "payload": {"shown": N, "pending": [ids in the outbox]}}`.
- Validate everything yourself (the SDK doesn't): reject non-arrays, missing/overlong fields,
  and lists over a sane cap (e.g. 40 items, 120-char labels). The SDK sets cJSON's nesting
  limit to 16; this shape is well under it.

### `todo.show_message`

Shows a short message in the message bar (see `03-ui-spec.md`) — reminders, encouragement,
anything Muse wants on the desk.

- Required `text` (string, ≤ 280 chars).
- Returns `{"ok": true}`.

Both handlers are fast (update state, poke the UI under the display lock), so they can return
synchronously; no `_async` task needed. Keep the descriptions short — `link.register` must fit
in 8 KB. Add a host test in `tests/` that both commands are advertised and dispatched under
the same option, like `test_link_sensecap_sensors.py`.

## Device → Muse: typed chat turns

The SDK has no device-initiated structured event, but a full-UI board with PSRAM can send a
**typed chat turn** to Muse: `muse_hatch_text_turn(char *text)` in
`components/muse/muse_chat.h` (enabled by `CONFIG_MUSE_HATCH`, on by default with PSRAM).
Muse's reply streams back as turn events (`muse_hatch_turn_event`: `SENT`, `REPLY`, `DONE`,
`ERROR`). This is free text read by Muse's model — the owner accepts that.

### Message format

One line per completion, item ID included so Muse can ignore repeats:

```
[todo-display] Completed: Vitamins AM (id vitamins-am@2026-10-05) at 9:14 AM.
```

If several completions are waiting, send them in one turn, one line each.

Muse needs a standing instruction (set up by the owner in Muse, not in firmware), roughly:
*"Messages starting with [todo-display] are completions from my desk display. Record each
item ID once and ignore IDs you've already recorded. Reply in one short sentence."*

### The outbox

- A completion enters the outbox when its undo window ends (`03-ui-spec.md`). Nothing is sent
  during the undo window, so undo never has to retract anything.
- The outbox is **persisted in NVS** so a reboot or power loss doesn't lose it.
- Send when Muse is connected (`muse_hatch_configured()` and the link is up), one turn at a
  time, oldest first, batching whatever is queued.
- `muse_hatch_text_turn` refuses a turn when Muse isn't set up or another turn is running
  (it logs `MUSE NOT SET UP` / `BUSY` and frees the text). Treat both as "not sent yet" and retry.
- On `DONE`: remove those IDs from the outbox, show the reply text in the message bar, and
  remove the rows.
- On `ERROR` or no `DONE` within a timeout (e.g. 60 s): keep the IDs and retry with backoff
  (e.g. 10 s, 30 s, 2 min, then every 5 min). A resend after a lost reply is safe because of the IDs.
- After a long run of failures (e.g. 24 h), drop the entry, show "couldn't reach Muse" and move on.

### Plumbing note

Today a typed turn's reply only goes to the serial console (`muse_hatch_console`). The todo
code must read the reply from the turn events and route it to the message bar. Check what in
the SDK already drains `muse_hatch_turn_event` before adding a reader — hook into the existing
consumer rather than adding a second one.

## Spike (do before building the integration)

The one thing not yet confirmed is whether Muse will call a device command **on its own
schedule** (the morning push, and mid-day HealthKit updates). Test it:

1. Add `todo.show_message` only.
2. Ask Muse (via `tools/muse/chat.py` or the app) to call it now, then to call it at a set
   time later today.
3. Send one typed turn with the `[todo-display]` format and check that the reply comes back.

If scheduled calls don't work, stop and tell the owner before building workarounds.

## Pairing and build (from the SDK docs)

- SDK token from gadgets.muse.ai (Account > SDK tokens). Set `CONFIG_GADGET_SDK_TOKEN` in the
  build's `sdkconfig`. Never commit or print it.
- Pair in the Muse app: Settings > Devices, Developer mode on, Add Device; it shows as
  `MuseGadget-XXXXXX`. Confirm pairing by touch (BOOT can't be read on this board).
- ESP-IDF v6.0.1.

## SDK reference

- Repo: https://github.com/facebookincubator/muse-gadget-sdk (Apache 2.0)
- `esp32/AGENTS.md`: build, flash, monitor, "Adding a command", naming rules. Follow it.
