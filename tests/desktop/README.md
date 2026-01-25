# Desktop tests

This folder contains tests for COI’s experimental `--target desktop`.

## Layout

- `compile/`: compiler-only checks (mostly `*_fail.coi`).
- `runtime/`: desktop runtime golden-output tests (`*_pass.coi`, `*_fail.coi` + sidecar files like `.expected.txt`, `.dump`, `.viewport`, etc.).
- `visual/`: visual regression tests
  - `visual/scenes/`: deterministic screenshot scenes (`*_visual.coi`).
  - Baselines live in `tests/visual/baseline/desktop/` (PNG + `.dhash` per scene).

## Running

- Runtime/compile tests: `./tests/run_desktop.sh`
- Visual regression (desktop): `./tests/run_visual.sh --backend desktop` (or `./tests/run_visual.sh --backend desktop --update`)
