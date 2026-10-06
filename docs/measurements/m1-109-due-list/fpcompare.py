"""Compare every mutant file's judge fingerprints between two checkouts of one
commit (M1-109): `python fpcompare.py <checkout A> <checkout B> build/debug`.
Each checkout's own scripts/mutants-due.py computes its fingerprints; the
result counts the judges that agree and names those that differ."""
import importlib.util, json, pathlib, sys
def load(root):
    spec = importlib.util.spec_from_file_location("md" + str(abs(hash(root))), pathlib.Path(root) / "scripts/mutants-due.py")
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m
a_root, b_root, tree = sys.argv[1], sys.argv[2], sys.argv[3]
A, B = load(a_root), load(b_root)
ta, tb = A.Tree(A.ROOT / tree), B.Tree(B.ROOT / tree)
same = diff = 0
for f in sorted((A.ROOT / "scripts/mutants").glob("*.json")):
    s = json.loads(f.read_text(encoding="utf-8"))
    fa, fb = ta.fingerprints(s), tb.fingerprints(s)
    d = [k for k in fa if fa[k] != fb.get(k)]
    same += len(fa) - len(d); diff += len(d)
    if d: print(f.name, "differs:", d[:4])
print(f"{tree}: {same} judges agree, {diff} differ")
