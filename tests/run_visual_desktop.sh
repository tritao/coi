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
UI_MODE="headless" # headless | window
CAPTURE_MODE="offscreen" # offscreen | x11 | auto

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
  --ci                        CI-friendly defaults (headless + capture-mode=auto)
  --window                    Run scenes with a visible window (default: headless)
  --headless                  Run scenes headlessly (default)
  --capture-mode <mode>       Capture mode: offscreen|x11|auto (default: offscreen)
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
    --ci) UI_MODE="headless"; CAPTURE_MODE="auto"; USE_XVFB="auto"; shift;;
    --window) UI_MODE="window"; shift;;
    --headless) UI_MODE="headless"; shift;;
    --capture-mode) CAPTURE_MODE="${2:-}"; shift 2;;
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

if [[ "$CAPTURE_MODE" != "offscreen" && "$CAPTURE_MODE" != "x11" && "$CAPTURE_MODE" != "auto" ]]; then
  echo "error: invalid --capture-mode: $CAPTURE_MODE (expected: offscreen|x11|auto)"
  exit 1
fi

scenes=(
  "font_window_demo:$ROOT_DIR/tests/desktop/visual/scenes/font_window_demo.coi"
  "paint_rects:$ROOT_DIR/tests/desktop/visual/scenes/paint/rects.coi"
  "paint_border_widths:$ROOT_DIR/tests/desktop/visual/scenes/paint/border_widths.coi"
  "paint_bg_none:$ROOT_DIR/tests/desktop/visual/scenes/paint/bg_none.coi"
  "paint_corner_radius_basic:$ROOT_DIR/tests/desktop/visual/scenes/paint/corner_radius_basic.coi"
  "paint_corner_radius_per_corner:$ROOT_DIR/tests/desktop/visual/scenes/paint/corner_radius_per_corner.coi"
  "paint_opacity_overlap:$ROOT_DIR/tests/desktop/visual/scenes/paint/opacity_overlap.coi"
  "paint_border_radius_combo:$ROOT_DIR/tests/desktop/visual/scenes/paint/border_radius_combo.coi"
  "paint_floating_zindex:$ROOT_DIR/tests/desktop/visual/scenes/paint/floating_zindex.coi"
  "paint_image_basic:$ROOT_DIR/tests/desktop/visual/scenes/paint/image_basic.coi"
  "paint_image_clip_radius_edges:$ROOT_DIR/tests/desktop/visual/scenes/paint/image_clip_radius_edges.coi"
  "layout_row_fixed_and_grow:$ROOT_DIR/tests/desktop/visual/scenes/layout/row_fixed_and_grow.coi"
  "layout_col_fixed_and_grow:$ROOT_DIR/tests/desktop/visual/scenes/layout/col_fixed_and_grow.coi"
  "layout_nested_flex:$ROOT_DIR/tests/desktop/visual/scenes/layout/nested_flex.coi"
  "layout_min_max_constraints:$ROOT_DIR/tests/desktop/visual/scenes/layout/min_max_constraints.coi"
  "layout_align_main_cross:$ROOT_DIR/tests/desktop/visual/scenes/layout/align_main_cross.coi"
  "clip_overflow_hidden:$ROOT_DIR/tests/desktop/visual/scenes/clip/overflow_hidden.coi"
  "scroll_y_basic:$ROOT_DIR/tests/desktop/visual/scenes/scroll/scroll_y_basic.coi"
  "scroll_nested_clip:$ROOT_DIR/tests/desktop/visual/scenes/scroll/nested_clip.coi"
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

have_xvfb=0
if command -v xvfb-run >/dev/null 2>&1; then
  have_xvfb=1
fi

is_linux=0
if [[ "$(uname -s)" == "Linux" ]]; then
  is_linux=1
fi

ui_args=(--headless)
if [[ "$UI_MODE" == "window" ]]; then
  ui_args=(--window)
fi

run_scene() {
  local mode="$1"    # update | compare
  local scene="$2"
  local path="$3"
  local out_dir="$4"
  local base_dir="$5"
  shift 5
  local -a extra_env_local=("$@")

  local cap_mode="$CAPTURE_MODE"
  local ui_mode="$UI_MODE"

  # Resolve auto capture mode:
  # - prefer headless+offscreen (works without DISPLAY)
  # - fallback to window+x11 if the first attempt fails
  local -a attempts=()
  if [[ "$cap_mode" == "auto" ]]; then
    attempts=("offscreen:$ui_mode" "x11:window")
  else
    attempts=("$cap_mode:$ui_mode")
  fi

  local errexit_was_set=0
  case "$-" in
    *e*) errexit_was_set=1;;
  esac

  local attempt_rc=1
  local attempt_idx=0
  for entry in "${attempts[@]}"; do
    attempt_idx=$((attempt_idx + 1))
    local try_cap="${entry%%:*}"
    local try_ui="${entry#*:}"
    local -a try_ui_args=(--headless)
    if [[ "$try_ui" == "window" || "$try_cap" == "x11" ]]; then
      try_ui_args=(--window)
    fi

    local -a try_prefix=()
    if [[ "$USE_XVFB" == "1" ]]; then
      if [[ "$have_xvfb" -eq 1 ]]; then
        try_prefix=(xvfb-run -a)
      else
        echo "error: --xvfb requested but xvfb-run not found"
        return 1
      fi
    elif [[ "$USE_XVFB" == "auto" ]]; then
      if [[ "$is_linux" -eq 1 && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        if [[ "$have_xvfb" -eq 1 ]]; then
          try_prefix=(xvfb-run -a)
        else
          echo "error: DISPLAY/WAYLAND_DISPLAY not set and xvfb-run not found (install xvfb, set DISPLAY, or use --xvfb)"
          continue
        fi
      fi
    elif [[ "$USE_XVFB" == "0" ]]; then
      if [[ "$is_linux" -eq 1 && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        echo "error: --no-xvfb set but DISPLAY/WAYLAND_DISPLAY is not set"
        continue
      fi
    fi

    echo "   run: ui=$try_ui capture=$try_cap (attempt $attempt_idx/${#attempts[@]})"
    local -a cap_env=(env COI_DESKTOP_CAPTURE_MODE="$try_cap")

    # Ensure we don't mix outputs across attempts.
    rm -f "$out_dir"/*.png "$out_dir"/*.dhash 2>/dev/null || true

    set +e
    if [[ "$mode" == "update" ]]; then
      "${try_prefix[@]}" "${cap_env[@]}" env "${extra_env_local[@]}" "$COI_BIN" run "$path" --target desktop "${try_ui_args[@]}" --frames "$FRAMES" \
        --capture "$out_dir" --capture-size "$CAPTURE_SIZE" --capture-every "$EVERY" --capture-max "$MAX_CAPTURES"
      attempt_rc=$?
    else
      "${try_prefix[@]}" "${cap_env[@]}" env "${extra_env_local[@]}" "$COI_BIN" run "$path" --target desktop "${try_ui_args[@]}" --frames "$FRAMES" \
        --capture "$out_dir" --capture-size "$CAPTURE_SIZE" --capture-every "$EVERY" --capture-max "$MAX_CAPTURES" \
        --capture-baseline "$base_dir" --capture-tolerance "$TOLERANCE" --capture-fail
      attempt_rc=$?
    fi
    if [[ "$errexit_was_set" -eq 1 ]]; then
      set -e
    fi

    if [[ "$attempt_rc" -eq 0 ]]; then
      return 0
    fi

    if [[ "$cap_mode" != "auto" ]]; then
      return "$attempt_rc"
    fi

    echo "   retrying with fallback capture mode..."
  done

  return "$attempt_rc"
}

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
    run_scene "update" "$name" "$path" "$scene_out" "$scene_base" "${extra_env[@]}"

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
  run_scene "compare" "$name" "$path" "$scene_out" "$scene_base" "${extra_env[@]}"
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
