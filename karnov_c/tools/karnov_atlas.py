#!/usr/bin/env python3
"""
karnov_atlas.py — извлекает кадры Karnov напрямую из SPRITES.EGA и
SPRITES.CGA и рендерит атлас PNG с подписями (sprite_id, размер,
ai_type), чтобы можно было визуально убедиться, что это именно Karnov.

Karnov в оригинальной игре имеет ai_type=513 (по memory):
    #98  STAND       — стандартная стойка
    #99  WALK1       — кадр ходьбы 1
    #100 WALK2       — кадр ходьбы 2
    #102 WALK3       — кадр ходьбы 3
    #104 CROUCH      — присед
    #105 FIRE        — бросок огня
    #112 DEAD        — лежит мёртвый
    #120 JUMP        — прыжок (руки вверх)
    #98..#123 — полный набор анимаций (22 кадра)

CGA = 2 bpp (4 цвета), EGA = 4 bpp (16 цветов).
"""
import os, sys, struct
from pathlib import Path

# Палитры
CGA_PAL_1H = [
    (0x00, 0x00, 0x00),   # 0 black
    (0x55, 0xFF, 0xFF),   # 1 cyan
    (0xFF, 0x55, 0xFF),   # 2 magenta
    (0xFF, 0xFF, 0xFF),   # 3 white
]
EGA_PAL16 = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF),
]

# ────────────────────────────────────────────────────────────────────────
# RLE декомпрессор (0x5A escape, как в KARNOV.EXE cs:0x58B)
# ────────────────────────────────────────────────────────────────────────
def rle_decompress_from(data, start, num_bytes):
    out = bytearray()
    i = start
    n = len(data)
    in_run = 0
    run_left = 0
    run_byte = 0
    while len(out) < num_bytes:
        if in_run:
            out.append(run_byte)
            run_left -= 1
            if run_left == 0:
                in_run = 0
            continue
        if i >= n:
            break
        b = data[i]; i += 1
        if b != 0x5A:
            out.append(b)
            continue
        if i >= n:
            break
        nb = data[i]; i += 1
        if nb == 0x5A:
            out.append(0x5A)
        else:
            if i >= n:
                break
            vb = data[i]; i += 1
            out.append(vb)
            run_left = (nb - 1) & 0xFF
            in_run = 1 if run_left > 0 else 0
            run_byte = vb
    return bytes(out)

# ────────────────────────────────────────────────────────────────────────
# CGA-декодер: spr_bytes = wid, cnt, count*width пар (mask_word, pix_word)
# ────────────────────────────────────────────────────────────────────────
def decode_cga(spr):
    if len(spr) < 4: return None
    wid = spr[0]; cnt = spr[1]
    if not (1 <= cnt <= 6 and 1 <= wid <= 60): return None
    if cnt * wid * 4 + 2 != len(spr): return None
    iw, ih = cnt * 8, wid
    pix = bytearray(iw * ih)
    for r in range(wid):
        for s in range(cnt):
            o = 2 + (r * cnt + s) * 4
            mlo, mhi, plo, phi = spr[o], spr[o+1], spr[o+2], spr[o+3]
            for px in range(8):
                col = s * 8 + px
                bi = px // 4
                ib = px % 4
                sh = 6 - ib * 2
                mb = mlo if bi == 0 else mhi
                pb = plo if bi == 0 else phi
                m = (mb >> sh) & 3
                if m == 3:
                    pix[r*iw + col] = 0xFF
                else:
                    pix[r*iw + col] = (pb >> sh) & 3
    return (iw, ih, bytes(pix))

