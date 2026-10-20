# M1-132 — Each recorded address, the linker's map and the procedure ranges

Kind: reference
Binding: no — this file records a measurement and how to do it again
Read when: you want the figures behind M1-132 step 2, or you must do this
measurement again.

**Complete.** Measured 2026-10-20 by a helper of the session, for step 2 of
[M1-132](../plan/tasks/m1-132-function-level-selection.md). Two results did
not agree with the instructions for the step: section 4, the number of
entries in a program's hook table, and section 6, the procedure records of
a thunk. The owner ruled the two the same day, in register decision 712.

## What is measured, and why

A **coverage record** is the file that the recorder writes when a program
with the coverage hook ends: the program, its command line, the count of
flags, and the address of each function that the program entered, less the
base address of the program (register decision 644). M1-132 turns each
address into a function, and selects the tests from these functions. The
rulings ([the register](../plan/milestone-1-decisions.md)):

- **Decision 582**: an address becomes a function through the linker's map,
  with the name that the object file gives the function.
- **Decision 695**: one name at an address is the function; two names, and
  only two, `??_E` and `??_G` with the same text after these four
  characters, are one destructor, with the `??_G` name; each other set of
  names is a problem.
- **Decision 560, with 709 (8)**: before the map is used, compare it with the
  procedure ranges of the debug information (the .pdb file), over a full run
  in each tree, one time, read with llvm-pdbutil, and written as a
  measurement record. It is not a part of `check`.
- **Decision 551**: no two entries of the hook's table of a program at one
  address.
- **Decision 704**: the count of flags in a record must be equal to the
  number of entries in the hook's table of its program.
- **Decisions 709 (3) and 711 (4)**: the key of a function -- its decorated
  name when it is external, the key of its object and its name when it is
  static -- must be the same from the map and from the digests (`Program` in
  scripts/compiled-code.py).

The functions that step 2 adds to scripts/compiled-code.py are
`address_index`, `record_functions`, `symbol_key` and `table_entries`. This
record uses them, and compares their results with the .pdb files, with the
objects, and with a reader of the programs that is part of the scripts.

## The machine and the text

The first machine, 2026-10-20, from 18:21 to 18:48 (+02:00). The text:
commit `8a656ac` ("M1-132 step 1: the readers and the digests"), with the
change of step 2 to scripts/compiled-code.py in the working tree, not
committed. The trees: build/relwithdebinfo and build/debug, each with
`ORBSIM_COVERAGE_HOOK=ON`. The programs date from 2026-10-19; the `check` of
step 1 (2026-10-20, 17:04 and 17:09) passed in the two trees, and no source
of a program has changed since. No build and no `check` ran for this
measurement.

Tools: CTest 4.3.1, from CLion
(C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\ctest.exe);
llvm-pdbutil and llvm-undname of LLVM 23.1.2, from
C:\GitHub\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin; Python 3.14.0. During
the two full runs, no other heavy work ran: the helper wrote scripts only.

## How it was run

From the root of the repository, in this sequence. `SCRATCH` is the folder
C:\Users\U439644\AppData\Local\Temp\claude\C--Users-U439644-Projects-untitled\5985916b-899b-4261-a138-ee39bc892852\scratchpad\step2,
a scratch folder of the session. It holds the records and the full CTest
output, and a different session cannot find it.

```
bash docs/measurements/m1-132/full-run.sh build/relwithdebinfo 'SCRATCH\records-rel' <ctest.exe>
bash docs/measurements/m1-132/full-run.sh build/debug 'SCRATCH\records-debug' <ctest.exe>
python -I docs/measurements/m1-132/table-layout.py build/relwithdebinfo build/debug
python -I docs/measurements/m1-132/entry-size.py build/relwithdebinfo build/debug
python -I docs/measurements/m1-132/plant-faults.py SCRATCH/faults2
python -I docs/measurements/m1-132/records-functions.py build/<tree> SCRATCH/records-<tree>
python -I docs/measurements/m1-132/make-faults.py build/relwithdebinfo test_srgb.exe SCRATCH/records-rel SCRATCH/fault-run2
python -I docs/measurements/m1-132/records-functions.py build/relwithdebinfo SCRATCH/fault-run2/records
python -I docs/measurements/m1-132/key-agreement.py build/<tree>
python -I docs/measurements/m1-132/key-agreement.py build/relwithdebinfo --show 2 --compiled SCRATCH/faults2/compiled-code-static-by-name.py
python -I docs/measurements/m1-132/procedure-ranges.py build/<tree> SCRATCH/records-<tree> <llvm-pdbutil.exe> <llvm-undname.exe>
```

