"""M1-131 item 4 (decision 559): the six rebuilds of one tree, set side by side.

    python -I compare-builds.py <short>          (rel or debug)

Reads, for the labels <short>-nohook-1..3 and <short>-hook-1..3, the copies of
.ninja_log that measure-build.py kept (the build's entries and the check's)
and the driver's own lines, and groups each build's compiles by the object's
path, so that the same objects are set against each other both ways:

  * hooked: an object of the project's own (CMakeFiles/<target>.dir/src/... or
    .../tests/...) in a target that carries the hook when it is on -- the same
    objects in both states;
  * own, never hooked: orbsim_stb_image and vma_impl, which compile someone
    else's code from a file under src/ (decision 588);
  * only with the hook: the recorder and the self-test's three programs, which
    exist only when the option is on;
  * header self-checks; someone else's code in top-level targets (ERFA,
    lodepng); the dependencies' compiles (_deps/).

The dependencies' compiles do not change with the hook, so their sum is a
measure of how fast the machine ran during that build; "hooked / dependencies"
divides one by the other. "other compiles, same objects" adds the classes
that hold the same objects in both states and never carry the hook (own,
never hooked; header self-checks; someone else's; dependencies), and "hooked /
other compiles" divides by that sum. Printed: each build's figures, then the
mean, the sample standard deviation, the lowest and the highest of each state,
and the difference of the means.
"""

import collections
import re
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NEVER_HOOKED = {"orbsim_stb_image", "vma_impl"}
ONLY_WITH_HOOK = {"orbsim_coverage_recorder", "coverage_recorder_probe", "coverage_recorder_unaligned",
                  "coverage_recorder_probe_plain"}
ELSE_TOP = {"orbsim_erfa", "orbsim_lodepng", "t_erfa_c", "t_erfa_c_extra"}
HOOK_PROGRAMS = {"coverage_recorder_probe.exe", "coverage_recorder_unaligned.exe",
                 "coverage_recorder_probe_plain.exe"}


def edges(before: Path, after: Path) -> list:
    old, new = before.read_bytes(), after.read_bytes()
    if not new.startswith(old):
        raise SystemExit(f"{after.name} does not begin with {before.name}")
    grouped = collections.defaultdict(list)
    for line in new[len(old):].decode("utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        start, end, _, output, command = line.split("\t")
        grouped[(int(start), int(end), command)].append(output.replace("\\", "/"))
    return [(end - start, outputs) for (start, end, _), outputs in grouped.items()]


def classify(outputs: list) -> str:
    for output in outputs:
        if output.endswith(".obj"):
            if output.startswith("_deps/"):
                return "dependencies"
            match = re.match(r"CMakeFiles/([^/]+)\.dir/([^/]+)/", output)
            if not match:
                return "objects elsewhere"
            target, top = match.groups()
            if top == "selfcheck":
                return "header self-checks"
            if target in ELSE_TOP:
                return "someone else's, top-level"
            if target in NEVER_HOOKED:
                return "own, never hooked"
            if target in ONLY_WITH_HOOK:
                return "only with the hook"
            if top in ("src", "tests"):
                return "hooked"
            return "objects elsewhere"
    for output in outputs:
        if output.endswith(".exe") and "/" not in output:
            return "links: recorder self-test programs" if output in HOOK_PROGRAMS else "links: other programs"
        if output.endswith(".exe") or output.endswith(".dll"):
            return "links: dependencies' programs"
        if output.endswith(".lib"):
            return "static libraries"
    return "other"


def driver_seconds(label: str, step: str) -> float:
    text = (HERE / f"{label}-driver.txt").read_text(encoding="utf-8")
    return float(re.search(rf"\] {step}: exit \d+, ([0-9.]+) s wall clock", text).group(1))


def main(argv: list) -> int:
    short = argv[1]
    rows = {}
    for kind in ("nohook", "hook"):
        for n in (1, 2, 3):
            label = f"{short}-{kind}-{n}"
            if not (HERE / f"{label}-ninja_log-after-check.txt").exists():
                continue
            sums, counts = collections.Counter(), collections.Counter()
            for ms, outputs in edges(HERE / f"{label}-ninja_log-before-build.txt",
                                     HERE / f"{label}-ninja_log-after-build.txt"):
                kind_of = classify(outputs)
                sums[kind_of] += ms / 1000
                counts[kind_of] += 1
            check_sums = collections.Counter()
            for ms, outputs in edges(HERE / f"{label}-ninja_log-after-build.txt",
                                     HERE / f"{label}-ninja_log-after-check.txt"):
                name = outputs[0]
                key = "lint" if name.startswith("lint/") else "test run" if name.endswith("CMakeFiles/check") \
                    else "other"
                check_sums[key] += ms / 1000
            row = {"build wall clock": driver_seconds(label, "build"),
                   "check wall clock": driver_seconds(label, "check")}
            for key in ("hooked", "own, never hooked", "only with the hook", "header self-checks",
                        "someone else's, top-level", "dependencies", "links: other programs",
                        "links: recorder self-test programs", "static libraries", "objects elsewhere", "other"):
                row[f"{key} ({counts[key]})"] = sums[key]
            row["hooked / dependencies"] = sums["hooked"] / sums["dependencies"]
            row["every compile but hooked"] = (sums["own, never hooked"] + sums["only with the hook"]
                                               + sums["header self-checks"] + sums["someone else's, top-level"]
                                               + sums["dependencies"])
            row["other compiles, same objects"] = (sums["own, never hooked"] + sums["header self-checks"]
                                                   + sums["someone else's, top-level"] + sums["dependencies"])
            row["hooked / other compiles"] = sums["hooked"] / row["other compiles, same objects"]
            row["links: all programs"] = sums["links: other programs"] + sums["links: recorder self-test programs"]
            row["check: lint summed"] = check_sums["lint"]
            row["check: the test run's step"] = check_sums["test run"]
            rows[label] = row
    keys = []
    for row in rows.values():
        for key in row:
            if key not in keys:
                keys.append(key)
    labels = list(rows)
    print(f"{'':44}" + "".join(f"{label:>16}" for label in labels))
    for key in keys:
        print(f"{key:44}" + "".join(f"{rows[label].get(key, float('nan')):16.3f}" if "/" in key
                                     else f"{rows[label].get(key, float('nan')):16.1f}" for label in labels))
    print()
    print(f"{'':44}{'without: mean':>14}{'sd':>8}{'low':>9}{'high':>9}{'with: mean':>12}{'sd':>8}{'low':>9}"
          f"{'high':>9}{'difference':>12}{'%':>8}")
    for key in keys:
        off = [rows[l][key] for l in labels if "-nohook-" in l and key in rows[l]]
        on = [rows[l][key] for l in labels if "-hook-" in l and key in rows[l]]
        if not off or not on:
            continue
        mean_off, mean_on = statistics.mean(off), statistics.mean(on)
        sd_off = statistics.stdev(off) if len(off) > 1 else float("nan")
        sd_on = statistics.stdev(on) if len(on) > 1 else float("nan")
        share = 100 * (mean_on - mean_off) / mean_off if mean_off else float("nan")
        digits = 3 if "/" in key else 1
        print(f"{key:44}{mean_off:14.{digits}f}{sd_off:8.{digits}f}{min(off):9.{digits}f}{max(off):9.{digits}f}"
              f"{mean_on:12.{digits}f}{sd_on:8.{digits}f}{min(on):9.{digits}f}{max(on):9.{digits}f}"
              f"{mean_on - mean_off:12.{digits}f}{share:8.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