# ────────────────────────────────────────────────────────────────────────
# EGA-декодер: 4bpp block-major. После RLE-распаковки:
#   +0..+5: 3 indir WORDs (indir1=0; indir2=indir3=size = cnt*wid*10+2)
#   +6: WORD strip_hdr (hi=cnt strips, lo=wid rows)
#   +8: 5 блоков по cnt*wid WORD: planes 0,1,2,3 + mask (last)
# ────────────────────────────────────────────────────────────────────────
def decode_ega(spr, mask_inverted=True):
    """On-disk EGA: 2-байтный header (wid, cnt) + 5 блоков по cnt*wid WORD
       (planes 0,1,2,3 + mask LAST). Это формат пакетного файла; рантайм
       формат отличается тем что добавляет 6 байт indir words в начало.

       mask_inverted: для Karnov-кадров (ai_type=513) корректна
       интерпретация mask=1 → opaque (а не transparent). При
       mask_inverted=False получается ~80% непрозрачных пикселей
       (баг для Karnov, рабочая интерпретация для тайтульных тайлов).
    """
    if len(spr) < 12: return None
    wid = spr[0]
    cnt = spr[1]
    if not (1 <= cnt <= 6 and 1 <= wid <= 60): return None
    cw = cnt * wid
    if 2 + cw * 10 != len(spr): return None
    iw, ih = cnt * 16, wid
    pix = bytearray(iw * ih)
    def W(b, ws):
        p = 2 + (b * cw + ws) * 2
        return spr[p] | (spr[p+1] << 8)
    for r in range(wid):
        for s in range(cnt):
            ws = r * cnt + s
            p0 = W(0, ws); p1 = W(1, ws); p2 = W(2, ws)
            p3 = W(3, ws); mk = W(4, ws)
            for bit in range(16):
                wb = 15 - bit
                col = s * 16 + bit
                mbit = (mk >> wb) & 1
                transparent = (mbit == 0) if mask_inverted else (mbit == 1)
                if transparent:
                    pix[r*iw + col] = 0xFF
                else:
                    v  = ((p0 >> wb) & 1) << 0
                    v |= ((p1 >> wb) & 1) << 1
                    v |= ((p2 >> wb) & 1) << 2
                    v |= ((p3 >> wb) & 1) << 3
                    pix[r*iw + col] = v
    return (iw, ih, bytes(pix))

# ────────────────────────────────────────────────────────────────────────
# Простейший PNG writer (без зависимостей)
# ────────────────────────────────────────────────────────────────────────
import zlib

def write_png(path, w, h, rgb):
    """rgb: bytes длиной w*h*3."""
    def chunk(tag, data):
        crc = zlib.crc32(tag + data)
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(rgb[y*w*3:(y+1)*w*3])
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))

# ────────────────────────────────────────────────────────────────────────
# Минимальный 5×7 bitmap-шрифт (только нужные символы)
# ────────────────────────────────────────────────────────────────────────
FONT = {
    '0':[0x0E,0x11,0x13,0x15,0x19,0x11,0x0E],'1':[0x04,0x0C,0x04,0x04,0x04,0x04,0x0E],
    '2':[0x0E,0x11,0x01,0x06,0x08,0x10,0x1F],'3':[0x0E,0x11,0x01,0x06,0x01,0x11,0x0E],
    '4':[0x02,0x06,0x0A,0x12,0x1F,0x02,0x02],'5':[0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E],
    '6':[0x06,0x08,0x10,0x1E,0x11,0x11,0x0E],'7':[0x1F,0x01,0x02,0x04,0x08,0x08,0x08],
    '8':[0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E],'9':[0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C],
    '#':[0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A],'x':[0x00,0x00,0x11,0x0A,0x04,0x0A,0x11],
    ' ':[0]*7,':':[0,0x04,0x04,0,0x04,0x04,0],
    'A':[0x0E,0x11,0x11,0x1F,0x11,0x11,0x11],'I':[0x0E,0x04,0x04,0x04,0x04,0x04,0x0E],
    'D':[0x1E,0x11,0x11,0x11,0x11,0x11,0x1E],'E':[0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F],
    'K':[0x11,0x12,0x14,0x18,0x14,0x12,0x11],'R':[0x1E,0x11,0x11,0x1E,0x14,0x12,0x11],
    'N':[0x11,0x19,0x15,0x13,0x11,0x11,0x11],'O':[0x0E,0x11,0x11,0x11,0x11,0x11,0x0E],
    'V':[0x11,0x11,0x11,0x11,0x11,0x0A,0x04],'-':[0,0,0,0x0E,0,0,0],
    'G':[0x0E,0x11,0x10,0x17,0x11,0x11,0x0E],'B':[0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E],
    'C':[0x0E,0x11,0x10,0x10,0x10,0x11,0x0E],
    'F':[0x1F,0x10,0x10,0x1E,0x10,0x10,0x10],'L':[0x10,0x10,0x10,0x10,0x10,0x10,0x1F],
    'S':[0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E],'T':[0x1F,0x04,0x04,0x04,0x04,0x04,0x04],
    'U':[0x11,0x11,0x11,0x11,0x11,0x11,0x0E],'P':[0x1E,0x11,0x11,0x1E,0x10,0x10,0x10],
    'W':[0x11,0x11,0x11,0x15,0x15,0x15,0x0A],'M':[0x11,0x1B,0x15,0x11,0x11,0x11,0x11],
    'H':[0x11,0x11,0x11,0x1F,0x11,0x11,0x11],'J':[0x07,0x02,0x02,0x02,0x02,0x12,0x0C],
    'Y':[0x11,0x11,0x0A,0x04,0x04,0x04,0x04],
}

