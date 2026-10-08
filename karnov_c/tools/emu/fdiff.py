#!/usr/bin/env python3
"""fdiff.py DSDUMP HEAPBIN кадр_ds кадр_heap fn... — выполнить функции
(имена из fnreg.c) в оригинале (unicorn) и переводе (--ftest) на одном
снимке; сравнить DS и кучу после каждой. Печатает первое расхождение."""
import os, re, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from orig import Orig, LOAD, HEAP_LIN, HEAP_N, FpuUsed
import dsdiff

KC = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../.."))
F = 0x10000
DSW = 0x1D99
LOW = 0x1A30                      # 0000:0000..1A2F (векторы, BIOS, DOS)


def fnaddr():
    t = open(os.path.join(KC, "src/orig/fnreg.c")).read()
    return {m.group(3): (int(m.group(1), 16), int(m.group(2), 16))
            for m in re.finditer(r"\{ 0x([0-9A-F]+), 0x([0-9A-F]+), \(farfn\)\w+, \"(\w+)\" \}", t)}


def read_frame(path, k, n):
    with open(path, "rb") as f:
        f.seek(k * n)
        return f.read(n)


def norm_orig_ds(x, mask):
    """сегменты far-указателей и сырые слова DS оригинала → вид перевода"""
    x = bytearray(x)
    i = 0
    while i < F - 1:
        if mask[i] == 2:
            v = x[i] | x[i + 1] << 8
            if v:
                v = (v - LOAD) & 0xFFFF
                x[i], x[i + 1] = v & 0xFF, v >> 8
            i += 2
        else:
            i += 1
    for i in range(F - 1):
        if mask[i] and (x[i] | x[i + 1] << 8) == DSW + LOAD:
            x[i], x[i + 1] = DSW & 0xFF, DSW >> 8
    return x


def compare(name, xo, yo, ho, hp, mask, vars_, objf, k=12):
    bad = [i for i in range(F) if mask[i] and xo[i] != yo[i]]
    hb = [i for i in range(HEAP_N) if ho[i] != hp[i]]
    # сегмент DS в куче (узлы списка) — релокация
    hb = [i for i in hb if not any(0 <= j < HEAP_N - 1 and (ho[j] | ho[j + 1] << 8) == DSW + LOAD
                                   and (hp[j] | hp[j + 1] << 8) == DSW for j in (i, i - 1))]
    if not bad and not hb:
        return True
    print(f"после {name}: DS {len(bad)}, куча {len(hb)}")
    shown = set()
    for i in bad:
        n, w = dsdiff.name_of(i, vars_, objf)
        if n in shown:
            continue
        shown.add(n)
        print(f"  DS:{w:04X} {n:32} оригинал {xo[w] | xo[w + 1] << 8:04X} перевод {yo[w] | yo[w + 1] << 8:04X}")
        if len(shown) >= k:
            break
    for i in hb[:k]:
        print(f"  куча {HEAP_LIN + i:05X}: оригинал {ho[i]:02X} перевод {hp[i]:02X}")
    return False


def run(dsdump, heapbin, kd, kh, fns, quiet=False):
    addr = fnaddr()
    ds = read_frame(dsdump, kd, F)
    heap = read_frame(heapbin, kh, HEAP_N)
    tmp = tempfile.mkdtemp()
    open(os.path.join(tmp, "ds"), "wb").write(ds)
    open(os.path.join(tmp, "heap"), "wb").write(heap)
    r = subprocess.run([os.path.join(KC, "karnov"), "--ftest", os.path.join(tmp, "ds"),
                        os.path.join(tmp, "heap"), os.path.join(tmp, "out")] + fns,
                       cwd=os.path.join(KC, ".."), capture_output=True, text=True)
    if r.returncode:
        print("ftest:", r.returncode, r.stderr[-500:])
        return None
    out = open(os.path.join(tmp, "out"), "rb").read()
    mask = out[:F]
    vars_, objf = dsdiff.symbols()
    o = Orig()
    o.load(ds, heap)
    for i, fn in enumerate(fns):
        seg, off = addr[fn]
        try:
            o.call(seg, off)
        except FpuUsed:
            print(f"{fn}: 8087 — не эмулируется")
            return None
        base = F + i * (F + HEAP_N + LOW)
        yo, hp = out[base:base + F], out[base + F:base + F + HEAP_N]
        lo = bytes(o.uc.mem_read(0, LOW))
        lp = out[base + F + HEAP_N:base + F + HEAP_N + LOW]
        lb = [j for j in range(LOW) if lo[j] != lp[j]]
        if lb:
            print(f"после {fn}: нижняя память {len(lb)}: " +
                  " ".join(f"{j:04X}:{lo[j]:02X}/{lp[j]:02X}" for j in lb[:12]))
            return False
        xo = norm_orig_ds(o.ds(), mask)
        if not compare(fn, xo, yo, o.heap(), hp, mask, vars_, objf):
            return False
    if not quiet:
        print("совпадает:", " ".join(fns))
    return True


if __name__ == "__main__":
    a = sys.argv
    run(a[1], a[2], int(a[3]), int(a[4]), a[5:])
