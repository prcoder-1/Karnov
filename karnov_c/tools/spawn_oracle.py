#!/usr/bin/env python3
"""
spawn_oracle.py — независимый оракул алгоритма CHECKTRIGGERS.

Реализует декомпилированный алгоритм KARNOV.EXE (file 0x5011) с нуля,
НЕ разделяя код с C-портом, и сверяет его решение со спавн-логом порта.

Использование:
  KARNOV_SPAWN_LOG=/tmp/log.txt ./karnov --level N --no-audio --ppm x.ppm --frame 500
  python3 tools/spawn_oracle.py N /tmp/log.txt

Формат лога:
  P frame player_x_px player_y_px camera_left_px   — на каждый вызов
  S frame rec_idx type subtype x y hp goal         — на каждый успешный спаун

Алгоритм (байт-точно):
  - записи 20 байт {type,subtype,cur_x,cur_y,range_x,range_y,spawn_x,
    spawn_y,trigger,flags}, selection sort по cur_x (строгий «<», swap
    при min≠i), flags из файла сохраняются;
  - каждый кадр: два бинпоиска (порог1 = camera_left, порог2 = px+0xA0),
    линейный скан [lo1..hi2] включительно;
  - спаун если flags==0 и |px−cur_x|<range_x и |py−cur_y|<range_y
    (строго); flags=1 при успехе. X — в 2px-единицах (px = pixels/2).

ОГРАНИЧЕНИЕ: оракул НЕ моделирует rearm-цикл (file 0x1015A), которым
движок перевзводит записи после гибели игрока. Поэтому сверять им можно
только прогоны, где Карнов не умирал: иначе порт законно спаунит записи
повторно, а оракул считает их лишними. Признак смерти в логе — скачок
player_x назад между соседними P-строками.
"""
import struct, sys, os


def rle_decompress(data, num_bytes=1 << 20):
    out = bytearray()
    i = 0
    while len(out) < num_bytes and i < len(data):
        b = data[i]; i += 1
        if b != 0x5A:
            out.append(b); continue
        if i >= len(data): break
        nb = data[i]; i += 1
        if nb == 0x5A:
            out.append(0x5A)
        else:
            vb = data[i]; i += 1
            out.append(vb)
            out.extend([vb] * ((nb - 1) & 0xFF))
    return bytes(out)


def load_records(level_bin):
    d = rle_decompress(open(level_bin, "rb").read())
    nreg = struct.unpack("<H", d[0:2])[0]
    off = 2 + nreg * 12
    nen = struct.unpack("<H", d[off:off + 2])[0]
    off += 2
    recs = [list(struct.unpack("<10H", d[off + i * 20:off + i * 20 + 20]))
            for i in range(nen)]
    # selection sort по cur_x как в оригинале (file 0x5268)
    for i in range(len(recs) - 1):
        mn = i
        for j in range(i + 1, len(recs)):
            if recs[j][2] < recs[mn][2]:
                mn = j
        if mn != i:
            recs[i], recs[mn] = recs[mn], recs[i]
    return recs


def check_frame(recs, flags, px_units, py, left_units):
    """Один кадр CHECKTRIGGERS → список индексов заспавненных записей."""
    n = len(recs)
    if n == 0:
        return []
    lo, hi = 0, n - 1
    while hi - lo > 1:
        mid = (lo + hi) >> 1
        if recs[mid][2] < left_units: lo = mid
        else: hi = mid
    start = lo
    lo, hi = 0, n - 1
    ahead = px_units + 0xA0
    while hi - lo > 1:
        mid = (lo + hi) >> 1
        if recs[mid][2] < ahead: lo = mid
        else: hi = mid
    end = hi
    spawned = []
    for si in range(start, end + 1):
        r = recs[si]
        if flags[si]:
            continue
        if abs(px_units - r[2]) >= r[4]:
            continue
        if abs(py - r[3]) >= r[5]:
            continue
        flags[si] = 1
        spawned.append(si)
    return spawned


def main():
    level = int(sys.argv[1])
    log_path = sys.argv[2]
    here = os.path.dirname(os.path.abspath(__file__))
    recs = load_records(os.path.join(here, f"../data/LEVEL{level}.BIN"))
    flags = [r[9] for r in recs]

    port_spawns = []       # (frame, rec_idx, x, y)
    oracle_spawns = []
    for line in open(log_path):
        f = line.split()
        if f[0] == "P":
            frame, px, py, left = map(int, f[1:5])
            for si in check_frame(recs, flags, px // 2, py, left // 2):
                r = recs[si]
                # Позиция из записи: порт логирует её ДО постановки на
                # землю в INIT (снап проверяется скриншотами, не оракулом).
                oracle_spawns.append((frame, si, r[6] * 2, r[7]))
        elif f[0] == "S":
            frame, si, typ, sub, x, y, hp, goal = map(int, f[1:9])
            port_spawns.append((frame, si, x, y))

    ok = port_spawns == oracle_spawns
    print(f"LEVEL{level}: port={len(port_spawns)} spawns, "
          f"oracle={len(oracle_spawns)} → {'MATCH' if ok else 'MISMATCH'}")
    if not ok:
        for i in range(max(len(port_spawns), len(oracle_spawns))):
            p = port_spawns[i] if i < len(port_spawns) else None
            o = oracle_spawns[i] if i < len(oracle_spawns) else None
            if p != o:
                print(f"  [{i}] port={p} oracle={o}")
        sys.exit(1)


if __name__ == "__main__":
    main()
