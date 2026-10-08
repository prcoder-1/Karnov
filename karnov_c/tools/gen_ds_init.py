#!/usr/bin/env python3
"""Генерирует src/orig/ds_init.c: инициализированная часть DS KARNOV.EXE
(байты с DS:0000 до конца файла) и список DS-смещений far-указателей
(по релокациям) — для проверки, что все они описаны в реестре."""
import os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../.."))
d = open(os.path.join(ROOT, "KARNOV.EXE"), "rb").read()
DSF = 0x1F190
img = d[DSF:]
n, = struct.unpack_from("<H", d, 6)
tab, = struct.unpack_from("<H", d, 0x18)
far = []
for i in range(n):
    o, s = struct.unpack_from("<HH", d, tab + i * 4)
    f = 0x1800 + s * 16 + o
    if f >= DSF:
        far.append(f - DSF - 2)
far.sort()
out = os.path.join(ROOT, "karnov_c/src/orig/ds_init.c")
with open(out, "w") as fh:
    fh.write("/* Сгенерировано tools/gen_ds_init.py — не править. Образ\n"
             " * инициализированного DS KARNOV.EXE (file 0x1F190..). */\n")
    fh.write('#include "orig/ds.h"\n\n')
    fh.write(f"const unsigned ds_init_size = 0x{len(img):X};\n")
    fh.write(f"const u8 ds_init_image[0x{len(img):X}] = {{\n")
    for i in range(0, len(img), 16):
        fh.write("    " + ",".join(f"0x{b:02X}" for b in img[i:i + 16]) + ",\n")
    fh.write("};\n\n")
    fh.write("/* DS-смещения far-указателей (off, seg) в образе. */\n")
    fh.write(f"const unsigned ds_init_nfar = {len(far)};\n")
    fh.write(f"const u16 ds_init_far[{len(far)}] = {{\n")
    for i in range(0, len(far), 10):
        fh.write("    " + ",".join(f"0x{x:04X}" for x in far[i:i + 10]) + ",\n")
    fh.write("};\n")
print("→", out, hex(len(img)), len(far))
