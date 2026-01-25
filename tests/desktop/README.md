# Desktop tests

This folder contains tests for COI’s experimental `--target desktop`.

## Layout

- `compile/`: compiler-only checks (mostly `*_fail.coi`).
- `runtime/`: desktop runtime golden-output tests (`*_pass.coi`, `*_fail.coi` + sidecar files like `.expected.txt`, `.dump`, `.viewport`, etc.).
- `visual/`: visual regression tests
  - `visual/scenes/`: deterministic screenshot scenes (`*_visual.coi`).
  - `visual/baseline/`: PNG + `.dhash` baselines per scene.

## Running

- Runtime/compile tests: `./tests/run_desktop.sh`
- Visual regression: `./tests/run_visual_desktop.sh` (or `./tests/run_visual_desktop.sh --update`)