[`m1-132/full-run.sh`](m1-132/full-run.sh) runs
`ctest -j 8` in the tree, in a subshell, with `ORBSIM_COVERAGE_RECORDS` set
to an empty folder that only this run uses: not the tree's coverage-records
folder, and not the test driver. The helper wrote the other scripts while it
measured, and ran each of them more than one time while it found the causes
in sections 5 and 6. The outputs in [`m1-132/`](m1-132/) come from the last
run of each script, as it is in that folder, after the two full runs.

## The figures

### 1. The full runs

| | RelWithDebInfo | Debug |
|---|---|---|
| Time of the run | 18:21:42 to 18:23:11 | 18:23:47 to 18:27:19 |
| Tests | 589: 100 % passed, 0 failed | 589: 100 % passed, 0 failed |
| Not run, as CTest shows | 7: test 92, and the six probes that must abort (tests 484 to 489) | 1: test 92 |
| Time, CTest's "Total Test time (real)" | 88.22 s | 211.05 s |
| Time, from the script | 88,486 ms | 211,373 ms |
| Records | 637 files: 586 `.functions.txt`, 51 `.shaders.txt` | the same counts |

Test 92 is "a write the disk refuses only when the file is closed is
reported". The six probes print "SKIPPED:" in RelWithDebInfo, because the
assertions are not live there (cmake/VerifyAssertionProbe.cmake). They run,
and write a record. The files:
[`m1-132/full-run-relwithdebinfo-summary.txt`](m1-132/full-run-relwithdebinfo-summary.txt)
and [`m1-132/full-run-debug-summary.txt`](m1-132/full-run-debug-summary.txt),
the start and the end of the CTest output; the full output, 123 KB for each
tree, is in `SCRATCH` only.

### 2. Each record turned into functions

[`m1-132/records-functions.py`](m1-132/records-functions.py) reads each
record with `read_record` of scripts/check-coverage-recorder.py, and gives
its addresses to `record_functions`, with `symbol_key` and the paths of
`module_paths`.

| | RelWithDebInfo | Debug |
|---|---|---|
| Records read | 586 of 586 | 586 of 586 |
| Programs | 51 | 51 |
| Records "out of line" | 0 | 0 |
| Addresses, in all records | 43,723 | 290,043 |
| Different (program, address) pairs | 4,709 | 33,285 |
| Keys | 43,723 | 290,043 |
| **Problems** | **0** | **0** |
| The record with the most addresses | 246 addresses (orbsim.exe, `probe_grid-400km_golden`) | 3,616 addresses (the same test) |

54 programs in each tree have the hook. Three left no record in the run:
coverage_recorder_probe.exe and coverage_recorder_unaligned.exe, because the
test `coverage_recorder` runs them with a different record folder
(scripts/check-coverage-recorder.py), and reproducible_build_probe.exe,
which the test `reproducible_build` compiles and links again but does not
run. Section 5 includes their tables.

### 3. The count of flags against the table (decision 704), and one function at each address (decision 551)

The same script reads the table of each program from the section `.SCOVP`,
with a parser of the PE format that is part of the script: the start
marker, the entries of two words of 8 bytes (the address with the base, and
the value 1 of the hook), and the stop marker.

| | RelWithDebInfo | Debug |
|---|---|---|
| Records with flags equal to `table_entries` | **586 of 586** | **586 of 586** |
| Records with flags equal to VirtualSize / 16, the markers not subtracted | 0 of 586 | 0 of 586 |
| Table entries in the 51 programs | 8,859 | 46,502 |
| Programs where `table_entries` is not the number of entries read | 0 | 0 |
| **Addresses two times in one table** | **0** | **0** |
| Table addresses with a problem in `record_functions` | 0 | 0 |
| Markers that are not 0; values other than 1 | 0; 0 | 0; 0 |
| Problems of `module_paths` | 0 | 0 |

Decision 551 is examined on the addresses of the tables in the programs,
not on the `.SCOVP$M` sections of the objects.

### 4. The number of entries in a program's table (decision 704)

