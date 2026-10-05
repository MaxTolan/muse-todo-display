# Open questions (ask the owner — do not guess)

1. **Board variant**: 800x480 or 1024x600 "5B"? — resolved at unboxing (~Oct 13–15). The whole
   timing table depends on this.
2. **Agent-to-display API**: has Meta published the push path yet? If not, the stub interface
   in `04-agent-integration.md` stands and integration waits.
3. **Backlight toggle in SDK settings UI** vs always-on for v1 — owner's call; default to
   always-on unless he asks for the toggle.
4. **Celebration animation style**: the two-second top-hat Muse animation is specified, but
   the exact artwork is up to whoever implements it — keep it simple and charming.
5. **Protein/fiber items**: hidden until the MyFitnessPal connection exists. Should they be
   hidden entirely, or shown greyed-out as "coming soon"? Ask before implementing.

# Build, flash, and iterate

- Toolchain: ESP-IDF v6.0.1. Build with `idf.py build` or `tools/board.sh <board> build`.
- The SDK's `AGENTS.md` is the authoritative build/flash reference — read it before touching
  the build system.
- UI iteration: use `esp32/simulator/` on desktop; it runs the same UI code without hardware.
- Hardware iteration: once the board arrives, flash the board-support port first (acceptance
  criteria in `02-board-support-port.md`), then the todo UI.
- Commit small and working. The `main` branch should always build.
