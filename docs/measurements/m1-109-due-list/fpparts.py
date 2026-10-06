"""Show what differs in one program's fingerprint between two checkouts of one
commit (M1-109): `python fpparts.py <checkout A> <checkout B> build/relwithdebinfo
orbsim.exe`. Prints the differing build commands, with the trees' paths
replaced, and the untracked input files whose contents differ."""
import importlib.util, pathlib, sys, difflib
def load(root):
    spec = importlib.util.spec_from_file_location("md" + str(abs(hash(root))), pathlib.Path(root) / "scripts/mutants-due.py")
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m
target = sys.argv[4]
parts = []
for root in sys.argv[1:3]:
    M = load(root); t = M.Tree(M.ROOT / sys.argv[3])
    cmds = M.placeholders(M.run([t.ninja, "-t", "commands", target], t.tree), t.tree, M.ROOT)
    files = {M.placeholders(p, t.tree, M.ROOT): t.content_hash(p) for p in t.build_inputs(target)
             if p not in t.tracked() and p not in t.outputs()}
    parts.append((cmds.splitlines(), files))
(c1, f1), (c2, f2) = parts
d = [l for l in difflib.unified_diff(c1, c2, lineterm="", n=0) if not l.startswith(("---", "+++", "@@"))]
print("command lines differing:", len(d)); [print("  ", l[:300]) for l in d[:6]]
ks = sorted(set(f1) | set(f2))
fd = [k for k in ks if f1.get(k) != f2.get(k)]
print("untracked inputs:", len(f1), "vs", len(f2), "; differing:", len(fd)); [print("  ", k, f1.get(k, "-")[:10], f2.get(k, "-")[:10]) for k in fd[:10]]
