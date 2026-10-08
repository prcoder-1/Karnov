#!/usr/bin/env python3
"""heapdiff.py ORIG_HEAP.BIN PORT.heap [кадр] — сверка far-кучи
(линейные 0x2DE30.., 0x50000 байт на кадр) по известным блокам."""
import sys
N = 0x50000
BASE = 0x2DE30
BLOCKS = [(0x2DE48, 0x18, "env"), (0x2DE4C, 0x20010, "vbuf_alloc"), (0x4DE64, 0x7992, "map_buf"),
          (0x557FE, 0x3E80, "tile_gfx"), (0x59686, 0x3C, "regions"), (0x596CA, 0xFA0, "spawns"),
          (0x5A672, 0x1B800, "sprite_arena")]


def blk(a):
    for s, n, name in BLOCKS:
        if s - 4 <= a < s + n:
            return f"{name}+{a - s:#x}"
    return "?"


def main():
    fo, fp = open(sys.argv[1], "rb"), open(sys.argv[2], "rb")
    f0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    f = 0
    while True:
        x, y = fo.read(N), fp.read(N)
        if len(x) < N or len(y) < N:
            break
        if f >= f0:
            bad = [i for i in range(N) if x[i] != y[i]]
            if bad:
                print(f"кадр {f}: расхождений {len(bad)}")
                runs, s = [], bad[0]
                for a, b in zip(bad, bad[1:] + [None]):
                    if b != a + 1:
                        runs.append((s, a))
                        s = b
                for s, e in runs[:20]:
                    print(f"  {BASE + s:05X}..{BASE + e:05X} ({e - s + 1}) {blk(BASE + s)}")
                return 1
        f += 1
    print("совпадает", f, "кадров")


main()
