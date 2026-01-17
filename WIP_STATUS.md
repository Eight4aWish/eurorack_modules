WIP Status — eurorack_firmware
Date: 2025-11-19

Summary
- Shared UI library created: `libs/eurorack_ui` with `OledHelpers` and `OledHomeMenu`.
- Pico2W integration: `src/pico2w_oc/main.cpp` uses the shared UI, home menu, and has a `Diag` patch.
- Diag updated to show compact numeric values: ADS raw (A0/A1), pots raw, and MCP last-written codes.
- Added a lightweight `Clock` patch (functions: `clock_enter`, `clock_tick`, `clock_render`) and registered it in the `Util` bank so it's selectable from the home menu.
- Fixed build issues (moved `patchShortPressed` declaration) and successfully built+uploaded `pico2w_oc` (UF2 generated and flashed).

Files changed
- `src/pico2w_oc/main.cpp` — added `Clock` patch, updated bank registration, adjusted diag, added MCP mirror array.
- `libs/eurorack_ui/*` — shared UI helpers (existing from earlier work).

Current status
- Firmware built & flashed successfully to device (pico2w_oc environment).
- `Clock` is selectable from the home menu; core behavior implemented but untested on hardware by me.

How to reproduce
- Build: `pio run -e pico2w_oc`
- Build+Upload: `pio run -e pico2w_oc -t upload`
- Serial monitor: `pio device monitor -e pico2w_oc`

Next steps (recommended)
1. Test `Clock` on hardware: select from home menu and verify outputs D0..D3 and OLED behavior.
2. Tune detection: adjust `delta` threshold in `clock_tick()` and pulse length (currently 10 ms).
3. (Optional) Add calibration persistence similar to Ornament & Crime if you want DAC/ADC calibration.
4. Add lightweight serial debug prints while testing to observe edge detection and intervals.

Notes
- `Clock` currently maps three pots to a small division table and mirrors channel 0 to channel 3.
- ADS reads use `ads.readADC_SingleEnded(0)`; external clock detection is a simple rising-edge delta test (heuristic value `delta = 4000`).
- MCP outputs use `mcp.fastWrite(...)` and `mcp_values[]` contains last outputs for display.

If you want, I can run the serial monitor now and exercise the Clock patch while you test the module, or I can add runtime debug prints in `clock_tick()` to make the behaviour more visible in the monitor.
