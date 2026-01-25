#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COI_BIN="$ROOT_DIR/coi"

if [[ ! -x "$COI_BIN" ]]; then
  echo "error: expected executable at $COI_BIN"
  echo "hint: build coi first"
  exit 1
fi

# Ensure the binary supports the capture flags (developers often forget to rebuild ./coi after changes).
if ! "$COI_BIN" --help 2>/dev/null | grep -q -- "--capture"; then
  echo "[visual] ./coi doesn't support --capture yet; rebuilding..."
  if [[ ! -x "$ROOT_DIR/build.sh" ]]; then
    echo "error: missing build script: $ROOT_DIR/build.sh"
    exit 1
  fi
  "$ROOT_DIR/build.sh" >/dev/null
  if ! "$COI_BIN" --help 2>/dev/null | grep -q -- "--capture"; then
    echo "error: ./coi still doesn't advertise --capture after rebuild"
    echo "hint: run: $ROOT_DIR/build.sh"
    exit 1
  fi
fi

MODE="compare" # compare | update
SCENE_FILTER=""
LIST_ONLY=0

BASELINE_DIR="$ROOT_DIR/tests/desktop/visual/baseline"
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
  --update                    Write baselines into tests/desktop/visual/baseline/<scene>/
  --scene <name>              Run only one scene (exact), or use prefix/glob (e.g. layout_*, layout_)
  --list                      List available scenes (honors --scene filter)
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
    --list) LIST_ONLY=1; shift;;
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
  "font_window_demo:$ROOT_DIR/tests/desktop/visual/scenes/font_window_demo.coi"
  "paint_rects:$ROOT_DIR/tests/desktop/visual/scenes/paint_rects_visual.coi"
  "paint_border_widths:$ROOT_DIR/tests/desktop/visual/scenes/paint_border_widths_visual.coi"
  "paint_bg_none:$ROOT_DIR/tests/desktop/visual/scenes/paint_bg_none_visual.coi"
  "layout_row_fixed_and_grow:$ROOT_DIR/tests/desktop/visual/scenes/layout/row_fixed_and_grow.coi"
  "layout_col_fixed_and_grow:$ROOT_DIR/tests/desktop/visual/scenes/layout/col_fixed_and_grow.coi"
  "layout_nested_flex:$ROOT_DIR/tests/desktop/visual/scenes/layout/nested_flex.coi"
  "layout_min_max_constraints:$ROOT_DIR/tests/desktop/visual/scenes/layout/min_max_constraints.coi"
  "layout_align_main_cross:$ROOT_DIR/tests/desktop/visual/scenes/layout/align_main_cross.coi"
  "clip_overflow_hidden:$ROOT_DIR/tests/desktop/visual/scenes/clip/overflow_hidden.coi"
  "scroll_y_basic:$ROOT_DIR/tests/desktop/visual/scenes/scroll/scroll_y_basic.coi"
  "text_glyphs_basic:$ROOT_DIR/tests/desktop/visual/scenes/text/glyphs_basic.coi"
  "text_wrap_measure:$ROOT_DIR/tests/desktop/visual/scenes/text/wrap_measure.coi"
  "text_alignment:$ROOT_DIR/tests/desktop/visual/scenes/text/alignment.coi"
  "border_render_dump_pass:$ROOT_DIR/tests/desktop/runtime/border_render_dump_pass.coi"
  "border_between_render_dump_pass:$ROOT_DIR/tests/desktop/runtime/border_between_render_dump_pass.coi"
  "clip_render_dump_pass:$ROOT_DIR/tests/desktop/runtime/clip_render_dump_pass.coi"
  "scroll_render_dump_pass:$ROOT_DIR/tests/desktop/runtime/scroll_render_dump_pass.coi"
  "text_render_dump_pass:$ROOT_DIR/tests/desktop/runtime/text_render_dump_pass.coi"
)

SCENE_PATTERN="$SCENE_FILTER"
if [[ -n "$SCENE_PATTERN" ]]; then
  # If the filter doesn't contain glob metacharacters and doesn't exactly match any scene,
  # treat it as a prefix.
  has_glob=0
  case "$SCENE_PATTERN" in
    *"*"*|*"?"*|*"["*) has_glob=1;;
  esac
  if [[ "$has_glob" -eq 0 ]]; then
    exact=0
    for entry in "${scenes[@]}"; do
      name="${entry%%:*}"
      if [[ "$name" == "$SCENE_PATTERN" ]]; then
        exact=1
        break
      fi
    done
    if [[ "$exact" -eq 0 ]]; then
      SCENE_PATTERN="${SCENE_PATTERN}*"
    fi
  fi
fi

if [[ "$LIST_ONLY" -eq 1 ]]; then
  for entry in "${scenes[@]}"; do
    name="${entry%%:*}"
    if [[ -n "$SCENE_PATTERN" && "$name" != $SCENE_PATTERN ]]; then
      continue
    fi
    echo "$name"
  done
  exit 0
fi

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

capture_env_prefix=()
if [[ "$need_xvfb" -eq 1 ]]; then
  # Xvfb's GL stack often can't create offscreen render targets; use X11-based capture instead.
  capture_env_prefix=(env COI_DESKTOP_CAPTURE_MODE=x11)
fi

mkdir -p "$OUT_DIR"
mkdir -p "$BASELINE_DIR"

fail=0

trim_ws() {
  local s="$1"
  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"
  printf '%s' "$s"
}

for entry in "${scenes[@]}"; do
  name="${entry%%:*}"
  path="${entry#*:}"

  if [[ -n "$SCENE_PATTERN" && "$name" != $SCENE_PATTERN ]]; then
    continue
  fi

  echo "==> scene: $name"
  scene_out="$OUT_DIR/$name"
  scene_base="$BASELINE_DIR/$name"

  extra_env=()

  env_file="${path%.coi}.desktop_env"
  if [[ -f "$env_file" ]]; then
    while IFS= read -r raw || [[ -n "$raw" ]]; do
      line="${raw%%#*}"
      line="$(trim_ws "$line")"
      if [[ -z "$line" ]]; then
        continue
      fi
      if [[ "$line" != *=* ]]; then
        echo "error: invalid env line in $env_file: $raw" >&2
        exit 1
      fi
      k="$(trim_ws "${line%%=*}")"
      v="$(trim_ws "${line#*=}")"
      v="${v//\$ROOT_DIR/$ROOT_DIR}"
      extra_env+=("$k=$v")
    done <"$env_file"
  fi

  script_file="${path%.coi}.desktop_script"
  if [[ -f "$script_file" ]]; then
    extra_env+=("COI_DESKTOP_SCRIPT=$script_file")
    has_dumps=0
    for kv in "${extra_env[@]}"; do
      if [[ "$kv" == COI_DESKTOP_SCRIPT_DUMPS=* ]]; then
        has_dumps=1
        break
      fi
    done
    if [[ "$has_dumps" -eq 0 ]]; then
      extra_env+=("COI_DESKTOP_SCRIPT_DUMPS=0")
    fi
  fi

  rm -rf "$scene_out"
  mkdir -p "$scene_out"

  if [[ "$MODE" == "update" ]]; then
    mkdir -p "$scene_base"
    "${run_cmd_prefix[@]}" "${capture_env_prefix[@]}" env "${extra_env[@]}" "$COI_BIN" run "$path" --target desktop --window --frames "$FRAMES" \
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
  "${run_cmd_prefix[@]}" "${capture_env_prefix[@]}" env "${extra_env[@]}" "$COI_BIN" run "$path" --target desktop --window --frames "$FRAMES" \
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
