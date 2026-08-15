#!/usr/bin/env python3
"""
decompile_ai.py — разбор AI-обработчиков KARNOV.EXE.

Обработчики устроены единообразно (см. KARNOV.md §3.1-3.2):

    handler(void):
        ax = [0xD8A2]                 ; state_byte объекта (0..6)
        switch ax → jmp [cs:bx+TBL]   ; таблица состояний
        state 1 = INIT, state 2 = THINK, 4 = HURT, 6 = DYING
    THINK:
        ++[+0xEB]                     ; счётчик тиков фазы
        si = [+0xE9]; [+0xE9] |= 1    ; фаза; чётная — одноразовый вход
        if !(si & 1): [+0xEB] = 0
        switch si → jmp [cs:bx+TBL2]  ; таблица фаз

Скрипт линейно дизассемблирует обработчик (ndisasm), находит все
`jmp [cs:bx+X]`, вычисляет длину таблицы по предшествующему `cmp ax,K`,
исключает байты таблиц из кода и печатает разобранные цели, а также
подписывает известные хелперы и поля ActiveObject.

Использование:
    python3 tools/decompile_ai.py 24            # тип 24
    python3 tools/decompile_ai.py 0x1348:0x2D7  # произвольный far-адрес
    python3 tools/decompile_ai.py all           # сводка по всем типам
"""
import os, re, struct, subprocess, sys

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../KARNOV.EXE")
DS_FILE = 0x1F190
HANDLERS = 0x10FE

FIELDS = {
    0x00: "x_screen", 0x02: "y_screen", 0x08: "x_world", 0x0C: "y_world",
    0x0E: "vx_lo", 0x10: "vx_hi", 0x12: "vy_lo", 0x14: "vy_hi",
    0x16: "ax_lo", 0x18: "ax_hi", 0x1A: "ay_lo", 0x1C: "ay_hi",
    0x1E: "num_bboxes", 0x20: "box0.sprite", 0x22: "box0.dx", 0x24: "box0.dy",
    0x26: "box0.x0", 0x28: "box0.y0", 0x2A: "box0.x1", 0x2C: "box0.y1",
    0x2E: "box0.hitflag",
    0xB0: "alive", 0xB6: "state_byte", 0xB7: "handler_lo", 0xB9: "handler_hi",
    0xBB: "ai_init_lo", 0xBD: "ai_init_hi", 0xBF: "ai_next_lo", 0xC1: "ai_next_hi",
    0xC3: "anim_len", 0xC5: "frame_idx", 0xC7: "frame_ctr", 0xC9: "tick_period",
    0xCB: "throttle", 0xCD: "state_anim_lo", 0xCF: "state_anim_hi",
    0xD1: "state_offs_lo", 0xD3: "state_offs_hi",
    0xD5: "state_list_lo", 0xD7: "state_list_hi", 0xD9: "next_state_id",
    0xDB: "active", 0xE5: "obj_type", 0xE7: "hit_points",
    0xE9: "misc(+0xE9)", 0xEB: "phase_ctr(+0xEB)", 0xED: "phase(+0xED)",
    0xEF: "aux(+0xEF)", 0xF1: "parent_lo", 0xF3: "parent_hi",
}

GLOBALS = {
    0xD8A2: "cur_state", 0xD8A6: "self_lo", 0xD8A8: "self_hi", 0xD88C: "next_anim_state",
    0xD963: "ai_result", 0xD95F: "player_ptr", 0xD8BC: "player_x", 0xD8BE: "player_y",
    0xD8B0: "player_dir", 0xD949: "firepower", 0xD947: "weapon", 0x1424: "shots_alive",
    0xD97C: "gold_balls", 0xB828: "tile_shift", 0xB834: "view_left", 0xB838: "view_right",
    0xCC8E: "view_x", 0xCC92: "view_y", 0xCCA6: "god_mode", 0x2822: "alloc_idx",
    0xD8CE: "player_state", 0xD8B2: "fire_cooldown",
}

