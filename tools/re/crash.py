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
# The faulting CONTEXT lives in the exception stream (the thread list holds the dump writer's).
loc = exc.ThreadContext
raw = open(path, "rb").read()[loc.Rva:loc.Rva + loc.DataSize]
offsets = {"Edi": 0x9C, "Esi": 0xA0, "Ebx": 0xA4, "Edx": 0xA8, "Ecx": 0xAC, "Eax": 0xB0, "Ebp": 0xB4, "Eip": 0xB8,
           "Esp": 0xC4}
regs = {k: struct.unpack_from("<I", raw, o)[0] for k, o in offsets.items()}
print(" ".join("%s=%08X" % (k, v) for k, v in regs.items()))

esp = regs["Esp"]
thread = next(t for t in mf.threads.threads if t.ThreadId == exc.ThreadId)
lo = thread.Stack.StartOfMemoryRange
mem = thread.Stack.MemoryLocation
stackbytes = raw_all = open(path, "rb").read()[mem.Rva:mem.Rva + mem.DataSize]
start = max(0, esp - lo)
stack = stackbytes[start:start + 0x2000]
code_lo, code_hi = 0x401000, 0x6D6320  # iw4x.exe .text
print("stack return-address candidates (code only):")
shown = 0
for i in range(0, len(stack) - 3, 4):
    v = struct.unpack_from("<I", stack, i)[0]
    w = where(v)
    if (code_lo <= v < code_hi) or w.startswith(("dsound.dll", "iw4x.dll")):
        print("  [esp+%03X] %08X %s" % (i, v, w))
        shown += 1
        if shown > 40:
            break
