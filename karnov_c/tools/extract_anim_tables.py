#!/usr/bin/env python3
"""
extract_anim_tables.py — извлекает из KARNOV.EXE таблицы состояний/анимаций
объектов и генерирует C-данные для порта (src/anim_data.c + include/anim_data.h).

Механика (дизассемблирована 2026-08-14, ai_dispatch cs:0xD4B:0xBD5 / file 0x0F885):

  obj+0xD5/+0xD7  state_list  — far-указатель на массив StateScript[]
  obj+0xCD/+0xCF  state_anim  — far-указатель на AnimScript текущего состояния
  obj+0xD1/+0xD3  state_offs  — far-указатель на массив FrameOffset
  obj+0xC5        frame_index (он же sub_timer)
  obj+0xC9        tick_period (обработчик вызывается раз в столько кадров)
  obj+0xD9        cur_state_id (0x3E7 = не загружено)

  struct StateScript { u16 anim_off, anim_seg;  u16 offs_off, offs_seg; };  // 8 байт
  struct AnimScript  { u16 count;  u16 sprite_id[count]; };
  struct FrameOffset { s16 dx, dy; } [count];        // без заголовка

  Каждый «думательный» тик при state==2: ++frame_index, по модулю count.
  Обработчик, выставив бит 2 в ai_result, переключает состояние (анимация
  рестартится только если состояние ДЕЙСТВИТЕЛЬНО другое), а бит 4 применяет
  текущий кадр: bbox[0].sprite_id/dx/dy.

Скрипт находит все константные записи `mov word [es:bx+0xD5], imm16`
(= установка state_list в INIT'ах), привязывает их к типу по таблице
обработчиков DS:0x10FE и разбирает таблицы до первой «не-StateScript» записи.
"""
import os, re, struct, sys

DS_FILE = 0x1F190      # file offset DS:0000
DS_SEG  = 0x1D99       # значение DS
HANDLERS = 0x10FE      # DS:0x10FE — таблица обработчиков (58 × 4 байта)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "../../KARNOV.EXE")
    out_c = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "../src/anim_data.c")
    out_h = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, "../include/anim_data.h")
    d = open(exe, "rb").read()

    u16 = lambda a: struct.unpack_from("<H", d, DS_FILE + a)[0]
    s16 = lambda a: struct.unpack_from("<h", d, DS_FILE + a)[0]

    # тип → диапазон адресов его обработчика (для привязки места записи)
    handlers = []
    for t in range(58):
        off, seg = struct.unpack_from("<HH", d, DS_FILE + HANDLERS + t * 4)
        if seg:
            handlers.append((t, 0x1800 + seg * 16 + off, 0x1800 + seg * 16))

    def owner(addr):
        best = None
        for t, fa, base in handlers:
            if base <= addr < base + 0x10000 and fa <= addr:
                if best is None or fa > best[1]:
                    best = (t, fa)
        return best[0] if best else None

    # все `26 C7 87 D5 00 <imm16>` = mov word [es:bx+0xD5], imm
    sites = [(struct.unpack("<H", m.group(1))[0], m.start())
             for m in re.finditer(rb"\x26\xc7\x87\xd5\x00(..)", d, re.S)]
    bases = sorted({b for b, _ in sites})

    def parse_list(base):
        """Разбор StateScript[] — до записи с сегментом != DS."""
        nxt = min([b for b in bases if b > base], default=base + 0x200)
        states = []
        i = 0
        while base + i * 8 < nxt:
            a = base + i * 8
            anim, aseg, offs, oseg = u16(a), u16(a + 2), u16(a + 4), u16(a + 6)
            if aseg != DS_SEG or oseg != DS_SEG:
                break
            n = u16(anim)
            if n == 0 or n > 32:
                break
            spr = [u16(anim + 2 + k * 2) for k in range(n)]
            dlt = [(s16(offs + k * 4), s16(offs + k * 4 + 2)) for k in range(n)]
            states.append((spr, dlt))
            i += 1
        return states

    by_type = {}
    for b, site in sites:
        t = owner(site)
        if t is None or t in by_type:
            continue
        st = parse_list(b)
        if st:
            by_type[t] = (b, st)

    c = ['/* Сгенерировано tools/extract_anim_tables.py из KARNOV.EXE — не править вручную. */',
         '#include "anim_data.h"', '']
    for t in sorted(by_type):
        base, states = by_type[t]
        for i, (spr, dlt) in enumerate(states):
            c.append('static const u16 SPR_%d_%d[] = { %s };' % (t, i, ", ".join(map(str, spr))))
            c.append('static const s16 OFF_%d_%d[] = { %s };' %
                     (t, i, ", ".join("%d, %d" % p for p in dlt)))
        c.append('static const anim_state_t ST_%d[] = {' % t)
        for i, (spr, _) in enumerate(states):
            c.append('    { %d, SPR_%d_%d, OFF_%d_%d },' % (len(spr), t, i, t, i))
        c.append('};')
        c.append('/* type %d: state_list = DS:0x%04X, %d состояний */' % (t, base, len(states)))
        c.append('')
    c.append('const anim_list_t ANIM_LISTS[58] = {')
    for t in sorted(by_type):
        c.append('    [%d] = { %d, ST_%d },' % (t, len(by_type[t][1]), t))
    c.append('};')
    c.append('')
    open(out_c, "w").write("\n".join(c))

    h = ['/* Сгенерировано tools/extract_anim_tables.py — таблицы анимаций из KARNOV.EXE.',
         ' *',
         ' * Состояние = набор кадров; кадр = sprite_id + смещение bbox (dx, dy).',
         ' * Индекс кадра инкрементируется раз в tick_period игровых тиков и',
         ' * заворачивается по длине состояния (ai_dispatch, file 0x0F885).',
         ' * dx — в единицах движка (в порту умножается на 2), dy — строки. */',
         '#ifndef KARNOV_ANIM_DATA_H',
         '#define KARNOV_ANIM_DATA_H',
         '',
         '#include "karnov.h"',
         '',
         'typedef struct {',
         '    u8         n;      /* число кадров */',
         '    const u16 *spr;    /* sprite_id[n] */',
         '    const s16 *off;    /* пары (dx, dy) */',
         '} anim_state_t;',
         '',
         'typedef struct {',
         '    u8                  count;   /* число состояний (0 = нет данных) */',
         '    const anim_state_t *states;',
         '} anim_list_t;',
         '',
         'extern const anim_list_t ANIM_LISTS[58];',
         '',
         '#endif',
         '']
    open(out_h, "w").write("\n".join(h))

    print("типов с таблицами: %d" % len(by_type))
    for t in sorted(by_type):
        base, st = by_type[t]
        print("  type %2d  DS:0x%04X  состояний %d  кадров %s"
              % (t, base, len(st), [len(s[0]) for s in st]))


if __name__ == "__main__":
    main()
