#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COI_BIN="$ROOT_DIR/coi"
MANIFEST="$ROOT_DIR/tests/visual/scenes_manifest.txt"

if [[ ! -x "$COI_BIN" ]]; then
  echo "error: expected executable at $COI_BIN"
  echo "hint: build coi first"
  exit 1
fi

BACKEND="desktop" # desktop | web
MODE="compare"    # compare | update
SCENE_FILTER=""
LIST_ONLY=0
OPEN_AFTER=0

# Common
BASELINE_DIR=""
OUT_DIR=""
CAPTURE_SIZE="960x540"
FRAMES="120"
EVERY="60"
MAX_CAPTURES="2"
TOLERANCE="8"

# Desktop-only knobs (kept for compatibility)
UI_MODE="headless"       # headless | window
CAPTURE_MODE="offscreen" # offscreen | x11 | auto
USE_XVFB="auto"          # auto | 0 | 1

# Web-only knobs
WEB_DRIVER="playwright" # playwright | chrome
WEB_BROWSER="${WEB_BROWSER:-$(command -v google-chrome || true)}"

usage() {
  cat <<EOF
Usage:
  $0 [--backend <desktop|web>] [--update] [--scene <name>] [options]

Options:
  --backend <desktop|web>      Backend runner (default: $BACKEND)
  --update                     Write baselines into tests/visual/baseline/<backend>/<scene>/
  --scene <name>               Run only one scene (exact), or use prefix/glob (e.g. layout_*, layout_)
  --list                       List available scenes (honors --scene filter)
  --ci                         CI-friendly defaults (desktop: headless+auto, web: headless chrome)
  --open                       Serve and open an HTML view of captures

Common capture:
  --baseline-dir <dir>         Baseline directory (default: tests/visual/baseline/<backend>)
  --out-dir <dir>              Capture output directory (default: \$TMPDIR/coi-visual/<backend>)
  --size <WxH>                 Capture size (default: $CAPTURE_SIZE)
  --frames <n>                 Total frames (desktop) (default: $FRAMES)
  --every <n>                  Capture every N frames (desktop) (default: $EVERY)
  --max <n>                    Max captures per run (default: $MAX_CAPTURES)
  --tolerance <n>              dHash tolerance (default: $TOLERANCE)

Desktop-specific:
  --window                     Run scenes with a visible window (default: headless)
  --headless                   Run scenes headlessly (default)
  --capture-mode <mode>        Capture mode: offscreen|x11|auto (default: $CAPTURE_MODE)
  --xvfb / --no-xvfb           Force/disable xvfb-run wrapper

Web-specific:
  --web-driver <playwright|chrome> Web driver (default: $WEB_DRIVER)
  --browser <path>             Browser binary (default: $WEB_BROWSER)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0;;
    --backend) BACKEND="${2:-}"; shift 2;;
    --update) MODE="update"; shift;;
    --scene) SCENE_FILTER="${2:-}"; shift 2;;
    --list) LIST_ONLY=1; shift;;
    --open) OPEN_AFTER=1; shift;;
    --ci)
      if [[ "$BACKEND" == "desktop" ]]; then
        UI_MODE="headless"; CAPTURE_MODE="auto"; USE_XVFB="auto"
      fi
      shift
      ;;
    --baseline-dir) BASELINE_DIR="${2:-}"; shift 2;;
    --out-dir) OUT_DIR="${2:-}"; shift 2;;
    --size) CAPTURE_SIZE="${2:-}"; shift 2;;
    --frames) FRAMES="${2:-}"; shift 2;;
    --every) EVERY="${2:-}"; shift 2;;
    --max) MAX_CAPTURES="${2:-}"; shift 2;;
    --tolerance) TOLERANCE="${2:-}"; shift 2;;

    --window) UI_MODE="window"; shift;;
    --headless) UI_MODE="headless"; shift;;
    --capture-mode) CAPTURE_MODE="${2:-}"; shift 2;;
    --xvfb) USE_XVFB="1"; shift;;
    --no-xvfb) USE_XVFB="0"; shift;;

    --web-driver) WEB_DRIVER="${2:-}"; shift 2;;
    --browser) WEB_BROWSER="${2:-}"; shift 2;;
    *) echo "error: unknown arg: $1"; usage; exit 1;;
  esac
