# 800x480 todo simulator

Runs the real todo model and screen (`components/todo_display/`) on your desktop, with SDL
standing in for the 5" panel and touch. It uses the same pinned LVGL 9.5.0 and SDL2 as the
SDK's simulator, plus a pretend Muse that answers completion turns.

## Setup (macOS)

```sh
brew install cmake ninja sdl2
git submodule update --init
```

`tools/sim.sh` works around a Command Line Tools bug where the newest macOS SDK can't be
linked ("tapi error: malformed file"): it falls back to the newest SDK that works.

## Commands

```sh
tools/sim.sh run        # open the window (the mouse is your finger)
tools/sim.sh test       # model unit tests + scenario/screenshot tests
tools/sim.sh previews   # re-render docs/previews/ from sim/scenarios/
```

Keys in the window:

| Key | Does |
| --- | --- |
| L | Load a sample day (the items from `docs/03-ui-spec.md`) |
| O | Toggle the connection (offline holds completions) |
| R / E | Muse replies to / fails the turn in flight (it also auto-replies after 1.5 s) |
| M | Muse sends a message |
| K | Muse peeks in |
| P | Save `todo-sim.png` |
| Esc | Quit |

`--state FILE` saves the persisted state (list and outbox) there and restores it on the next
start, like NVS across a reboot.

## Scenarios

`sim/scenarios/*.txt` script a run: one `key=value` per line, applied in order, on a virtual
clock (fixed to 2026-10-07 09:14 America/Chicago unless `clock=` sets it).

| Key | Value |
| --- | --- |
| `clock` | `2026-10-07 07:02` (local time) |
| `online`, `refuse_turns` | `true` / `false` |
| `list_date` | Date text for the next push |
| `set_list` / `set_list_file` | `todo.set_list` items: inline JSON, or a file next to the scenario |
| `set_list_expect_error` | JSON that must be rejected |
| `message` | `todo.show_message` text |
| `tap` | `row:<id>`, `undo:<id>` or `message` (a real touch at that spot) |
| `tap_at` | `x,y`: a touch at exact screen coordinates |
| `reply` / `turn_error` | Muse answers / fails the turn in flight |
| `auto_reply`, `auto_reply_ms` | Reply text Muse sends automatically (`off` to stop), and the delay |
| `advance` | Run for N ms, drawing every 10 ms frame |
| `wait` | Skip ahead N ms in 1 s steps without drawing (for waits of minutes) |
| `peek` | Muse peeks in |
| `reboot` | Restart from the persisted state |
| `screenshot` | Save a PNG into `--out` |
| `redraw_reset`, `expect_redraw_max` | Zero the redrawn-pixel count / fail if more than N pixels were redrawn since |
| `expect_screen` | `idle`, `list`, `celebrate`, `all_done` |
| `expect_state` | `<id> <open\|undo\|queued\|sending\|auto_done\|leaving\|hidden\|gone>` |
| `expect_turn`, `expect_turns`, `expect_message` | Last turn contains text / number of turns / message bar text |

Any failed `expect_*` makes the run exit non-zero. `tests/test_sim.py` runs every scenario
twice and requires identical screenshots, so the UI must never read the real clock.
