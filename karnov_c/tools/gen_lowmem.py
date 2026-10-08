#!/usr/bin/env python3
"""gen_lowmem.py MEM.BIN → src/orig/hw_lowmem.c: память DOSBox ниже
программы (0..1A30: векторы, BIOS, DOS, PSP) и рабочие данные в сегментах
кода, отличающиеся от образа EXE. MEM.BIN — дамп 0000:0000.. из DSDUMP.EXE
(HSEG=0 HSKIP=300 HCOUNT=1 tools/dsdump/run.sh)."""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "../../KARNOV.EXE")
LOAD = 0x1A3
LOW = LOAD * 16
DSL = 0x1F3C0
HOOK = [(0x04E4 * 16 + 0x350, 8), (0x1851 * 16, 200)]   # правки DSDUMP.EXE


def exe_image():
    e = open(EXE, "rb").read()
    img = bytearray(e[0x1800:])
    n, = struct.unpack_from("<H", e, 6)
    tab, = struct.unpack_from("<H", e, 0x18)
    for i in range(n):
        o, s = struct.unpack_from("<HH", e, tab + 4 * i)
        v, = struct.unpack_from("<H", img, s * 16 + o)
        struct.pack_into("<H", img, s * 16 + o, (v + LOAD) & 0xFFFF)
    return img


def main(mem, out):
    m = open(mem, "rb").read()
    img = exe_image()
    m = bytearray(m)
    m[0x417] = 0                # до старта; 0x70 туда пишет read_key
    skip = set()
    for a, n in HOOK:
        skip.update(range(a, a + n))
    diff = [i for i in range(DSL - LOW) if m[LOW + i] != img[i] and i not in skip]
    runs = []
    for i in diff:
        if runs and i == runs[-1][0] + len(runs[-1][1]):
            runs[-1][1].append(m[LOW + i])
        else:
            runs.append([i, [m[LOW + i]]])
    L = ["/* hw_lowmem.c — сгенерирован tools/gen_lowmem.py из памяти DOSBox-Staging",
         " * (machine=ega). 0000:0000..1A2F — векторы, BIOS, DOS, PSP; сегменты",
         " * векторов игры — при загрузке с 0x01A3. 0417 — значение до старта. */",
         '#include "orig/hw.h"', "",
         "const u8 hw_lowmem_init[0x%X] = {" % LOW]
    for i in range(0, LOW, 16):
        L.append("    " + " ".join("0x%02X," % b for b in m[i:i + 16]))
    L += ["};", "",
          "/* Рабочие данные в сегментах кода (смещение от начала образа EXE). */",
          "const HwPatch hw_code_patch[] = {"]
    for a, b in runs:
        L.append("    { 0x%05X, %d, { %s } }," % (a, len(b), ", ".join("0x%02X" % x for x in b)))
    L += ["    { 0, 0, { 0 } }", "};"]
    open(out, "w").write("\n".join(L) + "\n")
    print(out, len(runs), "патчей", max(len(b) for a, b in runs), "макс. байт")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else
         os.path.join(HERE, "../src/orig/hw_lowmem.c"))