CALLS = {
    (0xD4B, 0x008D): "set_motion(idx)            ; vx/vy/ax/ay из DS:0x1348+idx*16",
    (0xD4B, 0x005A): "tile_at(x, y) → категория",
    (0xD4B, 0x0021): "tile_at_cell(col, row) → категория",
    (0xD4B, 0x014C): "place_on_ground_core(mask)",
    (0xD4B, 0x019F): "place_on_ground(sprite)     ; левая колонка, mask 0x0001",
    (0xD4B, 0x01DE): "place_on_ground_center(spr) ; центр, mask 0x0089",
    (0xD4B, 0x04A5): "die_puff()                  ; → handler 0x3C0, ai_result|=8",
    (0xD4B, 0x04FF): "die_puff2()",
    (0xD4B, 0x0B31): "init_active_object(obj, handler)",
    (0xD4B, 0x0BD5): "ai_dispatch(obj)",
    (0xD4B, 0x1124): "play_sound(idx)",
    (0xD4B, 0x03C0): "puff_handler",
    (0xD4B, 0x0559): "remove_object",
    (0x41D, 0x04D7): "add_score(long)",
    (0x11D6, 0x00DD): "aim(x0,y0,x1,y1,&vx,&vy,n) ; долететь за n тиков",
    (0x11D6, 0x0139): "spawn_projectile(...)",
    (0x11D6, 0x015D6): "boss_helper",
    (0x6E, 0x011B): "alloc_object(parity) → idx",
    (0x0, 0x039D): "ldiv",
    (0x0, 0x041C): "lmul",
    (0x6E, 0x059A): "set_player_pos",
}


def load():
    return open(EXE, "rb").read()


def far_to_file(seg, off):
    return 0x1800 + seg * 16 + off


def handler_of(d, t):
    off, seg = struct.unpack_from("<HH", d, DS_FILE + HANDLERS + t * 4)
    return seg, off


def ndis(data, org):
    p = subprocess.run(["ndisasm", "-b", "16", "-o", hex(org), "-"],
                       input=data, capture_output=True)
    return p.stdout.decode("latin1").splitlines()


def annotate(line):
    m = re.search(r"call (0x[0-9a-f]+):(0x[0-9a-f]+)", line)
    if m:
        key = (int(m.group(1), 16), int(m.group(2), 16))
        if key in CALLS:
            line += "        ; " + CALLS[key]
    m = re.search(r"\[es:bx\+0x([0-9a-f]+)\]", line)
    if m:
        f = FIELDS.get(int(m.group(1), 16))
        if f:
            line += "        ; self." + f
    for m in re.finditer(r"\[0x([0-9a-f]+)\]", line):
        g = GLOBALS.get(int(m.group(1), 16))
        if g:
            line += "        ; " + g
            break
    return line


def decode(d, seg, off, size=0x700):
    """Линейный разбор с вырезанием jump-таблиц."""
    base = far_to_file(seg, off)
    data = d[base:base + size]
    tables = {}          # адрес таблицы → число записей
    for _ in range(8):   # итерируем, пока находятся новые таблицы
        holes = sorted((a, a + 2 * n) for a, n in tables.items())
        lines, cur = [], off
        pieces = []
        pos = off
        for h0, h1 in holes:
            if h0 > pos:
                pieces.append((pos, h0))
            pos = max(pos, h1)
        pieces.append((pos, off + size))
        for a, b in pieces:
            if b > a:
                lines += ndis(data[a - off:b - off], a)
        found = False
        prev_bound = None
        for ln in lines:
            m = re.search(r"cmp ax,(?:byte )?\+?(0x[0-9a-f]+|\d+)", ln)
            if m:
                prev_bound = int(m.group(1), 16 if m.group(1).startswith("0x") else 10)
            m = re.search(r"jmp \[cs:bx\+(0x[0-9a-f]+)\]", ln)
            if m:
                tbl = int(m.group(1), 16)
                if tbl not in tables:
                    tables[tbl] = (prev_bound + 1) if prev_bound is not None else 8
                    found = True
        if not found:
            break
    return lines, tables, base


def dump(d, seg, off, title, size=0x700):
    lines, tables, base = decode(d, seg, off, size)
    print("=" * 72)
    print("%s  cs:0x%X:0x%04X  file 0x%X" % (title, seg, off, base))
    print("=" * 72)
    for a, n in sorted(tables.items()):
        ents = [struct.unpack_from("<H", d, base - off + a + i * 2)[0] for i in range(n)]
        print("  таблица @0x%04X (%d): %s" % (a, n, ", ".join("0x%04X" % e for e in ents)))
    print()
    for ln in lines:
        print(annotate(ln))


def main():
    d = load()
    if len(sys.argv) < 2:
        print(__doc__)
        return
    arg = sys.argv[1]
    size = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x700
    if arg == "all":
        for t in range(58):
            seg, off = handler_of(d, t)
            if not seg:
                continue
            print("type %2d  cs:0x%04X:0x%04X  file 0x%05X" % (t, seg, off, far_to_file(seg, off)))
        return
    if ":" in arg:
        seg, off = (int(x, 0) for x in arg.split(":"))
        dump(d, seg, off, "handler", size)
    else:
        t = int(arg, 0)
        seg, off = handler_of(d, t)
        dump(d, seg, off, "AI type %d" % t, size)


if __name__ == "__main__":
    main()