def draw_text(buf, w, h, x, y, text, color=(255,255,255)):
    for ch in text:
        g = FONT.get(ch.upper(), FONT[' '])
        for r in range(7):
            row = g[r]
            for b in range(5):
                if row & (1 << (4-b)):
                    xx, yy = x+b, y+r
                    if 0 <= xx < w and 0 <= yy < h:
                        o = (yy*w + xx)*3
                        buf[o:o+3] = bytes(color)
        x += 6

# ────────────────────────────────────────────────────────────────────────
# Main: извлечь Karnov ID #94..#125 (включая 98-123) и сделать атлас
# ────────────────────────────────────────────────────────────────────────
def extract_directory(raw):
    """Раскрыть директорию (count WORD + N*8 байт), вернуть [(idx, ai, sz, off), ...]."""
    dir_bytes = rle_decompress_from(raw, 0, 2 + 338*8)
    count = dir_bytes[0] | (dir_bytes[1] << 8)
    entries = []
    for i in range(count):
        eoff = 2 + i*8
        ai, sz, doff, _r = struct.unpack("<HHHH", dir_bytes[eoff:eoff+8])
        entries.append((i, ai, sz, doff))
    return entries

def extract_sprite_set(raw, decoder, target_ids):
    """Извлечь спрайты с указанными ID. Возвращает [(id, w, h, pix, ai)]."""
    entries = extract_directory(raw)
    out = []
    for idx, ai, sz, doff in entries:
        if idx not in target_ids: continue
        if doff == 0 or doff >= len(raw): continue
        data = rle_decompress_from(raw, doff, sz)
        if len(data) < sz: continue
        dec = decoder(data)
        if dec is None: continue
        w, h, px = dec
        out.append((idx, w, h, px, ai))
    return out

def render_atlas(sprites, palette, title, out_path, cell_w=64, cell_h=48, cols=8, scale=2, color_key=-1):
    """Рендерит сетку cols × rows из спрайтов с подписями."""
    rows = (len(sprites) + cols - 1) // cols
    # Each cell: scale*cell_w wide × scale*cell_h + 12 px label
    cw, ch = cell_w*scale, cell_h*scale + 14
    W = cw * cols + 8
    H = ch * rows + 24
    buf = bytearray(b'\x18\x18\x28' * (W*H))  # тёмно-синий фон

    draw_text(buf, W, H, 4, 6, title, (255, 255, 0))

    for i, (sid, sw, sh, px, ai) in enumerate(sprites):
        c = i % cols
        r = i // cols
        x0 = c * cw + 4
        y0 = 22 + r * ch
        # рамка
        for x in range(cw - 4):
            for y in (0, scale*cell_h):
                o = ((y0+y)*W + (x0+x))*3
                buf[o:o+3] = b'\x44\x44\x66'
        # спрайт
        for sy in range(sh):
            for sx in range(sw):
                v = px[sy*sw + sx]
                if v == 0xFF: continue
                if v == color_key: continue
                rgb = palette[v]
                # центрируем спрайт в ячейке
                dx = (cell_w - sw) // 2 + sx
                dy = (cell_h - sh) // 2 + sy
                for ky in range(scale):
                    for kx in range(scale):
                        xx = x0 + dx*scale + kx
                        yy = y0 + dy*scale + ky
                        if 0 <= xx < W and 0 <= yy < H:
                            o = (yy*W + xx)*3
                            buf[o:o+3] = bytes(rgb)
        # подпись
        label = f"#{sid} {sw}x{sh}"
        draw_text(buf, W, H, x0 + 2, y0 + scale*cell_h + 4, label, (200, 200, 255))

    write_png(out_path, W, H, bytes(buf))
    print(f"  → {out_path} ({W}x{H}, {len(sprites)} sprites)")


