#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

COI_COLOR='\033[38;2;148;119;255m'  # #9477FF
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

COMPILER="$PROJECT_ROOT/coi"
TEST_ROOT="$PROJECT_ROOT/tests/desktop"

if [ ! -d "$TEST_ROOT" ]; then
  echo -e "${RED}No desktop tests found at $TEST_ROOT${NC}"
  exit 1
fi

# Ensure compiler exists or rebuild
if [ ! -f "$COMPILER" ]; then
  echo "Compiler not found. Building..."
  (cd "$PROJECT_ROOT" && ./build.sh)
fi

TOTAL=$(find "$TEST_ROOT" -name "*_pass.coi" -o -name "*_fail.coi" | wc -l | tr -d ' ')
if [ "$TOTAL" -eq 0 ]; then
  echo -e "${RED}No desktop tests found in $TEST_ROOT${NC}"
  exit 1
fi

draw_progress_bar() {
  local current=$1
  local total=$2
  local width=50
  local filled=$((current * width / total))
  printf "\r["
  for ((i=0; i<filled; i++)); do printf "${COI_COLOR}█${NC}"; done
  for ((i=filled; i<width; i++)); do printf "░"; done
  printf "] %d/%d" "$current" "$total"
}

FAILURES=0
FAILED_TESTS=()
PASSED=0

while IFS= read -r -d '' test_file; do
  rel_path="${test_file#$PROJECT_ROOT/}"
  filename=$(basename "$test_file")
  expected_out="${test_file%.coi}.expected.txt"
  expected_err="${test_file%.coi}.expected.err.txt"

  tmp_dir="$(mktemp -d)"
  dist_dir="$tmp_dir/dist"
  mkdir -p "$dist_dir"

  set +e
  compiler_out="$("$COMPILER" "$test_file" --target desktop --out "$dist_dir" 2>&1)"
  compiler_rc=$?
  set -e

  if [[ "$filename" == *"_fail.coi" ]]; then
    if [ $compiler_rc -eq 0 ]; then
      FAILED_TESTS+=("$rel_path (expected failure, got success)")
      FAILURES=$((FAILURES+1))
    else
      if [ -f "$expected_err" ]; then
        # All non-empty lines in expected_err must appear in compiler output.
        missing=0
        while IFS= read -r line || [ -n "$line" ]; do
          [ -z "$line" ] && continue
          if ! printf "%s" "$compiler_out" | grep -Fq "$line"; then
            missing=1
          fi
        done < "$expected_err"
        if [ $missing -ne 0 ]; then
          FAILED_TESTS+=("$rel_path (error output mismatch)")
          FAILURES=$((FAILURES+1))
        else
          PASSED=$((PASSED+1))
        fi
      else
        PASSED=$((PASSED+1))
      fi
    fi
    rm -rf "$tmp_dir"
    draw_progress_bar $((PASSED + FAILURES)) "$TOTAL"
    continue
  fi

  # Pass tests must compile successfully.
  if [ $compiler_rc -ne 0 ]; then
    FAILED_TESTS+=("$rel_path (expected success, got failure)")
    FAILURES=$((FAILURES+1))
    rm -rf "$tmp_dir"
    draw_progress_bar $((PASSED + FAILURES)) "$TOTAL"
    continue
  fi

  if [ ! -f "$dist_dir/app" ]; then
    FAILED_TESTS+=("$rel_path (desktop build did not produce dist/app)")
    FAILURES=$((FAILURES+1))
    rm -rf "$tmp_dir"
    draw_progress_bar $((PASSED + FAILURES)) "$TOTAL"
    continue
  fi

  if [ ! -f "$expected_out" ]; then
    FAILED_TESTS+=("$rel_path (missing expected output: ${expected_out#$PROJECT_ROOT/})")
    FAILURES=$((FAILURES+1))
    rm -rf "$tmp_dir"
    draw_progress_bar $((PASSED + FAILURES)) "$TOTAL"
    continue
  fi

  # If the test asks for frames, pass them through. Default to 0 so the program exits immediately.
  frames="0"
  if [ -f "${test_file%.coi}.frames" ]; then
    frames="$(cat "${test_file%.coi}.frames" | tr -d ' \t\r\n')"
    [ -z "$frames" ] && frames="0"
  fi

  dump_mode="1"
  if [ -f "${test_file%.coi}.dump" ]; then
    dump_mode="$(cat "${test_file%.coi}.dump" | tr -d ' \t\r\n')"
    [ -z "$dump_mode" ] && dump_mode="1"
  fi

  layout_dump_set="0"
  layout_dump=""
  if [ -f "${test_file%.coi}.layout_dump" ]; then
    layout_dump_set="1"
    layout_dump="$(cat "${test_file%.coi}.layout_dump" | tr -d ' \t\r\n')"
    [ -z "$layout_dump" ] && layout_dump="1"
  fi

  viewport=""
  if [ -f "${test_file%.coi}.viewport" ]; then
    viewport="$(cat "${test_file%.coi}.viewport" | tr -d ' \t\r\n')"
  fi

  run_env=(COI_DESKTOP_DUMP="$dump_mode" COI_DESKTOP_FRAMES="$frames")
  if [ "$layout_dump_set" -eq 1 ]; then
    run_env+=(COI_DESKTOP_LAYOUT_DUMP="$layout_dump")
  fi
  if [ -n "$viewport" ]; then
    run_env+=(COI_DESKTOP_VIEWPORT="$viewport")
  fi

  set +e
  runtime_out="$(env "${run_env[@]}" stdbuf -o0 "$dist_dir/app" 2>&1)"
  runtime_rc=$?
  set -e

  if [ $runtime_rc -ne 0 ]; then
    FAILED_TESTS+=("$rel_path (runtime exited with code $runtime_rc)")
    FAILURES=$((FAILURES+1))
    rm -rf "$tmp_dir"
    draw_progress_bar $((PASSED + FAILURES)) "$TOTAL"
    continue
  fi

  if ! diff -u "$expected_out" <(printf "%s" "$runtime_out") >/dev/null; then
    FAILED_TESTS+=("$rel_path (output mismatch)")
    FAILURES=$((FAILURES+1))
  else
    PASSED=$((PASSED+1))
  fi

  rm -rf "$tmp_dir"
  draw_progress_bar $((PASSED + FAILURES)) "$TOTAL"
done < <(find "$TEST_ROOT" -name "*_pass.coi" -o -name "*_fail.coi" -type f -print0 | sort -z)

echo ""
if [ $FAILURES -eq 0 ]; then
  echo -e "${GREEN}All $TOTAL desktop test(s) passed!${NC}"
  exit 0
fi

echo -e "${RED}$FAILURES desktop test(s) failed:${NC}"
for failed_test in "${FAILED_TESTS[@]}"; do
  echo -e "  ${RED}✗${NC} $failed_test"
done
echo -e "\n${GREEN}$PASSED passed${NC}, ${RED}$FAILURES failed${NC} out of $TOTAL tests"
exit 1
