#!/usr/bin/env python3
"""
extract_bbox_offsets.py — извлекает из KARNOV.EXE привязку спрайтов
(SpriteBox offsets) для каждого AI-типа.

Найдено дизассемблированием (2026-08-14):
  ActiveObject.bboxes[i] — 18 байт (0x12), первый по +0x20:
      +0x00 sprite_id
      +0x02 x_offset      (к x_world)
      +0x04 y_offset      (к y_world)
      +0x0E flag = 1
  Спрайт рисуется ВЕРХНИМ-ЛЕВЫМ углом в (x_world + x_offset,
  y_world + y_offset) — доказательство: INIT type 36 (Ryu, file 0x17017)
  ставит y_offset хвоста = ВЫСОТЕ спрайта головы (таблица DS:0x0FAC),
  т.е. второй спрайт кладётся ровно под первый.

  Метаданные спрайтов (индекс = sprite_id):
      DS:0x0D08[id] — ширина  (видимая, px)
      DS:0x0FAC[id] — высота  (px)

Скрипт находит обработчик каждого типа в таблице DS:0x10FE, берёт из его
jump-table ветку INIT (state 0/1) и собирает оттуда непосредственные
присваивания полям bbox.
"""
import struct, sys, os

EXE_HDR = 0x1800          # MZ header size
DS_FILE = 0x1F190         # DS-сегмент в файле
HANDLER_TABLE = 0x10FE    # DS:0x10FE — 58 записей (off, seg)
W_TABLE = 0x0D08          # DS: ширины спрайтов
H_TABLE = 0x0FAC          # DS: высоты спрайтов
NUM_TYPES = 58


def load(path):
    return open(path, "rb").read()


def far_to_file(seg, off):
    return EXE_HDR + seg * 16 + off


def read_handlers(d):
    out = []
    for t in range(NUM_TYPES):
        p = DS_FILE + HANDLER_TABLE + t * 4
        off, seg = struct.unpack("<HH", d[p:p + 4])
        out.append((seg, off))
    return out


def find_init_branch(d, seg, off):
    """Разбирает пролог обработчика: cmp ax,N / jmp [cs:bx+TBL] → адрес INIT."""
    base = far_to_file(seg, off)
    win = d[base:base + 0x40]
    i = win.find(b"\x2E\xFF\xA7")            # jmp [cs:bx+disp16]
    if i < 0:
        return None
    tbl_off = struct.unpack("<H", win[i + 3:i + 5])[0]
    tbl = far_to_file(seg, tbl_off)
    init_off = struct.unpack("<H", d[tbl:tbl + 2])[0]   # state 0 → INIT
    return far_to_file(seg, init_off)


def scan_bbox_stores(d, start, limit=0x300):
    """Собирает `mov word [es:bx+disp8], imm16` в ветке INIT (до retf)."""
    stores = {}
    i = start
    end = start + limit
    while i < end and i + 2 < len(d):
        b = d[i]
        if b == 0xCB:                        # retf — конец ветки
            break
        if d[i:i + 3] == b"\x26\xC7\x47":    # mov word [es:bx+disp8], imm16
            disp = d[i + 3]
            imm = struct.unpack("<H", d[i + 4:i + 6])[0]
            stores.setdefault(disp, imm)
            i += 6
            continue
        if d[i:i + 4] == b"\x26\xC7\x87":    # mov word [es:bx+disp16], imm16
            disp = struct.unpack("<H", d[i + 3:i + 5])[0]
            imm = struct.unpack("<H", d[i + 5:i + 7])[0]
            stores.setdefault(disp, imm)
            i += 7
            continue
        i += 1
    return stores


def s16(v):
    return v - 0x10000 if v >= 0x8000 else v


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "../../KARNOV.EXE")
    d = load(exe)

    widths = d[DS_FILE + W_TABLE: DS_FILE + W_TABLE + 512]
    heights = d[DS_FILE + H_TABLE: DS_FILE + H_TABLE + 512]

    print("/* Сгенерировано tools/extract_bbox_offsets.py из KARNOV.EXE. */")
    print("/* type: num_bboxes, [sprite, dx, dy] — из INIT-ветки обработчика. */")
    rows = []
    for t, (seg, off) in enumerate(read_handlers(d)):
        if t == 0 or seg == 0:
            rows.append((t, None))
            continue
        init = find_init_branch(d, seg, off)
        if init is None:
            rows.append((t, None))
            continue
        st = scan_bbox_stores(d, init)
        n = st.get(0x1E)                       # num_bboxes
        boxes = []
        for k in range(8):
            b = 0x20 + k * 0x12
            if b in st or (b + 2) in st or (b + 4) in st:
                boxes.append((st.get(b), s16(st.get(b + 2, 0)), s16(st.get(b + 4, 0))))
        rows.append((t, (n, boxes)))

    for t, info in rows:
        if info is None:
            print(f"  [{t:>2}] —")
            continue
        n, boxes = info
        desc = ", ".join(
            f"spr={b[0]}({widths[b[0]] if b[0] and b[0] < 512 else '?'}x"
            f"{heights[b[0]] if b[0] and b[0] < 512 else '?'}) dx={b[1]} dy={b[2]}"
            for b in boxes) or "—"
        print(f"  [{t:>2}] num_bboxes={n}  {desc}")


if __name__ == "__main__":
    main()
