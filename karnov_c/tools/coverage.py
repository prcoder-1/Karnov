#!/usr/bin/env python3
"""coverage.py — прогресс перевода: функции манифеста (functions.csv) против
тегов @SEG:OFF в src/orig. Сводка по сегментам; -v — список непереведённых;
SEG — подробно по одному сегменту."""
import csv, os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_fn

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    verbose = "-v" in sys.argv
    only = [a for a in sys.argv[1:] if a != "-v"]
    only = int(only[0], 16) if only else None
    man = {}
    for r in csv.DictReader(open(os.path.join(HERE, "functions.csv"))):
        s, o = (int(x, 16) for x in r["addr"].split(":"))
        man[(s, o)] = r
    funcs, errors = gen_fn.collect()
    for e in errors:
        print("!", e)
    tagged = {(f["seg"], f["off"]): f for f in funcs}
    for k, f in sorted(tagged.items()):
        if k not in man:
            print(f"! @{k[0]:04X}:{k[1]:04X} {f['name']} — не начало функции в манифесте")
    per = collections.OrderedDict()
    for k in sorted(man):
        r = man[k]
        if r["class"] != "game" and k not in tagged:
            continue
        seg = k[0]
        d = per.setdefault(seg, [0, 0, 0, 0, []])
        d[0] += 1
        d[2] += int(r["ninsn"])
        if k in tagged:
            d[1] += 1
            d[3] += int(r["ninsn"])
        else:
            d[4].append(r)
    T = [sum(d[i] for d in per.values()) for i in range(4)]
    print(f"{'seg':6} {'функций':>9} {'инструкций':>13}")
    for seg, d in per.items():
        if only is not None and seg != only:
            continue
        mark = "✓" if d[0] == d[1] else " "
        print(f"{seg:04X} {mark} {d[1]:4d}/{d[0]:<4d} {d[3]:6d}/{d[2]:<6d}")
        if verbose or only is not None:
            for r in d[4]:
                print(f"      {r['addr']} {r['name'] or '-':24} {r['ninsn']:>5} инстр.")
    print(f"итого  {T[1]}/{T[0]} функций, {T[3]}/{T[2]} инструкций "
          f"({100 * T[3] / max(1, T[2]):.1f}%)")


if __name__ == "__main__":
    main()
