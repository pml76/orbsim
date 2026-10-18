#!/usr/bin/env bash
# M1-131 item 4 (decision 589): the frame-time benchmark, the way
# docs/PROJECT_STATE.md section 10 takes a row (decisions 374 and 383) --
# in relwithdebinfo, three runs one after the other of
#     orbsim --bench grid-orbit --frames 600 --width 1920 --height 1080
# The one difference: --bench-out puts each run's summary and table in this
# folder (bench-<label>/) rather than the tree's bench/, so the tree is left as
# it was and the files stay beside this record.
#     bash bench.sh <label>
set -u
LABEL=$1
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/bench-$LABEL"
ORBSIM=/c/Users/U439644/Projects/untitled/build/relwithdebinfo/orbsim.exe
mkdir -p "$OUT"
for run in 1 2 3; do
  echo "=== run $run, $(date '+%Y-%m-%d %H:%M:%S')"
  "$ORBSIM" --bench grid-orbit --frames 600 --width 1920 --height 1080 --bench-out "$(cygpath -w "$OUT")"
  echo "=== run $run exit $?"
done
ls -la "$OUT"
