# Desktop visual regression (PNG capture)

COI’s desktop runtime can capture screenshots and a tolerant 64‑bit dHash per frame.

On Linux the default capture mode is `COI_DESKTOP_CAPTURE_MODE=x11` (read back pixels from the swapchain framebuffer via `glReadPixels`) because it works reliably in headless setups (including `xvfb-run`). You can force offscreen rendering with `COI_DESKTOP_CAPTURE_MODE=offscreen`.

## Create a baseline

```bash
./coi run tests/desktop/font_window_demo.coi --target desktop --window --frames 120 \
  --capture tests/desktop/_baseline/font_window_demo \
  --capture-size 960x540 \
  --capture-every 60 \
  --capture-max 2
```

This writes:
- `tests/desktop/_baseline/font_window_demo/frame_000000.png`
- `tests/desktop/_baseline/font_window_demo/frame_000000.dhash`
- `tests/desktop/_baseline/font_window_demo/frame_000001.png`
- `tests/desktop/_baseline/font_window_demo/frame_000001.dhash`

## Compare against a baseline

```bash
./coi run tests/desktop/font_window_demo.coi --target desktop --window --frames 120 \
  --capture /tmp/coi-captures/font_window_demo \
  --capture-size 960x540 \
  --capture-every 60 \
  --capture-max 2 \
  --capture-baseline tests/desktop/_baseline/font_window_demo \
  --capture-tolerance 8 \
  --capture-fail
```

Notes:
- Tolerance is the max Hamming distance of `dHash(capture) ^ dHash(baseline)`.
- `--capture-fail` requests the window to quit on mismatch and returns non‑zero.

## Script

Use `tests/run_visual_desktop.sh` to run/update baselines:

```bash
./tests/run_visual_desktop.sh --update
./tests/run_visual_desktop.sh
```
