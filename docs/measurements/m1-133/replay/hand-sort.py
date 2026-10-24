"""The hand sort of the changed functions (decision 730), and the counts of the
causes per commit with it.

replay-count.py sorts a changed function by the object that holds the copy in
the program. That is not always where the function is: the linker can take the
copy of a function of a header of src/ from the object of the test file. Thus
each changed function is sorted here by hand, against the diff of its commit
(the lists below were made from `git show -W <commit>`):
  - cause 1, source changed: a function of src/ whose own text the commit
    changed;
  - cause 1, source not changed: a function of src/, or a template of a library
    compiled into an object of src/, or new code that a change in src/ put into
    a program, whose own text the commit did not change;
  - cause 2: a function of the test's own file (a test case body Case<>::run, a
    function of that file), or a template of a library compiled into the
    object of the test's own file, at a commit that changed that file.
The other causes are those of replay-count.py. "By program only" comes from
shadow.py, the count by program with a record of its own (decision 756).

    hand-sort.py <label>...      the commits in sequence
"""
import json
import pathlib
import sys

OUT = pathlib.Path(__file__).resolve().parent

CHANGED = {
    "4963695": ["?wrapTau@orb@@", "?wrapPi@orb@@"],
    "0eb2c93": ["?orbitInfo@orb@@"],
    "e334905": ["?propagateElements@orb@@"],
    "937e49f": ["?eccentricToMeanAnomaly@orb@@", "?eccentricToTrueAnomaly@orb@@", "?meanToEccentricAnomaly@orb@@",
                "?trueToEccentricAnomaly@orb@@", "?describe@orb@@"],
    "8bc7be7": ["?orbitInfo@orb@@", "?propagateElements@orb@@"],
    "73dc795": ["?split@orb@@", "?twoProduct@orb@@"],
    "5047abb": ["??$angleBetween@"],
    "2c5f1e8": ["?stateFromElements@orb@@"],
}
NOT_CHANGED = {
    # The commit changes the text of none of these. Their code changes because
    # the value of OrbitError::SolverDidNotConverge, which they return, goes
    # from 4 to 5: the commit puts NoEccentricAnomaly before it. The three
    # copies of safeguardedRoot keep their length, 1,226 bytes (decision 761).
    "937e49f": ["?solveUniversalAnomaly@", "safeguardedRoot@V<lambda_0>@?0??solveUniversalAnomaly",
                "safeguardedRoot@V<lambda_1>@?0??meanToEccentricAnomaly",
                "safeguardedRoot@V<lambda_2>@?0??meanToEccentricAnomaly"],
    "8bc7be7": ["??$in@U?$derived_unit@Uradian", "??$numerical_value_in@", "?value@?$Scalar@"],
    "5047abb": ["??$?H$2Uone@mp_units", "??Korb@@", "?_Xlength@?$vector@Vstacktrace_entry"],
}


def own_object(program: str) -> str:
    stem = pathlib.PurePosixPath(program).stem
    return f"CMakeFiles/{stem}.dir/tests/{stem}.cpp.obj"


def sorted_by_hand(label: str, function: str, objects: list, programs: list, diff: set) -> str:
    """'1 changed', '1 not changed', '2', or 'unsorted'."""
    # NOT_CHANGED first: the name of a lambda holds the name of the function
    # that holds the lambda, which can be in CHANGED (decision 761).
    if any(fragment in function for fragment in NOT_CHANGED.get(label, [])):
        return "1 not changed"
    if any(fragment in function for fragment in CHANGED.get(label, [])):
        return "1 changed"
    for program in programs:
        own = own_object(program)
        stem = pathlib.PurePosixPath(program).stem
        if (function.startswith(own + "|") or own in objects) and f"tests/{stem}.cpp" in diff:
            return "2"
    return "unsorted"


def main() -> int:
    labels = sys.argv[1:]
    shadow = {row["label"]: row for row in json.loads((OUT / "shadow.json").read_text(encoding="utf-8"))}
    rows = []
    for label in labels:
        result = json.loads((OUT / label / "count.json").read_text(encoding="utf-8"))
        diff = set(result["diff"])
        causes = result["function"]["causes"]
        tested = {}
        functions = {}
        for name, found in causes.items():
            kinds = set()
            programs = [d.split(": ")[0] for d in found.get("note", []) if d.endswith("its bytes changed")]
            for cause, details in found.items():
                if cause == "note":
                    continue
                if cause in ("1", "2", "1/2: another file of tests/", "1/2: no source found"):
                    for detail in details:
                        hand = sorted_by_hand(label, detail["function"], detail["objects"], programs, diff)
                        kinds.add(hand)
                        functions.setdefault((detail["function"], tuple(detail["objects"])), [hand, set()])[1].add(name)
                elif cause == "1/2: the functions without a flag changed":
                    for detail in details:
                        for function in detail["added"] + detail["removed"]:
                            hand = sorted_by_hand(label, function, [], programs, diff)
                            kinds.add(hand)
                            functions.setdefault((function, ("without a flag",)), [hand, set()])[1].add(name)
                else:
                    kinds.add(cause)
            tested[name] = kinds
        counts = {}
        for kinds in tested.values():
            for kind in kinds:
                counts[kind] = counts.get(kind, 0) + 1
            if "1 changed" in kinds or "1 not changed" in kinds:
                counts["1"] = counts.get("1", 0) + 1
        counts["by program only (B)"] = shadow[label]["own record: by program only"]
        rows.append({"label": label, "subject": result["subject"], "counts": counts,
                     "functions": [{"function": f, "objects": list(o), "sort": h, "tests": len(t)}
                                   for (f, o), (h, t) in sorted(functions.items())]})
        print(label, result["subject"][:70])
        print("   causes:", dict(sorted(counts.items())))
        for (f, o), (h, t) in sorted(functions.items(), key=lambda x: x[1][0]):
            print(f"   [{h}] {len(t)} tests: {f[:110]} | {', '.join(o)[:90]}")
    (OUT / "hand-sort.json").write_bytes(json.dumps(rows, indent=1).encode("utf-8"))
    unsorted = [(r["label"], f["function"]) for r in rows for f in r["functions"] if f["sort"] == "unsorted"]
    print("unsorted:", unsorted)
    return 1 if unsorted else 0


if __name__ == "__main__":
    sys.exit(main())
