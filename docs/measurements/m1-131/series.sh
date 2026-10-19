#!/usr/bin/env bash
# M1-131 item 4, the second half: six full rebuilds of one check tree, the
# hook off and on in turn (off, on, off, on, off, on), at one commit.
#     bash series.sh relwithdebinfo rel      or      bash series.sh debug debug
# For each rebuild:
#   1. a configure that sets the option (decision 641), timed;
#   2. a machine sample, then measure-build.py: ninja -t clean, the timed
#      full build, one check -- exactly as in the first half;
#   3. the two Ninja-log analyses, as in the first half;
#   4. with the hook on: the harness's copy (measure-snapshot.py), the
#      extension breakdown (after the first rebuild with the hook only) and
#      the census of the maps;
#   5. a machine sample, and in relwithdebinfo the benchmark (bench.sh).
# A heartbeat line every 60 s keeps the run from being silent.
# An optional third argument starts the series at that rebuild (1 to 6), to
# continue a series that stopped. After each check, the tree's CTest log,
# Testing/Temporary/LastTest.log, is kept as <label>-LastTest.log: it holds
# the output of each test that ran, reproducible_build's line among them.
set -u
TREE_NAME=$1
SHORT=$2
FIRST=${3:-1}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=/c/Users/U439644/Projects/untitled
CMAKE=/c/Users/U439644/AppData/Local/Programs/CLion/bin/cmake/win/x64/bin/cmake.exe
TREE_WIN="C:\\Users\\U439644\\Projects\\untitled\\build\\$TREE_NAME"
TREE_FWD="C:/Users/U439644/Projects/untitled/build/$TREE_NAME"

stamp() { date '+%Y-%m-%d %H:%M:%S'; }

# Runs a command with its output to a file, and prints a line every 60 s
# while it runs. Prints the exit code and the wall time at the end.
watched() {
  local out=$1; shift
  local start; start=$(date +%s.%N)
  "$@" > "$out" 2>&1 &
  local pid=$!
  while kill -0 "$pid" 2> /dev/null; do
    sleep 60
    kill -0 "$pid" 2> /dev/null && echo "[$(stamp)]   ... still running: $(tail -c 300 "$out" | tr '\r\n' '  ' | tail -c 160)"
  done
  wait "$pid"
  local code=$?
  local end; end=$(date +%s.%N)
  echo "[$(stamp)]   exit $code, $(python -c "print(f'{$end - $start:.1f}')") s, output in $(basename "$out")"
  return $code
}

first_on=yes
# The extension breakdown is taken after the first rebuild with the hook only.
if (( FIRST > 2 )); then first_on=no; fi
for run in $(seq "$FIRST" 6); do
  if (( run % 2 == 1 )); then state=OFF; kind=nohook; else state=ON; kind=hook; fi
  n=$(( (run + 1) / 2 ))
  label="$SHORT-$kind-$n"
  echo "[$(stamp)] === rebuild $run of 6: $label, ORBSIM_COVERAGE_HOOK=$state"
  echo "[$(stamp)] configure"
  (cd "$REPO" && watched "$HERE/$label-configure.log" "$CMAKE" --preset "$TREE_NAME" -DORBSIM_COVERAGE_HOOK=$state) || {
    echo "[$(stamp)] STOP: the configure failed"; exit 1; }
  grep -n "ORBSIM_COVERAGE_HOOK" "$REPO/build/$TREE_NAME/CMakeCache.txt" | sed "s/^/[$(stamp)]   cache: /"
  pwsh -NoProfile -File "$HERE/machine-state.ps1" > "$HERE/state-$label-before.txt" 2>&1
  echo "[$(stamp)] measure-build.py"
  watched "$HERE/$label-driver.txt" python -I "$HERE/measure-build.py" "$TREE_WIN" "$label"
  build_code=$?
  cp -p "$REPO/build/$TREE_NAME/Testing/Temporary/LastTest.log" "$HERE/$label-LastTest.log" 2> /dev/null
  cat "$HERE/$label-driver.txt"
  python -I "$HERE/ninja-log-analysis.py" "$TREE_FWD" "$HERE/$label-ninja_log-before-build.txt" \
      "$HERE/$label-ninja_log-after-build.txt" --targets 15 > "$HERE/$label-build-analysis.txt" 2>&1
  python -I "$HERE/ninja-log-analysis.py" "$TREE_FWD" "$HERE/$label-ninja_log-after-build.txt" \
      "$HERE/$label-ninja_log-after-check.txt" --targets 5 > "$HERE/$label-check-analysis.txt" 2>&1
  head -3 "$HERE/$label-build-analysis.txt"
  grep -E "run-tests:" "$HERE/$label-check.log" | tail -3
  if [[ $build_code -ne 0 ]]; then
    echo "[$(stamp)] NOTE: measure-build.py ended with exit $build_code"
    if ! grep -q "build: exit 0" "$HERE/$label-driver.txt"; then echo "[$(stamp)] STOP: the build failed"; exit 1; fi
  fi
  pwsh -NoProfile -File "$HERE/machine-state.ps1" > "$HERE/state-$label-after-check.txt" 2>&1
  if [[ $state == ON ]]; then
    echo "[$(stamp)] measure-snapshot.py"
    watched "$HERE/$label-snapshot.txt" python -I "$HERE/measure-snapshot.py" "$TREE_WIN" "$label"
    cat "$HERE/$label-snapshot.txt"
    if [[ $first_on == yes ]]; then
      python -I "$HERE/tree-breakdown.py" "$TREE_WIN" > "$HERE/$label-tree-breakdown.txt" 2>&1
      first_on=no
    fi
    python -I "$HERE/maps-census.py" "$TREE_WIN" "$HERE/$label-maps.json" > "$HERE/$label-maps.txt" 2>&1
    head -8 "$HERE/$label-maps.txt"
  fi
  if [[ $SHORT == rel ]]; then
    pwsh -NoProfile -File "$HERE/machine-state.ps1" > "$HERE/state-$label-before-bench.txt" 2>&1
    echo "[$(stamp)] bench.sh"
    watched "$HERE/$label-bench.txt" bash "$HERE/bench.sh" "$label"
    grep -E "=== run|median|exit" "$HERE/$label-bench.txt" | head -12
  fi
  echo "[$(stamp)] === rebuild $run done"
done
pwsh -NoProfile -File "$HERE/machine-state.ps1" > "$HERE/state-$SHORT-end.txt" 2>&1
echo "[$(stamp)] SERIES DONE"
