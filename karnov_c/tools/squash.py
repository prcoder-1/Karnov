#!/usr/bin/env python3
"""squash.py SEG:OFF — листинг x86map со свёрнутыми повторами: подряд
идущие одинаковые группы инструкций (период 1..16) → «[группа] × N»."""
import re, subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    out = subprocess.run([sys.executable, os.path.join(HERE, "x86map.py"), "list", sys.argv[1]],
                         capture_output=True, text=True).stdout.splitlines()
    rows = []
    for l in out:
        m = re.match(r"\s+([0-9A-F]{4})\s+[0-9a-f]+\s+(.*)$", l)
        if m:
            rows.append((m.group(1), re.sub(r"\s+", " ", m.group(2)).strip()))
        else:
            rows.append((None, l))
    i, res = 0, []
    while i < len(rows):
        best = None
        for p in range(1, 17):
            if rows[i][0] is None:
                break
            k = 1
            while i + (k + 1) * p <= len(rows) and \
                    all(rows[i + k * p + j][0] is not None and
                        rows[i + k * p + j][1] == rows[i + j][1] for j in range(p)):
                k += 1
            if k >= 3 and (best is None or k * p > best[0] * best[1]):
                best = (k, p)
        if best:
            k, p = best
            res.append(f"  {rows[i][0]}  [ " + " ; ".join(r[1] for r in rows[i:i + p]) + f" ] × {k}")
            i += k * p
        else:
            a, t = rows[i]
            res.append(f"  {a}  {t}" if a else t)
            i += 1
    print("\n".join(res))


main()