done

if [[ "$BACKEND" != "desktop" && "$BACKEND" != "web" ]]; then
  echo "error: invalid --backend: $BACKEND (expected: desktop|web)"
  exit 1
fi

if [[ -z "$BASELINE_DIR" ]]; then
  BASELINE_DIR="$ROOT_DIR/tests/visual/baseline/$BACKEND"
fi
if [[ -z "$OUT_DIR" ]]; then
  OUT_DIR="${TMPDIR:-/tmp}/coi-visual/$BACKEND"
fi

if [[ ! -f "$MANIFEST" ]]; then
  echo "error: missing manifest: $MANIFEST"
  exit 1
fi

trim_ws() {
  local s="$1"
  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"
  printf '%s' "$s"
}

load_scenes() {
  local -n _names=$1
  local -n _paths=$2
  local -n _backends=$3
  _names=()
  _paths=()
  _backends=()
  while IFS= read -r raw || [[ -n "$raw" ]]; do
    line="${raw%%#*}"
    line="$(trim_ws "$line")"
    if [[ -z "$line" ]]; then
      continue
    fi
    IFS="|" read -r name path backends <<<"$line"
    name="$(trim_ws "$name")"
    path="$(trim_ws "$path")"
    backends="$(trim_ws "$backends")"
    if [[ -z "$name" || -z "$path" || -z "$backends" ]]; then
      echo "error: invalid manifest line: $raw" >&2
      exit 1
    fi
    _names+=("$name")
    _paths+=("$path")
    _backends+=("$backends")
  done <"$MANIFEST"
}

names=()
paths=()
backends=()
load_scenes names paths backends

SCENE_PATTERN="$SCENE_FILTER"
if [[ -n "$SCENE_PATTERN" ]]; then
  has_glob=0
  case "$SCENE_PATTERN" in
    *"*"*|*"?"*|*"["*) has_glob=1;;
  esac
  if [[ "$has_glob" -eq 0 ]]; then
    exact=0
    for n in "${names[@]}"; do
      if [[ "$n" == "$SCENE_PATTERN" ]]; then
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
  for i in "${!names[@]}"; do
    n="${names[$i]}"
    b="${backends[$i]}"
    if [[ -n "$SCENE_PATTERN" && "$n" != $SCENE_PATTERN ]]; then
      continue
    fi
    case ",$b," in
      *",$BACKEND,"*) echo "$n";;
    esac
  done
  exit 0
fi

mkdir -p "$OUT_DIR"
mkdir -p "$BASELINE_DIR"