**The length of one entry.** [`m1-132/entry-size.py`](m1-132/entry-size.py):
each `.SCOVP$M` section of the objects with the hook has 16 bytes, with one
relocation of type 1 (IMAGE_REL_AMD64_ADDR64) at offset 0, and the second
word is 1. That is 8,794 sections in 91 objects in RelWithDebInfo, and 63,069
sections in 92 objects in Debug, with no other layout.

**The layout of `.SCOVP` in a program.**
[`m1-132/table-layout.py`](m1-132/table-layout.py) reads the parts of
`.SCOVP` from the map. In each of the 54 programs with the hook in each tree,
the section starts with `.SCOVP$A`, 8 bytes, then `.SCOVP$M`, then
`.SCOVP$Z`, 8 bytes, with no padding. `.SCOVP$A` and `.SCOVP$Z` are the two
markers of the recorder (tests/CoverageRecorder.cpp). Thus **VirtualSize is
16 bytes more than the entries, and VirtualSize / 16 is the number of
entries plus 1** in each program. For example, test_orbit.exe in Debug:
`.SCOVP$M` 0x4a70 bytes (1,191 entries), 1,191 flags, VirtualSize 0x4a80.
SizeOfRawData is more: 512 bytes against 32 bytes for the smallest table.

**This does not agree with the instructions for step 2**, which give
"VirtualSize divided by the size of one entry". With that formula, 0 of the
586 records in each tree are equal to their table (section 3). Of the 54
programs in each tree, only coverage_recorder_unaligned.exe has a count of
flags in its map equal to VirtualSize / 16 (76 and 289): its test gives it
one flag more than its table. `table_entries` subtracts the two markers:
(VirtualSize - 16) / 16. It refuses (gives None) a section that is not two
markers and a number of complete entries, a program with no `.SCOVP` or
with two, and a file that is not a PE program. **This went to the owner
for a decision.** A different method is to read the addresses of
`__start___sancov_pcs` and `__stop___sancov_pcs` from the map, as the
recorder does.

**Decision 712 (1)** ruled that the two counts must agree: from the map's
markers -- from the end of the 8-byte start marker to the stop marker,
divided by 16, as LLVM's SanitizerCoverage pass gives the hook the start and
the end of its table on Windows (`CreateSecStartEnd`) -- and from the length
of `.SCOVP` less the two markers. `map_table_entries` and
`hook_table_entries` in scripts/compiled-code.py do this; `hook_table_entries`
gives no number and a problem when the two do not agree. Measured after the
ruling: the two counts are equal in each of the 54 programs with the hook in
each tree, and equal to the count of flags in each of the 586 records of each
tree. Five faults planted in copies of the file were each found by a case of
its self-test: the two counts not compared, a marker taken when the map has
it two times, the 8 bytes of the start marker not subtracted, a part of an
entry accepted, and a length below 0 accepted.

### 5. The key from the map against the key from Program (decisions 709 (3) and 711 (4))

[`m1-132/key-agreement.py`](m1-132/key-agreement.py), for each of the 54
programs with the hook in each tree: each object that the map gives with a
path is read, an object linked directly from its path, a member from its
library. The objects with a `.SCOVP$M` section go into one `Program`, each
keyed by `object_key`. Two sets of (object, name) are compared: the functions
of `Program`, each with its key, and the symbols of the map in code sections
of these objects, each with `symbol_key`.

| | RelWithDebInfo | Debug |
|---|---|---|
| Objects with the hook, summed over the programs | 276 (125 members of libraries) | 322 (171 members) |
| Members with a name in the library that is not the map's name | 0 | 0 |
| Objects that `read_object` does not read, with the text `.SCOVP$M` | 0 | 0 |
| (object, name) in the two sets | 24,238 | 70,478 |
| **... with different keys** | **0** | **0** |
| Table addresses of the 54 programs | 9,012 | 47,180 |
| ... with a key that `Program` has, in the same object | 9,012 | 47,180 |

The names in one set only, with their causes from the symbol tables of the
objects:

| In one set only | RelWithDebInfo | Debug |
|---|---|---|
| Map only: `$ehgcr_...`, a static symbol that is not of function type | 2,695 | 2,403 |
| Map only: `??_E...`, a weak external with the `??_G` name of the same class as its default | 645 | 675 |
| Program only: the map gives the name in a different object (a COMDAT copy that the linker did not use) | 13,985 | 79,242 |
| Program only: the map gives a different function of the same COMDAT section in a different object (a copy that the linker did not use) | 6,059 | 8,158 |

