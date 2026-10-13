"""Run ad-hoc analysis: python q.py <exe> <file.py | -> with `db` in scope."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iw4
db = iw4.load(sys.argv[1])
H = lambda v: "%08X" % v
src = sys.stdin.read() if sys.argv[2] == "-" else open(sys.argv[2]).read()
exec(compile(src, "query", "exec"))
