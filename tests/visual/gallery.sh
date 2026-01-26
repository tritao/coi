#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

OUT_DIR="${TMPDIR:-/tmp}/coi-side-by-side"
declare -a SCENE_PATTERNS=()
SET_NAME=""
RUN=1
OPEN_AFTER=0
NATIVE_UI_BACKENDS="clay,rmlui" # comma-separated list (e.g. clay,rmlui or clay or rmlui)

usage() {
  cat <<EOF
Usage:
  $0 (--scene <name|glob> | --set <name>) [--out <dir>] [--native-ui-backends <list>] [--no-run] [--open]

Examples:
  $0 --scene paint_rects
  $0 --scene layout_* --out /tmp/coi-gallery
  $0 --set golden

Outputs:
  <out>/native/<ui-backend>/<scene>/frame_*.png
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
    --native-ui-backends) NATIVE_UI_BACKENDS="${2:-}"; shift 2;;
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

NATIVE_BASE="$OUT_DIR/native"
WEB_BASE="$OUT_DIR/web"

IFS=',' read -r -a NATIVE_UI_LIST <<<"$NATIVE_UI_BACKENDS"
if [[ "${#NATIVE_UI_LIST[@]}" -eq 0 ]]; then
  echo "error: empty --native-ui-backends"
  exit 1
fi

if [[ "$RUN" -eq 1 ]]; then
  for pat in "${SCENE_PATTERNS[@]}"; do
    for ui in "${NATIVE_UI_LIST[@]}"; do
      ui="${ui#"${ui%%[![:space:]]*}"}"
      ui="${ui%"${ui##*[![:space:]]}"}"
      if [[ -z "$ui" ]]; then
        continue
      fi
      "$ROOT_DIR/tests/run_visual.sh" --backend native --native-ui-backend "$ui" --update --scene "$pat" --baseline-dir "$NATIVE_BASE/$ui" --out-dir "$OUT_DIR/.out/native/$ui"
    done
    "$ROOT_DIR/tests/run_visual.sh" --backend web --update --scene "$pat" --baseline-dir "$WEB_BASE" --out-dir "$OUT_DIR/.out/web"
  done
fi

cols=()
for ui in "${NATIVE_UI_LIST[@]}"; do
  ui="${ui#"${ui%%[![:space:]]*}"}"
  ui="${ui%"${ui##*[![:space:]]}"}"
  if [[ -z "$ui" ]]; then
    continue
  fi
  cols+=(--col "native-$ui=$NATIVE_BASE/$ui")
done
cols+=(--col "web=$WEB_BASE")

python3 "$ROOT_DIR/tests/visual/make_gallery.py" "${cols[@]}" --out "$OUT_DIR/index.html" --title "COI Gallery"

echo "wrote: $OUT_DIR/index.html"

if [[ "$OPEN_AFTER" -eq 1 ]]; then
  port="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

  pid_file="$OUT_DIR/.coi_visual_server.pid"
  if [[ -f "$pid_file" ]]; then
    old_pid="$(cat "$pid_file" 2>/dev/null || true)"
    if [[ -n "${old_pid:-}" ]] && kill -0 "$old_pid" >/dev/null 2>&1; then
      kill "$old_pid" >/dev/null 2>&1 || true
    fi
    rm -f "$pid_file" >/dev/null 2>&1 || true
  fi

  python3 -m http.server "$port" --bind 127.0.0.1 --directory "$OUT_DIR" >/dev/null 2>&1 &
  pid="$!"
  echo "$pid" >"$pid_file"

  url="http://127.0.0.1:$port/index.html"
  if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$url" >/dev/null 2>&1 &
    disown || true
  elif command -v open >/dev/null 2>&1; then
    open "$url" >/dev/null 2>&1 &
    disown || true
  else
    echo "warn: couldn't open browser; url: $url" >&2
  fi
  echo "opened: $url"
  echo "server: pid=$pid (stop: kill $pid)"
fi
