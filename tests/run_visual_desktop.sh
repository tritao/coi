#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COI_BIN="$ROOT_DIR/coi"

if [[ ! -x "$COI_BIN" ]]; then
  echo "error: expected executable at $COI_BIN"
  echo "hint: build coi first"
  exit 1
fi

MODE="compare" # compare | update
SCENE_FILTER=""

BASELINE_DIR="$ROOT_DIR/tests/desktop/_baseline"
OUT_DIR="${TMPDIR:-/tmp}/coi-visual-desktop"

CAPTURE_SIZE="960x540"
FRAMES="120"
EVERY="60"
MAX_CAPTURES="2"
TOLERANCE="8"

USE_XVFB="auto" # auto | 0 | 1

usage() {
  cat <<EOF
Usage:
  $0 [--update] [--scene <name>] [options]

Options:
  --update                    Write baselines into tests/desktop/_baseline/<scene>/
  --scene <name>              Run only one scene
  --baseline-dir <dir>        Baseline directory (default: $BASELINE_DIR)
  --out-dir <dir>             Capture output directory (default: $OUT_DIR)
  --size <WxH>                Capture size (default: $CAPTURE_SIZE)
  --frames <n>                Total frames to run (default: $FRAMES)
  --every <n>                 Capture every N frames (default: $EVERY)
  --max <n>                   Max captures per run (default: $MAX_CAPTURES)
  --tolerance <n>             dHash tolerance (default: $TOLERANCE)
  --xvfb / --no-xvfb          Force/disable xvfb-run wrapper
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0;;
    --update) MODE="update"; shift;;
    --scene) SCENE_FILTER="${2:-}"; shift 2;;
    --baseline-dir) BASELINE_DIR="${2:-}"; shift 2;;
    --out-dir) OUT_DIR="${2:-}"; shift 2;;
    --size) CAPTURE_SIZE="${2:-}"; shift 2;;
    --frames) FRAMES="${2:-}"; shift 2;;
    --every) EVERY="${2:-}"; shift 2;;
    --max) MAX_CAPTURES="${2:-}"; shift 2;;
    --tolerance) TOLERANCE="${2:-}"; shift 2;;
    --xvfb) USE_XVFB="1"; shift;;
    --no-xvfb) USE_XVFB="0"; shift;;
    *) echo "error: unknown arg: $1"; usage; exit 1;;
  esac
done

scenes=(
  "font_window_demo:$ROOT_DIR/tests/desktop/font_window_demo.coi"
)

need_xvfb=0
if [[ "$USE_XVFB" == "1" ]]; then
  need_xvfb=1
elif [[ "$USE_XVFB" == "auto" ]]; then
  if [[ -z "${DISPLAY:-}" ]]; then
    need_xvfb=1
  fi
fi

run_cmd_prefix=()
if [[ "$need_xvfb" -eq 1 ]]; then
  if command -v xvfb-run >/dev/null 2>&1; then
    run_cmd_prefix=(xvfb-run -a)
  else
    echo "error: DISPLAY is not set and xvfb-run not found"
    echo "hint: install xvfb, or run with DISPLAY, or pass --no-xvfb if you know you have a headless GL context"
    exit 1
  fi
fi

mkdir -p "$OUT_DIR"
mkdir -p "$BASELINE_DIR"

fail=0

for entry in "${scenes[@]}"; do
  name="${entry%%:*}"
  path="${entry#*:}"

  if [[ -n "$SCENE_FILTER" && "$name" != "$SCENE_FILTER" ]]; then
    continue
  fi

  echo "==> scene: $name"
  scene_out="$OUT_DIR/$name"
  scene_base="$BASELINE_DIR/$name"

  rm -rf "$scene_out"
  mkdir -p "$scene_out"

  if [[ "$MODE" == "update" ]]; then
    mkdir -p "$scene_base"
    "${run_cmd_prefix[@]}" "$COI_BIN" run "$path" --target desktop --window --frames "$FRAMES" \
      --capture "$scene_out" --capture-size "$CAPTURE_SIZE" --capture-every "$EVERY" --capture-max "$MAX_CAPTURES"

    rm -rf "$scene_base"
    mkdir -p "$scene_base"
    cp -f "$scene_out"/*.dhash "$scene_base/" 2>/dev/null || true
    cp -f "$scene_out"/*.png "$scene_base/" 2>/dev/null || true

    echo "   updated baseline: $scene_base"
    continue
  fi

  if [[ ! -d "$scene_base" ]]; then
    echo "error: missing baseline dir: $scene_base"
    echo "hint: run: $0 --update --scene $name"
    fail=1
    continue
  fi

  set +e
  "${run_cmd_prefix[@]}" "$COI_BIN" run "$path" --target desktop --window --frames "$FRAMES" \
    --capture "$scene_out" --capture-size "$CAPTURE_SIZE" --capture-every "$EVERY" --capture-max "$MAX_CAPTURES" \
    --capture-baseline "$scene_base" --capture-tolerance "$TOLERANCE" --capture-fail
  rc=$?
  set -e

  if [[ "$rc" -ne 0 ]]; then
    echo "   FAIL (rc=$rc): $name"
    echo "   captures: $scene_out"
    fail=1
  else
    echo "   OK: $name"
  fi
done

exit "$fail"

