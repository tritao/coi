#!/usr/bin/env bash
#
# Shared helpers for web visual + integration runners.
# Intended to be sourced from other scripts.
#

coi_web_open_url() {
  local url="$1"
  if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$url" >/dev/null 2>&1 &
    disown || true
    return 0
  fi
  if command -v open >/dev/null 2>&1; then
    open "$url" >/dev/null 2>&1 &
    disown || true
    return 0
  fi
  return 1
}

coi_web_pick_free_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

coi_web_start_server() {
  local dir="$1"
  local port
  port="$(coi_web_pick_free_port)"
  python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir" >/dev/null 2>&1 &
  echo "$!:$port"
}

coi_web_stop_server() {
  local pid="$1"
  if [[ -n "$pid" ]]; then
    kill "$pid" >/dev/null 2>&1 || true
  fi
}

coi_web_touch_favicon() {
  local build_dir="$1"
  # Avoid noisy "Failed to load resource" 404 from Chrome's default /favicon.ico request.
  : >"$build_dir/favicon.ico"
}

coi_web_serve_dir_and_open() {
  local dir="$1"
  local html_file="$2"

  local srv
  srv="$(coi_web_start_server "$dir")"
  local pid="${srv%%:*}"
  local port="${srv#*:}"

  local url="http://127.0.0.1:$port/${html_file}"
  if ! coi_web_open_url "$url"; then
    echo "warn: couldn't open browser; url: $url" >&2
  else
    echo "opened: $url"
  fi
  echo "server: pid=$pid (stop: kill $pid)"
}

coi_web_pick_script_file() {
  local backend="$1"
  local path="$2"
  local base="${path%.coi}"

  if [[ -f "${base}.visual_script" ]]; then
    echo "${base}.visual_script"
  elif [[ -f "${base}.${backend}_script" ]]; then
    echo "${base}.${backend}_script"
  elif [[ -f "${base}.web_script" ]]; then
    echo "${base}.web_script"
  elif [[ -f "${base}.native_script" ]]; then
    # Allow reusing native scripts on web via gen_web_visual_js.py
    echo "${base}.native_script"
  else
    echo ""
  fi
}

coi_web_inject_visual_js() {
  local root_dir="$1"
  local build_dir="$2"
  local script_file="$3"

  if [[ -z "$script_file" ]]; then
    return 0
  fi

  python3 "$root_dir/tests/visual/gen_web_visual_js.py" --script "$script_file" --out "$build_dir/coi_visual.js"

  python3 - "$build_dir" <<'PY'
from pathlib import Path
import sys

build_dir = Path(sys.argv[1])
index = build_dir / "index.html"
html = index.read_text(encoding="utf-8")
if "coi_visual.js" in html:
    sys.exit(0)

needle = "<body>"
if needle not in html:
    raise SystemExit(f"error: expected {needle} in {index}")

html = html.replace(needle, needle + "\n    <script src=\"coi_visual.js\"></script>", 1)
index.write_text(html, encoding="utf-8")
PY
}
