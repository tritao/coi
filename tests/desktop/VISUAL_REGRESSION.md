# Desktop visual regression (PNG capture)

COI’s desktop runtime can capture deterministic offscreen screenshots and a tolerant 64‑bit dHash per frame.

## Create a baseline

```bash
./coi run tests/desktop/font_window_demo.coi --target desktop --window --frames 120 \
  --capture tests/desktop/_baseline/font_window_demo \
  --capture-size 960x540 \
  --capture-every 60
```

This writes:
- `tests/desktop/_baseline/font_window_demo/frame_000000.png`
- `tests/desktop/_baseline/font_window_demo/frame_000000.dhash`
- `tests/desktop/_baseline/font_window_demo/frame_000060.png`
- `tests/desktop/_baseline/font_window_demo/frame_000060.dhash`

## Compare against a baseline

```bash
./coi run tests/desktop/font_window_demo.coi --target desktop --window --frames 120 \
  --capture /tmp/coi-captures/font_window_demo \
  --capture-size 960x540 \
  --capture-every 60 \
  --capture-baseline tests/desktop/_baseline/font_window_demo \
  --capture-tolerance 8 \
  --capture-fail
```

Notes:
- Tolerance is the max Hamming distance of `dHash(capture) ^ dHash(baseline)`.
- `--capture-fail` requests the window to quit on mismatch and returns non‑zero.