open_url() {
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

pick_free_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

serve_dir_and_open() {
  local dir="$1"
  local html_file="$2"
  local pid_file="$dir/.coi_visual_server.pid"

  if [[ ! -d "$dir" ]]; then
    echo "warn: missing output dir: $dir" >&2
    return 1
  fi

  if [[ -f "$pid_file" ]]; then
    old_pid="$(cat "$pid_file" 2>/dev/null || true)"
    if [[ -n "${old_pid:-}" ]] && kill -0 "$old_pid" >/dev/null 2>&1; then
      kill "$old_pid" >/dev/null 2>&1 || true
    fi
    rm -f "$pid_file" >/dev/null 2>&1 || true
  fi

  local port
  port="$(pick_free_port)"

  python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir" >/dev/null 2>&1 &
  local pid="$!"
  echo "$pid" >"$pid_file"

  local url="http://127.0.0.1:$port/${html_file}"
  if ! open_url "$url"; then
    echo "warn: couldn't open browser; url: $url" >&2
  else
    echo "opened: $url"
  fi
  echo "server: pid=$pid (stop: kill $pid)"
}

pick_env_file() {
  local path="$1"
  local base="${path%.coi}"
  if [[ -f "${base}.visual_env" ]]; then
    echo "${base}.visual_env"
  elif [[ -f "${base}.${BACKEND}_env" ]]; then
    echo "${base}.${BACKEND}_env"
  elif [[ -f "${base}.desktop_env" ]]; then
    echo "${base}.desktop_env"
  else
    echo ""
  fi
}

pick_script_file() {
  local path="$1"
  local base="${path%.coi}"
  if [[ -f "${base}.visual_script" ]]; then
    echo "${base}.visual_script"
  elif [[ -f "${base}.${BACKEND}_script" ]]; then
    echo "${base}.${BACKEND}_script"
  elif [[ -f "${base}.desktop_script" ]]; then
    echo "${base}.desktop_script"
  else
    echo ""
  fi
}

read_env_file() {
  local env_file="$1"
  local -n out_env=$2
  out_env=()
  if [[ -z "$env_file" || ! -f "$env_file" ]]; then
    return 0
  fi
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
    out_env+=("$k=$v")
  done <"$env_file"
}

# ---- Desktop runner (mostly identical to the legacy script) ----

run_scene_desktop() {
  local mode="$1" # update | compare
  local path="$2"
  local out_dir="$3"
  local base_dir="$4"
  shift 4
  local -a extra_env_local=("$@")

  if [[ "$CAPTURE_MODE" != "offscreen" && "$CAPTURE_MODE" != "x11" && "$CAPTURE_MODE" != "auto" ]]; then
    echo "error: invalid --capture-mode: $CAPTURE_MODE (expected: offscreen|x11|auto)"
    return 1
  fi

  local cap_mode="$CAPTURE_MODE"
  local ui_mode="$UI_MODE"

  local -a attempts=()
  if [[ "$cap_mode" == "auto" ]]; then
    attempts=("offscreen:$ui_mode" "x11:window")
  else
    attempts=("$cap_mode:$ui_mode")
  fi

  have_xvfb=0
  if command -v xvfb-run >/dev/null 2>&1; then
    have_xvfb=1
  fi

  is_linux=0
  if [[ "$(uname -s)" == "Linux" ]]; then
    is_linux=1
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

# ---- Web runner ----

web_script_max_frame() {
  local script_file="$1"
  awk '
    function trim(s){ sub(/^[ \t\r\n]+/,"",s); sub(/[ \t\r\n]+$/,"",s); return s }
    BEGIN{ f=0; max=0 }
    {
      gsub(/\r/,"")
      sub(/#.*/,"")
      line=trim($0)
      if(line=="") next
      split(line,a,/ +/)
      cmd=a[1]
      if(cmd=="wait" || cmd=="frames"){
        if(a[2] ~ /^[0-9]+$/){ f+=a[2] }
        if(f>max) max=f
      } else {
        if(f>max) max=f
      }
    }
    END{ print max }
  ' "$script_file"
}

web_inject_visual_js() {
  local build_dir="$1"
  local script_file="$2"

  if [[ -z "$script_file" ]]; then
    return 0
  fi

  python3 "$ROOT_DIR/tests/visual/gen_web_visual_js.py" --script "$script_file" --out "$build_dir/coi_visual.js"

  python3 - "$build_dir" <<'PY'
from pathlib import Path
import sys

build_dir = Path(sys.argv[1])
index = build_dir / "index.html"
html = index.read_text(encoding="utf-8")
if "coi_visual.js" in html:
    sys.exit(0)

# Insert right after <body>
needle = "<body>"
if needle not in html:
    raise SystemExit(f"error: expected {needle} in {index}")

html = html.replace(needle, needle + "\n    <script src=\"coi_visual.js\"></script>", 1)
index.write_text(html, encoding="utf-8")
PY
}

web_start_server() {
  local dir="$1"
  local port
  port="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"
  python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir" >/dev/null 2>&1 &
  echo "$!:$port"
}

web_stop_server() {
  local pid="$1"
  if [[ -n "$pid" ]]; then
    kill "$pid" >/dev/null 2>&1 || true
  fi
}

web_capture_one() {
  local url="$1"
  local png_out="$2"
  local _budget_ms="$3"

  if [[ "$WEB_DRIVER" == "playwright" ]]; then
    if [[ ! -f "$ROOT_DIR/tests/visual/node_modules/playwright-core/package.json" ]]; then
      echo "error: missing web deps: tests/visual/node_modules/playwright-core" >&2
      echo "hint: run: (cd tests/visual && npm install)" >&2
      return 1
    fi
    if [[ -z "$WEB_BROWSER" || ! -x "$WEB_BROWSER" ]]; then
      echo "error: web browser not found; set WEB_BROWSER or pass --browser" >&2
      return 1
    fi
    node "$ROOT_DIR/tests/visual/web_capture_playwright.mjs" --url "$url" --out "$png_out" --size "$CAPTURE_SIZE" --timeout-ms 12000 --browser "$WEB_BROWSER"
    return $?
  fi

  # Legacy driver: raw headless Chrome screenshot with virtual-time budget.
  if [[ -z "$WEB_BROWSER" || ! -x "$WEB_BROWSER" ]]; then
    echo "error: web browser not found; set WEB_BROWSER or pass --browser" >&2
    return 1
  fi
  local size_csv="${CAPTURE_SIZE/x/,}"
  "$WEB_BROWSER" \
    --headless=new \
    --disable-gpu \
    --hide-scrollbars \
    --no-sandbox \
    --window-size="$size_csv" \
    --virtual-time-budget="$_budget_ms" \
    --screenshot="$png_out" \
    "$url" >/dev/null 2>&1
}

run_scene_web() {
  local mode="$1" # update | compare
  local name="$2"
  local path="$3"
  local out_dir="$4"
  local base_dir="$5"
  local script_file="$6"

  rm -rf "$out_dir"
  mkdir -p "$out_dir"

  local build_dir="$out_dir/build"
  rm -rf "$build_dir"
  mkdir -p "$build_dir"

  "$COI_BIN" "$path" --target web --out "$build_dir" >/dev/null
  web_inject_visual_js "$build_dir" "$script_file"

  local srv pid port
  srv="$(web_start_server "$build_dir")"
  pid="${srv%%:*}"
  port="${srv#*:}"

  local captures="$MAX_CAPTURES"
  if [[ -z "$script_file" ]]; then
    captures="1"
  fi

  local max_frame="0"
  if [[ -n "$script_file" ]]; then
    max_frame="$(web_script_max_frame "$script_file")"
  fi

  local fail=0
  for ((i=0; i<captures; i++)); do
    local png="$out_dir/frame_$(printf '%06d' "$i").png"
    local dh="$out_dir/frame_$(printf '%06d' "$i").dhash"
    local budget_ms="1200"
    if [[ -n "$script_file" && "$i" -gt 0 ]]; then
      budget_ms=$(( (max_frame + 10) * 20 + 3000 ))
    fi

    local url="http://127.0.0.1:$port/index.html?coi_visual=1&coi_visual_capture_index=$i"
    web_capture_one "$url" "$png" "$budget_ms" || { fail=1; break; }

    python3 "$ROOT_DIR/tests/visual/dhash_png.py" "$png" --write "$dh" >/dev/null

    if [[ "$mode" == "compare" ]]; then
      local base_dh="$base_dir/$(basename "$dh")"
      if [[ ! -f "$base_dh" ]]; then
        echo "error: missing baseline: $base_dh"
        fail=1
        break
      fi
      set +e
      python3 "$ROOT_DIR/tests/visual/dhash_png.py" "$png" --baseline "$base_dh" --tolerance "$TOLERANCE" >/dev/null
      rc=$?
      set -e
      if [[ "$rc" -ne 0 ]]; then
        echo "   mismatch: $(basename "$dh")"
        fail=1
        break
      fi
    fi
  done

  web_stop_server "$pid"

  if [[ "$fail" -ne 0 ]]; then
    return 1
  fi

  if [[ "$mode" == "update" ]]; then
    rm -rf "$base_dir"
    mkdir -p "$base_dir"
    cp -f "$out_dir"/*.dhash "$base_dir/" 2>/dev/null || true
    cp -f "$out_dir"/*.png "$base_dir/" 2>/dev/null || true
    echo "   updated baseline: $base_dir"
  fi

  return 0
}

fail=0
ran_count=0
last_scene_out=""

for i in "${!names[@]}"; do
  name="${names[$i]}"
  rel_path="${paths[$i]}"
  b="${backends[$i]}"

  if [[ -n "$SCENE_PATTERN" && "$name" != $SCENE_PATTERN ]]; then
    continue
  fi

  case ",$b," in
    *",$BACKEND,"*) ;;
    *) continue;;
  esac

  path="$ROOT_DIR/$rel_path"
  if [[ ! -f "$path" ]]; then
    echo "error: missing scene file: $path"
    fail=1
    continue
  fi

  echo "==> scene: $name"

  scene_out="$OUT_DIR/$name"
  scene_base="$BASELINE_DIR/$name"
  ran_count=$((ran_count + 1))
  last_scene_out="$scene_out"

  env_file="$(pick_env_file "$path")"
  script_file="$(pick_script_file "$path")"

  extra_env=()
  read_env_file "$env_file" extra_env

  if [[ "$BACKEND" == "desktop" && -n "$script_file" ]]; then
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

  if [[ "$MODE" == "update" ]]; then
    if [[ "$BACKEND" == "desktop" ]]; then
      rm -rf "$scene_out"
      mkdir -p "$scene_out"
      mkdir -p "$scene_base"
      run_scene_desktop "update" "$path" "$scene_out" "$scene_base" "${extra_env[@]}" || { fail=1; continue; }
      rm -rf "$scene_base"
      mkdir -p "$scene_base"
      cp -f "$scene_out"/*.dhash "$scene_base/" 2>/dev/null || true
      cp -f "$scene_out"/*.png "$scene_base/" 2>/dev/null || true
      echo "   updated baseline: $scene_base"
    else
      run_scene_web "update" "$name" "$path" "$scene_out" "$scene_base" "$script_file" || { fail=1; continue; }
    fi
    continue
  fi

  if [[ ! -d "$scene_base" ]]; then
    echo "error: missing baseline dir: $scene_base"
    echo "hint: run: $0 --backend $BACKEND --update --scene $name"
    fail=1
    continue
  fi

  set +e
  if [[ "$BACKEND" == "desktop" ]]; then
    rm -rf "$scene_out"
    mkdir -p "$scene_out"
    run_scene_desktop "compare" "$path" "$scene_out" "$scene_base" "${extra_env[@]}"
    rc=$?
  else
    run_scene_web "compare" "$name" "$path" "$scene_out" "$scene_base" "$script_file"
    rc=$?
  fi
  set -e

  if [[ "$rc" -ne 0 ]]; then
    echo "   FAIL: $name"
    echo "   captures: $scene_out"
    fail=1
  else
    echo "   OK: $name"
  fi
done

if [[ "$OPEN_AFTER" -eq 1 ]]; then
  if [[ "$ran_count" -le 1 ]]; then
    python3 "$ROOT_DIR/tests/visual/make_run_output_index.py" --root "$last_scene_out" --out "$last_scene_out/index.html" --title "COI Visual Output" >/dev/null
    serve_dir_and_open "$last_scene_out" "index.html"
  else
    python3 "$ROOT_DIR/tests/visual/make_run_output_index.py" --root "$OUT_DIR" --out "$OUT_DIR/index.html" --title "COI Visual Output" >/dev/null
    serve_dir_and_open "$OUT_DIR" "index.html"
  fi
fi

exit "$fail"
