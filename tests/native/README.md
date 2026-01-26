# Native tests

This folder contains tests for COI’s experimental `--target native`.

## Layout

- `compile/`: compiler-only checks (mostly `*_fail.coi`).
- `runtime/`: native runtime golden-output tests (`*_pass.coi`, `*_fail.coi` + sidecar files like `.expected.txt`, `.dump`, `.viewport`, etc.).
- `visual/`: visual regression tests
  - `visual/scenes/`: deterministic screenshot scenes (`*_visual.coi`).
  - Baselines live in `tests/visual/baseline/native/` (PNG + `.dhash` per scene).

## Running

- Runtime/compile tests: `./tests/run_native.sh`
- Visual regression (native): `./tests/run_visual.sh --backend native` (or `./tests/run_visual.sh --backend native --update`)
