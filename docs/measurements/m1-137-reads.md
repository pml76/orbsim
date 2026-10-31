# M1-137 item 3 — the read records compared with Process Monitor

Status: measured 2026-10-31 on the first machine, in `build/debug` and
`build/relwithdebinfo`, with `scripts/compare-reads.py` before its commit, on the
tree of commit `b235055` with the tool, with clang 23.1.2. Register decisions
848, 849 and 892-903. The results of the tool are
[`m1-137/reads-debug.json`](m1-137/reads-debug.json) and
[`m1-137/reads-relwithdebinfo.json`](m1-137/reads-relwithdebinfo.json). They hold
the list of folders outside the repository that a later gate compares with its
own list (decision 895).

## The question

Does the fingerprint of each test hold each path in the repository, the build
tree included, that a process of the test read, examined or listed? C++ tests
were last compared on 2026-10-16, and the `list` lines of decision 832 had not
been seen in a recording.

## The result

**No test used a path that its fingerprint does not hold, by the rules of
decisions 893 and 899-902.**

| | Debug | RelWithDebInfo |
|---|---|---|
| Tests compared | 591 of 591 | 585 of 585 |
| C++ tests with a miss, before the rulings | 0 of 487 | 0 of 487 |
| CMake trace tests with a miss, before the rulings | 1 of 57 | 1 of 51 |
| Python tests with complete records, with a miss, before the rulings | 35 of 35, only a `.pyc` file | 35 of 35, only a `.pyc` file |
| Tests with no complete record, with a miss, before the rulings | 12 of 12 | 12 of 12 |
| Lines of misses, before the rulings | 6,326 | 6,310 |
| Misses, after the rulings | **0** | **0** |
| The planted fault | found, and no other | found, and no other |
| Folders outside the repository | 76 | 75 |

The `list` lines of decision 832 are seen: the folder of the hook gives no miss
for a Python test with complete records.

## The groups

Each row is a group that the tool counts and does not call a miss. The numbers
are from the first comparison of each tree, with the rule that the row names.

| Group | Debug | RelWithDebInfo | Ruling |
|---|---|---|---|
| Not there, in the build tree | 5,480 | 4,584 | decision 834 |
| A path of the build tree, with the part `build tree` | 5,349 | 5,347 | decision 901 |
| A folder of the repository | 1,180 | 1,168 | decision 834 |
| A name in a folder that a part hashes | 888 | 874 | decision 900 |
| A folder of the build tree | 282 | 282 | decision 834 |
| A `.pyc` file of a known `.py` file | 79 | 79 | decision 899 |
| `build/SystemResources` | 60 | 52 | decision 834 |
| The `.pdb` file of a program of the test | 17 | 1 | decision 864 |
| A listing of a folder that the test writes again | 10 | 10 | decision 834 |
| A file of `.git`, with the part `build tree` | 8 | 8 | decision 902 |
| A listing of the root or of `build/` | 3 | 3 | decisions 834 and 902 |
| A question of CMake about `build.ninja` | 2 | 2 | decision 902 |

## The recordings

| Recording | The run | Its last event | It includes |
|---|---|---|---|
| Debug, 1 | full, 08:10:29 to 08:33:15, stopped 08:34 | 08:27:04 | 544 of 591 tests |
| Debug, 2 | the 47 other tests and their fixtures, with `--short-run`, 11:27:43 to 11:35:29 | 11:37:21 | the 47 tests |
| RelWithDebInfo, 1 | full, 08:38:29 to 08:47:56 | none | not used: the machine restarted at 10:38, before the file was closed |
| RelWithDebInfo, 2 | full, 10:46:26 to 11:04:16, stopped 11:23 | 11:19:57 | 585 of 585 tests |

All tests passed in each run. Each `check-full` gave exit 1 at its end for one
reason: the tree held the tool, not committed, thus the driver stamps no commit.

**Why recordings end early** (decision 903). Process Monitor writes its file
later than the events arrive. It was about 6 minutes behind at 08:29 and about 7
minutes behind when it stopped at 08:34, and the events in its queue were lost.
At 11:23, it was about 4 minutes behind. This is probably also the cause of the
recordings of M1-138 that ended early, which was not proved. The size of the file
does not show the queue: the file grows at about 128 MB each minute with no test
running, from other programs on the machine. The owner's question found this. The
time of the newest event in the file does show it, and the steps of the tool now
use the events.

## Faults of the tool that real data found

- After decision 899, the planted fault of the `.py` line of the hook also makes
  its `.pyc` file a miss, and the check of the plant refused that. It now accepts
  the `.pyc` file of that line and no other path.
- The list of outside folders counted the folders that a search of `PATH` opens,
  170 entries. It now counts only files: 76 and 75.

**The instrument, seen to fail** (decision 855). The self-test has 118 cases. On a
scratch copy, 31 changes of the tool, each alone, each made the self-test fail.

## The limits

- The tool sees only what the recorded runs did.
- The tool holds its own copy of what each part holds. It stops for a new part,
  and it does not see a changed part (decision 894).
- For the six tests with the part `build tree`, a file of the tree that somebody
  changes by hand is seen only by the full run before a push (decision 901).
- Why CMake asks about `build.ninja` was not measured, and that the `.git` reads
  come from `git ls-files` was not examined (decision 902).
- The list of outside folders is compared by hand (decision 895).
- The first machine only.
