#!/usr/bin/env python3
"""genmov.py OUT SEED — случайное демо LEVELn.MOV: 255 событий (маска ввода,
кадр); маски — сочетания R/L/D/U/огонь/OPTION с разной длительностью."""
import random, struct, sys

out, seed = sys.argv[1], int(sys.argv[2])
rnd = random.Random(seed)
ev, frame = [], 1
for i in range(255):
    # с перекосом вправо, иначе игрок не уходит от начала уровня
    m = rnd.choice([1, 1, 1, 1 | 0x10, 1 | 0x10, 1 | 8, 1 | 8 | 0x10, 1 | 8, 8, 0x10, 2, 2 | 0x10,
                    4, 4 | 0x10, 0x20, 0, 2 | 8, 1 | 4])
    ev.append((m, frame))
    frame += rnd.choice([1, 2, 3, 5, 8, 12, 20, 30])
with open(out, "wb") as f:
    f.write(struct.pack("<hh", 0, len(ev)))
    for m, fr in ev:
        f.write(struct.pack("<BH", m, fr))