The two "map only" classes are not functions of `Program` by its rule: a
function is a symbol of function type in a code section (decision 711 (1)).
No table address gives one of these names as its function: each table
address has a key that `Program` has, in the same object.
The two "Program only" classes are copies of COMDAT functions in objects
that the linker did not use the copy from; each is a COMDAT section. The
names of the funclets in such a copy can be different from the names in the
copy that the linker used. No other cause occurs.

### 6. The recorded addresses against the procedure ranges (decision 560)

[`m1-132/procedure-ranges.py`](m1-132/procedure-ranges.py) reads the records
S_GPROC32, S_LPROC32 and S_THUNK32 with `llvm-pdbutil dump --symbols`, and the section
headers with `llvm-pdbutil dump --section-headers`. The two numbers of
`addr = 0001:0048` are decimal: that procedure, `writeRecord`, is at 0x30 in
.text of the map of angle_between_zero_probe.exe in RelWithDebInfo. The
start of a procedure is the virtual address of its section and its offset.

| | RelWithDebInfo | Debug |
|---|---|---|
| S_GPROC32 and S_LPROC32 records in the 51 .pdb files | 203,596 | 576,558 |
| ... that start at the address of a code symbol of the map | 203,596 | 576,558 |
| S_THUNK32 records | 13,398 | 16,558 |
| Different recorded addresses | 4,709 | 33,285 |
| **One procedure, and only one, starts at the address** (S_GPROC32, S_LPROC32 or S_THUNK32, decision 712) | **4,709** | **33,285** |
| No procedure starts at the address | 0 | 0 |
| Before decision 712, with S_GPROC32 and S_LPROC32 only: one procedure; none | 4,707; 2 | 33,283; 2 |
| One procedure, with no other symbol of the map in its range | 2,440 | 27,418 |
| One procedure, with only funclets and labels in its range | 2,267 | 5,865 |
| **One procedure, with a different symbol of the map in its range** | **0** | **0** |
| Funclets in the ranges; the ranges with a funclet | 11,358; 2,267 | 13,505; 5,865 |
| Labels `$ehgcr_` in the ranges; the ranges with a label | 2,210; 961 | 2,065; 956 |
| The instrument: each recorded address plus 1, a procedure starts there | 0 of 4,709 | 0 of 33,285 |

**The rule of the instructions did not hold for two types of symbols in a
range,** and decision 712 (2) ruled them parts of their procedure. A funclet (the code of a clean-up or of a `catch` block, which clang
puts after its function in the same section) is a different function in the
map, but the .pdb file gives one range for the function and its funclets.
The digest of a function includes its funclets (decision 711 (1)), thus this
is correct for the selection. A label `$ehgcr_` is a static symbol that is
not a function (section 5). The script counts the two classes apart. Each
other symbol in a range is a disagreement, and there is none.

**The two addresses that had no S_GPROC32 or S_LPROC32 record** are one
function in two programs, in each tree: the thunk
`??_E?$basic_ostringstream@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@$4PPPPPPPM@A@EAAPEAXI@Z`,
which llvm-undname gives as the vector deleting destructor of
`std::basic_ostringstream<char>` with the adjustment "vtordisp{-4, 0}" (a
thunk adjusts `this`, then goes to the destructor), in
test_earth_orientation.exe and test_orbit.exe. The .pdb file gives it as
S_THUNK32, not as S_GPROC32 or S_LPROC32, at the same
address. The map gives one name there, the same name, and the hook gives it
a flag. Thus `record_functions` gives one function, as for each other
address. Decision 712 (2) counts an S_THUNK32 record as a procedure:
Microsoft's cvinfo.h gives it an offset, a segment and a length, as a
procedure has, and LLVM's CodeViewDebug.cpp writes it, not a procedure
record, for a thunk. The script counts it so since the ruling, and the
outputs are of that run. Addresses: 0x26c10 and 0x361d0 in RelWithDebInfo, 0x41280 and
0x53a70 in Debug.

**The names, for information only.** The .pdb file gives readable names, the
map decorated names. For the addresses with one procedure:

| | RelWithDebInfo | Debug |
|---|---|---|
| The .pdb name equals the map name (C names) | 103 | 113 |
| The .pdb name is a part of the text of llvm-undname, without spaces and without `class`, `struct`, `union`, `enum` | 2,381 | 21,409 |
| The .pdb name is not a part of it | 628 | 6,740 |
| llvm-undname does not read the map name | 1,595 | 5,021 |

