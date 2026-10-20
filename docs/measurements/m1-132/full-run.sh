#!/usr/bin/env bash
# M1-132 step 2 (decisions 560 and 709 (8)): one full run of the tests of one
# tree, with the coverage records in a folder that only this run uses.
#
#   bash docs/measurements/m1-132/full-run.sh <tree> <empty folder, with the separators of Windows> <ctest.exe>
#
# Run from the root of the repository. The folder of the tree does not change
# for the shell that calls this script: the cd is in a subshell. Each test runs
# one time, eight at a time, as CTest runs them, without the test driver.
set -u
tree=$1
records=$2
ctest=$3
start=$(date +%s%N)
echo "start $(date -Iseconds), tree $tree, records $records"
(cd "$tree" && ORBSIM_COVERAGE_RECORDS="$records" "$ctest" -j 8)
code=$?
end=$(date +%s%N)
echo "end $(date -Iseconds), ctest exit $code, $(( (end - start) / 1000000 )) ms"
