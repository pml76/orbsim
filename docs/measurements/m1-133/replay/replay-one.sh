#!/bin/sh
# One commit of the replay of M1-133 (the brief, step 3):
#   replay-one.sh <initial commit> <label of the commit before>
# It copies the commit with git cherry-pick, compares the patch-id of the copy
# with that of the initial commit, builds the default target, runs
# replay-count.py count, the driver without --full, and replay-count.py after.
# Each step writes its log in <scratch>/replay/<initial commit>/.
# It stops at the first step that fails, with an exit code of its own.

WT=C:/Users/U439644/Projects/untitled/build/wt133
TREE=$WT/build/debug
R=C:/Users/U439644/AppData/Local/Temp/claude/C--Users-U439644-Projects-untitled/4fe89f80-6ba9-4110-b2ad-13efa52050eb/scratchpad/replay
P="C:/Program Files/PyManager/python3.exe"
CMAKE=C:/Users/U439644/AppData/Local/Programs/CLion/bin/cmake/win/x64/bin/cmake.exe
CTEST=C:/Users/U439644/AppData/Local/Programs/CLion/bin/cmake/win/x64/bin/ctest.exe

c=$1
previous=$2
L=$R/$c
mkdir -p "$L"
stamp() { date +%s; }

before=$(git -C $WT rev-parse HEAD)
echo "before=$before" > "$L/steps.txt"
t=$(stamp)
if ! git -C $WT cherry-pick $c > "$L/cherry-pick.log" 2>&1; then
    echo "cherry-pick failed" >> "$L/steps.txt"; exit 10
fi
echo "cherry-pick seconds=$(( $(stamp) - t ))" >> "$L/steps.txt"
copy=$(git -C $WT rev-parse HEAD)
a=$(git -C $WT diff $before $copy | git -C $WT patch-id --stable | cut -d' ' -f1)
b=$(git -C $WT show $c | git -C $WT patch-id --stable | cut -d' ' -f1)
echo "copy=$copy initial=$c patch-id of the copy's diff=$a patch-id of the initial commit=$b" > "$L/patch-id.txt"
if [ "$a" != "$b" ]; then
    echo "patch-id differs" >> "$L/steps.txt"; exit 11
fi

t=$(stamp)
"$CMAKE" --build $TREE > "$L/build.log" 2>&1
code=$?
echo "build exit=$code seconds=$(( $(stamp) - t ))" >> "$L/steps.txt"
[ $code -eq 0 ] || exit 12

t=$(stamp)
"$P" "$R/replay-count.py" count $c --previous $previous > "$L/count.log" 2>&1
code=$?
echo "count exit=$code seconds=$(( $(stamp) - t ))" >> "$L/steps.txt"
[ $code -eq 0 ] || exit 13

t=$(stamp)
"$P" $WT/scripts/run-tests.py $TREE --ctest $CTEST --jobs 10 > "$L/driver.log" 2>&1
code=$?
echo "driver exit=$code seconds=$(( $(stamp) - t ))" >> "$L/steps.txt"

t=$(stamp)
"$P" "$R/replay-count.py" after $c --log "$L/driver.log" > "$L/after.log" 2>&1
code=$?
echo "after exit=$code seconds=$(( $(stamp) - t ))" >> "$L/steps.txt"
[ $code -eq 0 ] || exit 14
exit 0