In the examples of the outputs, the names that do not agree are different
texts for the same function: a lambda is `<lambda_1>` in the decorated name
and `lambda at <file>:<line>` in the .pdb file; `const` is before or after
its type; the .pdb file gives `~basic_ofstream` where llvm-undname gives
`vbase dtor`. llvm-undname does not read some long names with template
arguments, for example names with the types of mp-units. The script looks
at no name other than these examples.

### 7. The instruments fail one time (VERIFICATION.md rule 23)

- [`m1-132/make-faults.py`](m1-132/make-faults.py) wrote three records
  with a fault from a record of test_srgb.exe in RelWithDebInfo: the first
  address plus 1 (0x1300 to 0x1301), the count of flags plus 1 (132 to 133),
  and the record for a copy of the program in which the second table entry
  has the address of the first. records-functions.py found 1 problem ("the
  address 0x1301 has 0 names in the map, not one function"), 1 record with
  flags not equal to `table_entries`, and 1 address two times in one table.
  The record with 133 flags is equal to VirtualSize / 16 of its program: the
  formula of the instructions does not see this fault.
- key-agreement.py with a copy of compiled-code.py in which `symbol_key`
  gives only the name for a static symbol: 15,927 of 24,238 (object, name)
  with different keys, and 1,283 of 9,012 table addresses with a key that
  `Program` does not have there.
- procedure-ranges.py: each recorded address plus 1 has no procedure that
  starts there (section 6).
- [`m1-132/plant-faults.py`](m1-132/plant-faults.py): four faults in copies
  of compiled-code.py, each found by the self-test of the file (section 8).

### 8. The self-test of the four functions

The cases of `record_cases` in scripts/compiled-code.py were written before
the code, and failed against stubs: 20 of the 26 new cases reported WRONG.
The six that passed against the stubs are the refusals of `table_entries`,
which a stub that gives None passes. With the code, 166 cases pass
(140 before, measured at the start), and 284 with `--compiler`, `--archiver`
and `--linker` (258 before, as the coordinator gave it: 284 less the 26 new
cases). After decision 712, 176 and 294: ten cases more, for the count of
the table from the map's markers and for the agreement of the two counts
(section 4). The four planted faults
([`m1-132/plant-faults.txt`](m1-132/plant-faults.txt)):

| Fault | Cases that report WRONG |
|---|---|
| The `??_E` and `??_G` pair accepted, the text after the prefix not compared | 1: "??_E and ??_G with a different rest" |
| A static symbol keyed by its name only | 6, for example the static name in a library and the agreement with `Program` |
| `table_entries` from SizeOfRawData | 4: VirtualSize 64 in a section of 512 bytes gives 31, not 3 |
| An address with no name passed | 3: no name, a data symbol and no code, and the record of five addresses |

## What this means

The map turns each recorded address of the two full runs into one function,
with no problem, and with the key that `Program` gives the same function in
the same object. Each recorded address is the start of one procedure record of
the .pdb file, and only one, a thunk included, and no other function of the
map starts in a range, but for the funclets of the same function and the
labels `$ehgcr_`. No two entries of a table have one address. The count of
flags of each record is equal to the entries of its table, which the map's
markers and the length of `.SCOVP` give the same (decision 712).

## The files

In [`m1-132/`](m1-132/), with LF line endings:

- the scripts: [`full-run.sh`](m1-132/full-run.sh),
  [`table-layout.py`](m1-132/table-layout.py),
  [`entry-size.py`](m1-132/entry-size.py),
  [`records-functions.py`](m1-132/records-functions.py),
  [`key-agreement.py`](m1-132/key-agreement.py),
  [`procedure-ranges.py`](m1-132/procedure-ranges.py),
  [`make-faults.py`](m1-132/make-faults.py) and
  [`plant-faults.py`](m1-132/plant-faults.py);
- their outputs, each less than 14 KB: `full-run-<tree>-summary.txt`,
  `table-layout.txt`, `entry-size.txt`, `records-functions-<tree>.txt`,
  `records-functions-faults.txt`, `make-faults.txt`,
  `key-agreement-<tree>.txt`, `key-agreement-fault-relwithdebinfo.txt`,
  `procedure-ranges-<tree>.txt` and `plant-faults.txt`.

Only in `SCRATCH`, which a different session cannot find: the 637 records
of each tree, the full CTest output of each run, and the copies with a
fault.
