#!/usr/bin/env python3
"""dsobj.py DUMP кадр объект [--load SEG] — поля ActiveObject из дампа DS."""
import re, struct, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
F = 0x10000


def fields():
    h = open(os.path.join(HERE, "../include/orig/ds.h"), encoding="utf-8").read()
    body = h[h.index("#define OBJ_FIELDS"):h.index("typedef struct ActiveObject")]
    out = []
    for m in re.finditer(r"([RAPC])\((0x[0-9A-F]+),\s*(?:([\w ]+),\s*)?(\w+)", body):
        out.append((int(m.group(2), 16), m.group(1), m.group(4)))
    return out


def main():
    path, f, k = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    with open(path, "rb") as fp:
        fp.seek(f * F)
        d = fp.read(F)
    base = 0x2824 + k * 0xF5
    w = lambda o: struct.unpack_from("<H", d, base + o)[0]
    for off, kind, name in fields():
        if kind == "A":
            for b in range(8):
                v = [struct.unpack_from("<h", d, base + 0x20 + b * 18 + j * 2)[0] for j in range(9)]
                if any(v):
                    print(f"  box[{b}] spr {v[0]:#x} d=({v[1]},{v[2]}) ({v[3]},{v[4]})-({v[5]},{v[6]}) hit {v[7]} vis {v[8]}")
        elif kind in "PC":
            print(f"  {name:14} {w(off + 2):04X}:{w(off):04X}")
        elif name == "state_byte":
            print(f"  {name:14} {d[base + off]}")
        else:
            print(f"  {name:14} {struct.unpack_from('<h', d, base + off)[0]}")


main()
