"""Summarise the newest IW4x minidump: exception, faulting module, registers and a
stack scan of return addresses that land in iw4x.exe or our dsound.dll.
Usage: python crash.py <game dir>
"""
import glob
import ntpath
import os
import struct
import sys

from minidump.minidumpfile import MinidumpFile

game = sys.argv[1]
path = max(glob.glob(os.path.join(game, "minidumps", "*.dmp")), key=os.path.getmtime)
print("dump:", os.path.basename(path))
mf = MinidumpFile.parse(path)
mods = [(m.baseaddress, m.baseaddress + m.size, ntpath.basename(m.name)) for m in mf.modules.modules]


def where(a):
    for lo, hi, n in mods:
        if lo <= a < hi:
            return "%s+%X" % (n, a - lo)
    return "?"


exc = mf.exception.exception_records[0]
rec = exc.ExceptionRecord
code = int(getattr(rec.ExceptionCode, "value", rec.ExceptionCode))
print("exception %08X at %08X (%s) info %s" % (code, rec.ExceptionAddress, where(rec.ExceptionAddress),
                                             [hex(x) for x in rec.ExceptionInformation[:2]]))

reader = mf.get_reader()
thread = next(t for t in mf.threads.threads if t.ThreadId == exc.ThreadId)
ctx = thread.ContextObject
regs = {r: getattr(ctx, r, None) for r in ("Eip", "Esp", "Ebp", "Eax", "Ebx", "Ecx", "Edx", "Esi", "Edi")}
print(" ".join("%s=%08X" % (k, v) for k, v in regs.items() if v is not None))

esp = regs["Esp"]
try:
    stack = reader.read(esp, 0x1000)
except Exception:
    stack = b""
print("stack return-address candidates:")
for i in range(0, len(stack) - 3, 4):
    v = struct.unpack_from("<I", stack, i)[0]
    w = where(v)
    if w.startswith(("iw4x.exe", "dsound.dll", "iw4x.dll")):
        print("  [esp+%03X] %08X %s" % (i, v, w))