def main():
    repo = Path(__file__).parent.parent.parent
    cga = repo / "SPRITES.CGA"
    ega = repo / "SPRITES.EGA"
    print(f"SPRITES.CGA = {cga} ({cga.stat().st_size} bytes)")
    print(f"SPRITES.EGA = {ega} ({ega.stat().st_size} bytes)")

    raw_cga = cga.read_bytes()
    raw_ega = ega.read_bytes()

    # Karnov sprite IDs (per memory project_sprites_runtime.md)
    karnov_ids = set(range(94, 126))  # широкая полоса вокруг 98..123
    karnov_keyframes_only = [98, 99, 100, 102, 104, 105, 112, 120]

    # Извлечь и проверить ai_type у каждого
    cga_sprites = extract_sprite_set(raw_cga, decode_cga, karnov_ids)
    ega_sprites = extract_sprite_set(raw_ega, decode_ega, karnov_ids)

    print(f"\nИзвлечено CGA: {len(cga_sprites)} спрайтов")
    print(f"Извлечено EGA: {len(ega_sprites)} спрайтов")

    # Сортируем по ID для удобства
    cga_sprites.sort(key=lambda s: s[0])
    ega_sprites.sort(key=lambda s: s[0])

    print("\nai_type-распределение CGA Karnov-окрестности:")
    from collections import Counter
    print("  ", Counter(s[4] for s in cga_sprites))
    print("ai_type-распределение EGA Karnov-окрестности:")
    print("  ", Counter(s[4] for s in ega_sprites))

    out_dir = Path(__file__).parent.parent / "out"
    out_dir.mkdir(exist_ok=True)

    render_atlas(cga_sprites, CGA_PAL_1H,
                 "KARNOV SPRITES FROM SPRITES.CGA (RLE PACKED)",
                 out_dir / "karnov_atlas_cga.png", cell_w=56, cell_h=40, cols=8)
    render_atlas(ega_sprites, EGA_PAL16,
                 "KARNOV SPRITES FROM SPRITES.EGA (RLE PACKED, MASK INVERTED)",
                 out_dir / "karnov_atlas_ega.png", cell_w=56, cell_h=40, cols=8)

    # Также — атлас только ключевых кадров для гэймплея
    cga_key = [s for s in cga_sprites if s[0] in karnov_keyframes_only]
    ega_key = [s for s in ega_sprites if s[0] in karnov_keyframes_only]
    render_atlas(cga_key, CGA_PAL_1H,
                 "KARNOV KEYFRAMES CGA: STAND WALK1 WALK2 WALK3 CROUCH FIRE DEAD JUMP",
                 out_dir / "karnov_keyframes_cga.png", cell_w=64, cell_h=48, cols=8, scale=3)
    render_atlas(ega_key, EGA_PAL16,
                 "KARNOV KEYFRAMES EGA: STAND WALK1 WALK2 WALK3 CROUCH FIRE DEAD JUMP",
                 out_dir / "karnov_keyframes_ega.png", cell_w=64, cell_h=48, cols=8, scale=3)


if __name__ == "__main__":
    main()
