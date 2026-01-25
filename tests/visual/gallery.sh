#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

OUT_DIR="${TMPDIR:-/tmp}/coi-side-by-side"
SCENE_PATTERN=""
RUN=1

usage() {
  cat <<EOF
Usage:
  $0 --scene <name|glob> [--out <dir>] [--no-run]

Examples:
  $0 --scene paint_rects
  $0 --scene layout_* --out /tmp/coi-gallery

Outputs:
  <out>/desktop/<scene>/frame_*.png
  <out>/web/<scene>/frame_*.png
  <out>/index.html
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0;;
    --scene) SCENE_PATTERN="${2:-}"; shift 2;;
    --out) OUT_DIR="${2:-}"; shift 2;;
    --no-run) RUN=0; shift;;
    *) echo "error: unknown arg: $1"; usage; exit 1;;
  esac
done

if [[ -z "$SCENE_PATTERN" ]]; then
  echo "error: --scene is required (use a glob like layout_*)"
  exit 1
fi

mkdir -p "$OUT_DIR"

DESKTOP_BASE="$OUT_DIR/desktop"
WEB_BASE="$OUT_DIR/web"

if [[ "$RUN" -eq 1 ]]; then
  "$ROOT_DIR/tests/run_visual.sh" --backend desktop --update --scene "$SCENE_PATTERN" --baseline-dir "$DESKTOP_BASE" --out-dir "$OUT_DIR/.out/desktop"
  "$ROOT_DIR/tests/run_visual.sh" --backend web --update --scene "$SCENE_PATTERN" --baseline-dir "$WEB_BASE" --out-dir "$OUT_DIR/.out/web"
fi

python3 "$ROOT_DIR/tests/visual/make_gallery.py" --desktop "$DESKTOP_BASE" --web "$WEB_BASE" --out "$OUT_DIR/index.html" --title "COI Desktop vs Web"

echo "wrote: $OUT_DIR/index.html"

