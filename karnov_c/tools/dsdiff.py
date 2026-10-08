#!/usr/bin/env python3
"""dsdiff.py — покадровая сверка DS: дамп оригинала (DOSBox + DSDUMP.EXE)
против дампа перевода (KARNOV_DSDUMP), только по байтам из маски перевода.

  dsdiff.py ORIG.BIN PORT.BIN MASK.BIN [-n кадров] [-k показать_адресов]
            [--ignore 0xA-0xB,...]
Печатает первый расходящийся кадр и имена переменных/полей объектов."""
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
KC = os.path.normpath(os.path.join(HERE, ".."))
F = 0x10000
# Нестабильно в самом оригинале (два прогона расходятся): ввод/джойстик.
IGNORE = [(0xCC9C, 0xCCA0)]


def symbols():
    vars_, objf = [], {}
    t = open(os.path.join(KC, "include/orig/ds_vars.h"), encoding="utf-8").read()
    gen = os.path.join(KC, "include/orig/ds_anim.h")
    if os.path.exists(gen):
        t += open(gen, encoding="utf-8").read()
    for m in re.finditer(r"^DS(V|A|S|P|PA|F|FA|X|VX)\(([^)]*)\)", t, re.M):
        a = [x.strip() for x in m.group(2).split(",")]
        k = m.group(1)
        if k in ("F", "FA"):
            name, addr = a[0], int(a[1], 16)
            cnt = int(a[2], 0) if k == "FA" else 1
            vars_.append((addr, name, 4, cnt, None))
            continue
        typ, name, addr = a[0], a[1], int(a[2], 16)
        cnt = int(a[3], 0) if len(a) > 3 and k != "VX" else 1
        size = 4 if k in ("P", "PA") else \
            {"u8": 1, "s8": 1, "char": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4}.get(typ)
        if k == "VX":
            size = int(a[3], 0)
        if k == "X":
            size = int(a[4], 0)
        stype = typ if k in ("S", "A") and size is None else None
        vars_.append((addr, name, size or 1, cnt, stype))
    h = open(os.path.join(KC, "include/orig/ds.h"), encoding="utf-8").read()
    for i, (a, n, size, cnt, typ) in enumerate(vars_):
        m = re.search(r"#define %s_SIZE (0x[0-9A-Fa-f]+|\d+)" % (typ or "@@"), h)
        if m:
            vars_[i] = (a, n, int(m.group(1), 0), cnt, typ)
    for m in re.finditer(r"^\s+[RAPC]\((0x[0-9A-F]+),\s*(?:[\w ]+,\s*)?(\w+)", h, re.M):
        objf[int(m.group(1), 16)] = m.group(2)
    return vars_, objf


def name_of(addr, vars_, objf):
    for a, n, size, cnt, typ in vars_:
        if a <= addr < a + size * cnt:
            i, r = divmod(addr - a, size)
            if typ == "ActiveObject":
                fo = max(o for o in objf if o <= r)
                fn = objf[fo]
                if 0x20 <= r < 0xB0:
                    b, br = divmod(r - 0x20, 18)
                    fn = f"box[{b}].{['sprite','dx','dy','x0','y0','x1','y1','hitflag','was_vis'][br // 2]}"
                    fo = 0x20 + b * 18 + br // 2 * 2
                return f"{n}[{i}].{fn}" + (f"+{r - fo}" if r != fo else "")
            return f"{n}[{i}]" + (f"+{r}" if r else "") if cnt > 1 else n + (f"+{r}" if r else "")
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("orig"); ap.add_argument("port"); ap.add_argument("mask")
    ap.add_argument("-n", type=int, default=0)
    ap.add_argument("-k", type=int, default=24)
    ap.add_argument("--ignore", default="")
    a = ap.parse_args()
    ign = list(IGNORE)
    for r in filter(None, a.ignore.split(",")):
        lo, hi = r.split("-")
        ign.append((int(lo, 0), int(hi, 0) + 1))
    mask = bytearray(open(a.mask, "rb").read())
    for lo, hi in ign:
        mask[lo:hi] = bytes(hi - lo)
    idx = [i for i in range(F) if mask[i]]
    vars_, objf = symbols()
    fo, fp = open(a.orig, "rb"), open(a.port, "rb")
    nf = min(os.path.getsize(a.orig), os.path.getsize(a.port)) // F
    if a.n:
        nf = min(nf, a.n)
    print(f"кадров: оригинал {os.path.getsize(a.orig) // F}, перевод "
          f"{os.path.getsize(a.port) // F}; сравниваю {nf}, байт в маске {len(idx)}")
    for f in range(nf):
        x, y = fo.read(F), fp.read(F)
        bad = [i for i in idx if x[i] != y[i]]
        if not bad:
            continue
        print(f"кадр {f}: расхождений {len(bad)}")
        shown = set()
        for i in bad:
            n = name_of(i, vars_, objf)
            key = re.sub(r"\+\d+$", "", n)
            if key in shown:
                continue
            shown.add(key)
            w = i & ~1 if mask[i & ~1] and mask[(i & ~1) + 1] else i
            if w + 1 < F and mask[w + 1] and w == i & ~1:
                ov, pv = x[w] | x[w + 1] << 8, y[w] | y[w + 1] << 8
                print(f"  DS:{w:04X} {n:32} оригинал {ov:04X} перевод {pv:04X}")
            else:
                print(f"  DS:{i:04X} {n:32} оригинал {x[i]:02X} перевод {y[i]:02X}")
            if len(shown) >= a.k:
                break
        return 1
    print("совпадает")
    return 0


if __name__ == "__main__":
    sys.exit(main())
