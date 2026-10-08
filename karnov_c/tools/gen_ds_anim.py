#!/usr/bin/env python3
"""gen_ds_anim.py — объявления области анимаций DS (0x13F8..0x1F8E) по
релокациям: пары far-указателей (кадры, смещения) → StateEntry[],
массивы кадров/смещений → s16[], разрезанные по целям указателей.
Адреса, уже объявленные в ds_vars.h, пропускаются.
→ include/orig/ds_anim.h (включается в конце ds_vars.h)."""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsdiff

HERE = os.path.dirname(os.path.abspath(__file__))
KC = os.path.normpath(os.path.join(HERE, ".."))
LO, HI = 0x13F8, 0x1F8E
d = open(os.path.join(KC, "../KARNOV.EXE"), "rb").read()
DSF = 0x1F190


def far_ptrs():
    n, = struct.unpack_from("<H", d, 6)
    tab, = struct.unpack_from("<H", d, 0x18)
    out = {}
    for i in range(n):
        o, s = struct.unpack_from("<HH", d, tab + i * 4)
        f = 0x1800 + s * 16 + o
        if f >= DSF:
            a = f - DSF - 2
            out[a] = struct.unpack_from("<H", d, DSF + a)[0]
    return out


def main():
    # ручные объявления (без сгенерированного файла)
    gen = os.path.join(KC, "include/orig/ds_anim.h")
    if os.path.exists(gen):
        os.rename(gen, gen + ".old")
    try:
        vars_, _ = dsdiff.symbols()
    finally:
        if os.path.exists(gen + ".old"):
            os.rename(gen + ".old", gen)
    covered = bytearray(0x10000)
    for a, n, size, cnt, typ in vars_:
        for k in range(a, min(0x10000, a + size * cnt)):
            covered[k] = 1
    fp = far_ptrs()
    runs, cur = [], None
    for a in sorted(fp):
        if not (LO <= a < HI) or covered[a]:
            continue
        if cur and a == cur[1]:
            cur[1] = a + 4
        else:
            cur = [a, a + 4]
            runs.append(cur)
    cuts = {LO, HI}
    for a, b in runs:
        cuts |= {a, b}
    for a, t in fp.items():
        if LO <= a < HI and LO <= t < HI:
            cuts.add(t)
    cuts = sorted(cuts)
    lines = ["/* Сгенерировано tools/gen_ds_anim.py — не править.",
             " * Область анимаций DS: StateEntry — пары (кадры, смещения);",
             " * an_XXXX — массивы кадров (слово 0 — число кадров) и смещений. */"]
    run_at = {a: b for a, b in runs}
    for a, b in zip(cuts, cuts[1:]):
        if a in run_at:
            n = (run_at[a] - a) // 8
            lines.append(f"DSS(StateEntry, sl_{a:04X}, 0x{a:04X}, {n})")
            continue
        # массив s16 по непокрытым байтам
        k = a
        while k < b:
            if covered[k]:
                k += 1
                continue
            e = k
            while e < b and not covered[e]:
                e += 1
            if (e - k) % 2:
                lines.append(f"DSA(u8,  an_{k:04X}, 0x{k:04X}, {e - k})")
            else:
                lines.append(f"DSA(s16, an_{k:04X}, 0x{k:04X}, {(e - k) // 2})")
            k = e
    open(gen, "w").write("\n".join(lines) + "\n")
    print("→", gen, len(lines) - 3, "объявлений")


if __name__ == "__main__":
    main()
