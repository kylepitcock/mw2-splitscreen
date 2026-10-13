"""Static-analysis helpers for iw4mp 1.2.159 (iw4x.exe).

Linear-sweeps .text with Capstone once, caches the result, and indexes:
  * code/data references (immediates and memory displacements into the image)
  * call targets (used as function starts)
Usage:  python iw4.py <path-to-iw4x.exe>   (builds cache)
        import iw4; db = iw4.load(exe)
"""
import bisect
import os
import pickle
import re
import struct
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone import x86 as X

CACHE_VERSION = 3


class DB:
    def __init__(self, exe):
        self.exe = exe
        pe = pefile.PE(exe, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            va = self.base + s.VirtualAddress
            self.sections.append((name, va, va + max(s.Misc_VirtualSize, s.SizeOfRawData), s.get_data()))
        self.text = next(s for s in self.sections if s[0] == ".text")
        self.lo = self.base
        self.hi = max(s[2] for s in self.sections)

    # ---------------------------------------------------------------- memory
    def section_of(self, va):
        for s in self.sections:
            if s[1] <= va < s[2]:
                return s
        return None

    def read(self, va, n):
        s = self.section_of(va)
        if not s:
            return None
        off = va - s[1]
        return s[3][off:off + n]

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def cstr(self, va, maxlen=256):
        b = self.read(va, maxlen) or b""
        return b.split(b"\0")[0].decode("latin-1")

    def find_bytes(self, needle, section=None):
        out = []
        for name, va, end, data in self.sections:
            if section and name != section:
                continue
            for m in re.finditer(re.escape(needle), data):
                out.append(va + m.start())
        return out

    def find_string(self, s):
        """Addresses of NUL-terminated exact string s."""
        hits = self.find_bytes(s.encode("latin-1") + b"\0")
        return [h for h in hits if (self.read(h - 1, 1) or b"\0") == b"\0"]

    # ------------------------------------------------------------- disasm
    def build(self):
        name, va, end, data = self.text
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        md.detail = True
        md.skipdata = True
        addrs, sizes, texts = [], [], []
        refs = {}
        calls = {}
        for ins in md.disasm(data, va):
            addrs.append(ins.address)
            sizes.append(ins.size)
            texts.append(ins.mnemonic + " " + ins.op_str)
            if ins.mnemonic == ".byte":
                continue
            for op in ins.operands:
                v = None
                if op.type == X.X86_OP_IMM:
                    v = op.imm & 0xFFFFFFFF
                elif op.type == X.X86_OP_MEM:
                    v = op.mem.disp & 0xFFFFFFFF
                if v is not None and self.lo <= v < self.hi:
                    if ins.mnemonic == "call" and op.type == X.X86_OP_IMM:
                        calls.setdefault(v, []).append(ins.address)
                    else:
                        refs.setdefault(v, []).append(ins.address)
            if ins.mnemonic.startswith("j") and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                # jumps are refs too (tail calls / jump tables), keep separately
                pass
        self.addrs, self.sizes, self.texts, self.refs, self.calls = addrs, sizes, texts, refs, calls
        self.func_starts = sorted(set(calls) | self._prologues())

    def _prologues(self):
        out = set()
        for i, a in enumerate(self.addrs):
            # function after int3/nop padding
            if not i or self.texts[i].startswith(("int3", "nop", ".byte")):
                continue
            prev = self.texts[i - 1]
            if prev.startswith("int3") or (prev.startswith("ret") and a % 16 == 0):
                out.add(a)
        return out

    def save(self, path):
        with open(path, "wb") as f:
            pickle.dump((CACHE_VERSION, self.addrs, self.sizes, self.texts, self.refs, self.calls, self.func_starts), f, protocol=4)

    def restore(self, path):
        with open(path, "rb") as f:
            v, *rest = pickle.load(f)
        if v != CACHE_VERSION:
            return False
        self.addrs, self.sizes, self.texts, self.refs, self.calls, self.func_starts = rest
        return True

    # ------------------------------------------------------------- queries
    def index_of(self, va):
        i = bisect.bisect_right(self.addrs, va) - 1
        return i if i >= 0 and self.addrs[i] <= va < self.addrs[i] + self.sizes[i] else None

    def func_of(self, va):
        i = bisect.bisect_right(self.func_starts, va) - 1
        return self.func_starts[i] if i >= 0 else None

    def func_end(self, start):
        i = bisect.bisect_right(self.func_starts, start)
        return self.func_starts[i] if i < len(self.func_starts) else self.text[2]

    def insns(self, start, end=None):
        end = end or self.func_end(start)
        i = self.index_of(start)
        while i is not None and i < len(self.addrs) and self.addrs[i] < end:
            yield self.addrs[i], self.texts[i]
            i += 1

    def dis(self, start, end=None, limit=400):
        out = []
        for n, (a, t) in enumerate(self.insns(start, end)):
            if n >= limit:
                out.append("   ...")
                break
            out.append("%08X  %s%s" % (a, t, self.annotate(t)))
        return "\n".join(out)

    def annotate(self, text):
        notes = []
        for m in re.finditer(r"0x([0-9a-f]{6,8})", text):
            v = int(m.group(1), 16)
            s = self.section_of(v)
            if s and s[0] == ".rdata":
                st = self.cstr(v, 80)
                if len(st) >= 3 and all(32 <= ord(c) < 127 or c in "\n\t" for c in st):
                    notes.append(repr(st))
        return ("    ; " + " ".join(notes)) if notes else ""

    def xrefs(self, va):
        return sorted(set(self.refs.get(va, []) + self.calls.get(va, [])))

    def callers(self, fn):
        return sorted(set(self.func_of(a) for a in self.calls.get(fn, [])))

    def string_users(self, s):
        out = []
        for a in self.find_string(s):
            for x in self.xrefs(a):
                out.append((a, x, self.func_of(x)))
        return out


def load(exe, cache=None):
    db = DB(exe)
    cache = cache or os.environ.get("IW4_CACHE") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "iw4.cache")
    if not (os.path.exists(cache) and db.restore(cache)):
        db.build()
        db.save(cache)
    return db


if __name__ == "__main__":
    db = load(sys.argv[1])
    print("insns:", len(db.addrs), "funcs:", len(db.func_starts), "refs:", len(db.refs))


def callees(db, fn):
    out = []
    for a, t in db.insns(fn):
        if t.startswith("call 0x"):
            out.append((a, int(t[5:], 16)))
    return out


_WRITE_RE = None


def abs_writes(db, fn, depth=2, seen=None):
    """Absolute data addresses written by fn and its callees (to `depth`)."""
    import re as _re
    global _WRITE_RE
    if _WRITE_RE is None:
        _WRITE_RE = _re.compile(r"^(mov|add|sub|or|and|xor|inc|dec|movss|movsd|fstp|fistp|movq|movdqa|movdqu|movaps|movups|stos\w*|lock \w+|xchg)\b.*?(?:byte|word|dword|qword|xmmword|tbyte) ptr \[([^\]]*)\],")
    seen = seen if seen is not None else set()
    if fn in seen:
        return set()
    seen.add(fn)
    out = set()
    for a, t in db.insns(fn):
        m = _WRITE_RE.match(t)
        if m:
            for h in _re.findall(r"0x([0-9a-f]{6,8})", m.group(2)):
                v = int(h, 16)
                if 0x795000 <= v < 0x7015900:
                    out.add(v)
        if depth > 0 and t.startswith("call 0x"):
            out |= abs_writes(db, int(t[5:], 16), depth - 1, seen)
    return out
