#!/bin/sh
# The commits of the replay, one after the other:
#   replay-chain.sh <label of the commit before the first> <commit>...
# It stops when replay-one.sh fails, or when a test other than
# mutants_due_probe_shaders failed (decision 742; the brief, step 3).

R=C:/Users/U439644/AppData/Local/Temp/claude/C--Users-U439644-Projects-untitled/4fe89f80-6ba9-4110-b2ad-13efa52050eb/scratchpad/replay
P="C:/Program Files/PyManager/python3.exe"

previous=$1
shift
for c in "$@"; do
    echo "start $c after $previous at $(date +%H:%M:%S)"
    sh "$R/replay-one.sh" $c $previous
    code=$?
    echo "end $c exit=$code at $(date +%H:%M:%S)"
    [ $code -eq 0 ] || exit $code
    other=$("$P" -c "import json,sys; d=json.load(open(sys.argv[1])); print(' | '.join(n for n in d['failed'] if n != 'mutants_due_probe_shaders'))" "$R/$c/after.json")
    if [ -n "$other" ]; then
        echo "STOP at $c: failed: $other"
        exit 20
    fi
    previous=$c
done
echo "chain done"
