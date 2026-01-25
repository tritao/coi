#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

OUT_DIR="${TMPDIR:-/tmp}/coi-side-by-side"
declare -a SCENE_PATTERNS=()
SET_NAME=""
RUN=1
OPEN_AFTER=0

usage() {
  cat <<EOF
Usage:
  $0 (--scene <name|glob> | --set <name>) [--out <dir>] [--no-run] [--open]

Examples:
  $0 --scene paint_rects
  $0 --scene layout_* --out /tmp/coi-gallery
  $0 --set golden

Outputs:
  <out>/desktop/<scene>/frame_*.png
  <out>/web/<scene>/frame_*.png
  <out>/index.html
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0;;
    --scene) SCENE_PATTERNS+=("${2:-}"); shift 2;;
    --set) SET_NAME="${2:-}"; shift 2;;
    --out) OUT_DIR="${2:-}"; shift 2;;
    --no-run) RUN=0; shift;;
    --open) OPEN_AFTER=1; shift;;
    *) echo "error: unknown arg: $1"; usage; exit 1;;
  esac
done

if [[ -n "$SET_NAME" ]]; then
  set_file="$ROOT_DIR/tests/visual/scene_sets/$SET_NAME.txt"
  if [[ ! -f "$set_file" ]]; then
    echo "error: unknown --set '$SET_NAME' (missing $set_file)"
    exit 1
  fi
  while IFS= read -r raw || [[ -n "$raw" ]]; do
    line="${raw%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    if [[ -z "$line" ]]; then
      continue
    fi
    SCENE_PATTERNS+=("$line")
  done <"$set_file"
fi

if [[ "${#SCENE_PATTERNS[@]}" -eq 0 ]]; then
  echo "error: provide at least one --scene (or use --set golden)"
  exit 1
fi

mkdir -p "$OUT_DIR"

DESKTOP_BASE="$OUT_DIR/desktop"
WEB_BASE="$OUT_DIR/web"

if [[ "$RUN" -eq 1 ]]; then
  for pat in "${SCENE_PATTERNS[@]}"; do
    "$ROOT_DIR/tests/run_visual.sh" --backend desktop --update --scene "$pat" --baseline-dir "$DESKTOP_BASE" --out-dir "$OUT_DIR/.out/desktop"
    "$ROOT_DIR/tests/run_visual.sh" --backend web --update --scene "$pat" --baseline-dir "$WEB_BASE" --out-dir "$OUT_DIR/.out/web"
  done
fi

python3 "$ROOT_DIR/tests/visual/make_gallery.py" --desktop "$DESKTOP_BASE" --web "$WEB_BASE" --out "$OUT_DIR/index.html" --title "COI Desktop vs Web"

echo "wrote: $OUT_DIR/index.html"

if [[ "$OPEN_AFTER" -eq 1 ]]; then
  if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$OUT_DIR/index.html" >/dev/null 2>&1 &
    disown || true
  elif command -v open >/dev/null 2>&1; then
    open "$OUT_DIR/index.html" >/dev/null 2>&1 &
    disown || true
  else
    echo "warn: couldn't open HTML (missing xdg-open/open): $OUT_DIR/index.html" >&2
  fi
fi
