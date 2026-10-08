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
# не резать: смещения полей внутри таблиц (адресуются [bx+база+2])
# таблицы масок упоров игрока (CollRes.masks, 5 слов) — 0E62
MASKTABS = (0x1510, 0x151A, 0x1546, 0x1580, 0x15B6, 0x164A, 0x169A)
NOCUT = {0x19F6, 0x19FC, 0x19FE, 0x1BFA, 0x1C04, 0x1C6A, 0x1C76, 0x1F0E, 0x1F26}
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
    # непосредственные операнды игрового кода (mov ..., смещение в DS)
    import x86map
    from capstone import x86 as X
    for (seg, off), f in x86map.scan().items():
        if x86map.klass(seg) != "game":
            continue
        for i in f["insns"].values():
            for o in i.operands:
                v = None
                if o.type == X.X86_OP_IMM:
                    v = o.imm & 0xFFFF
                elif o.type == X.X86_OP_MEM and o.mem.segment in (0, X.X86_REG_DS) \
                        and o.mem.base not in (0, X.X86_REG_BP):
                    v = o.mem.disp & 0xFFFF
                if v is not None and LO <= v < HI and not v & 1 and not covered[v]:
                    cuts.add(v)
    # массивы кадров и смещений под StateEntry: [n, кадры×n] и [(dx, dy)×n]
    # — внутри них не резать (адресуются указателем + индекс)
    spans = []
    for a, b in runs:
        for e in range(a, b, 8):
            pa, po = fp.get(e), fp.get(e + 4)
            if pa is None or po is None or not (LO <= pa < HI):
                continue
            n, = struct.unpack_from("<H", d, DSF + pa)
            if n > 64:
                continue
            spans.append((pa, pa + 2 * (n + 1)))
            if LO <= po < HI:
                spans.append((po, po + 4 * n))
    spans += [(t, t + 10) for t in MASKTABS]
    spans += [(0x15B6, 0x15CA), (0x164A, 0x165E)]   # + флаги упоров [dir + 5]
    cuts = sorted(c for c in cuts if not any(a < c < b for a, b in runs)
                  and not any(a < c < b for a, b in spans) and c not in NOCUT)
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
