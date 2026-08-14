#!/usr/bin/env python3
"""
sprites_extract_ega_disk.py — extract sprites from SPRITES.EGA directly
(without needing a DOSBox runtime memory dump).

Mirrors the approach in sprites_extract_cga.py:
  - entry.offset in the directory is a COMPRESSED file offset
    (used with bin_fseek in KARNOV.EXE)
  - bin_fread decompresses entry.size bytes with reset RLE state

On-disk per-sprite layout for EGA (ВЕРИФИЦИРОВАНО по статистике байт
SPRITES.EGA #76: блок 0 содержит ~47/124 байт 0xFF — это MASK):
  +0:    width  (rows)
  +1:    count  (16-pixel strips per row)
  +2+:   block-major data:
         - block 0 (cw words): MASK   (mask bit 1 = transparent)
         - block 1 (cw words): plane 0
         - block 2 (cw words): plane 1
         - block 3 (cw words): plane 2
         - block 4 (cw words): plane 3
  where cw = count * width
  Total = 2 + cw * 10 bytes.

ВАЖНО: on-disk порядок ОТЛИЧАЕТСЯ от runtime-in-memory layout. Runtime
имеет mask ПОСЛЕДНИЙ (см. sprites_extract.py), потому что loader делает
переупорядочивание при загрузке. На диске же mask стоит ПЕРВЫМ.

Directory entry (8 bytes): u16 ai_type, u16 size, u16 offset, u16 bank.
Четвёртое слово — индекс 64КБ-банка: file_offset = bank*65536 + offset
(offset 16-битный, а архив ~120КБ; bank=1 у id 211..337). Игнорирование
банка — причина, по которой раньше извлекались только id 0..210.

Output cache (v4, bpp=4):
  uint16 magic, version=4, count
  per sprite: uint16 sid, uint8 w, uint8 h, uint8 bpp=4, uint8 reserved,
              uint16 ai_type (слово 0 директории — манифест sprite→AI type),
              uint8 pixels[w*h]   (0..15 EGA color, 0xFF = transparent)
"""
import os, sys, struct

MAGIC, VERSION = 0x534B, 4
# 128×63 — по факту: id 180 = 128×30 (cnt=8), id 172 = 64×63 (wid=63).
MAX_W, MAX_H = 128, 63


def rle_decompress_from(data, start_off, num_bytes):
    """Decompress num_bytes from compressed offset (state reset on each call)."""
    out = bytearray()
    i = start_off
    n = len(data)
    in_run = 0; run_left = 0; run_byte = 0
    while len(out) < num_bytes:
        if in_run:
            out.append(run_byte); run_left -= 1
            if run_left == 0: in_run = 0
            continue
        if i >= n: break
        b = data[i]; i += 1
        if b != 0x5A:
            out.append(b); continue
        if i >= n: break
        nb = data[i]; i += 1
        if nb == 0x5A:
            out.append(0x5A)
        else:
            if i >= n: break
            vb = data[i]; i += 1
            out.append(vb)
            run_left = (nb - 1) & 0xFF
            in_run = 1 if run_left > 0 else 0
            run_byte = vb
    return bytes(out)


def decode_sprite(bs):
    """Block-major EGA decoder: MASK FIRST, then 4 planes. Returns (w, h, pixels)
       with pixel ∈ 0..15 or 0xFF.

       On-disk order — block 0 = mask, blocks 1..4 = planes 0..3.
       (Runtime in-memory loader rearranges на mask-last.) """
    if len(bs) < 12: return None
    wid = bs[0]
    cnt = bs[1]
    if not (1 <= cnt <= MAX_W // 16 and 1 <= wid <= MAX_H): return None
    cw = cnt * wid
    if 2 + cw * 10 != len(bs): return None
    iw, ih = cnt * 16, wid
    if iw > MAX_W or ih > MAX_H: return None
    pix = bytearray(iw * ih)
    for r in range(wid):
        for s in range(cnt):
            ws = r * cnt + s
            def W(b):
                """Читает 16-битный strip как BIG-ENDIAN: первый байт = левые
                   8 пикселей, второй = правые. EGA-стандарт: leftmost pixel =
                   bit 7 первого байта. LE-чтение (bs[p]|bs[p+1]<<8) даёт
                   bit15 = bs[p+1].bit7, что свопит две 8-пикс половинки.
                   BE-чтение + (wb = 15-bit) → leftmost = bit 7 byte 0. """
                p = 2 + (b * cw + ws) * 2
                return (bs[p] << 8) | bs[p + 1]
            mask = W(0); p0 = W(1); p1 = W(2); p2 = W(3); p3 = W(4)
            for bit in range(16):
                wb = 15 - bit
                # БЕЗ переворота: в архиве арт прижат к ЛЕВОМУ краю канвы
                # (проверено: минимальная непрозрачная колонка = 0 у всех 334
                # спрайтов), а блиттер оригинала кладёт этот левый край в
                # x_world (update_bboxes, file 0x0247A). Прежний flip
                # `col = iw-1-...` был ошибкой извлечения — из-за него в порту
                # понадобился костыль body_cx. Разворот персонажа в оригинале
                # делается ВЫБОРОМ зеркального спрайта (в архиве 75 пар), а не
                # отражением при выводе.
                col = s * 16 + bit
                if (mask >> wb) & 1:
                    pix[r * iw + col] = 0xFF
                else:
                    v = (((p0 >> wb) & 1) << 0) | \
                        (((p1 >> wb) & 1) << 1) | \
                        (((p2 >> wb) & 1) << 2) | \
                        (((p3 >> wb) & 1) << 3)
                    pix[r * iw + col] = v
    return (iw, ih, bytes(pix))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src = sys.argv[1] if len(sys.argv) > 1 \
                       else os.path.join(here, "../../SPRITES.EGA")
    out = sys.argv[2] if len(sys.argv) > 2 \
                       else os.path.join(here, "../data/karnov_sprites_ega.bin")

    with open(src, "rb") as f:
        raw = f.read()
    print(f"SPRITES.EGA: {len(raw)} bytes compressed")

    dir_bytes = rle_decompress_from(raw, 0, 2 + 338 * 8)
    count = dir_bytes[0] | (dir_bytes[1] << 8)
    print(f"Directory count: {count}")

    sprites = []
    skipped_bad = 0
    skipped_empty = 0
    for i in range(count):
        eoff = 2 + i * 8
        ai, sz, doff, bank = struct.unpack("<HHHH", dir_bytes[eoff:eoff + 8])
        foff = bank * 65536 + doff
        if sz <= 2:  # ids 334..337: заглушки без пиксельных данных
            skipped_empty += 1; continue
        if foff >= len(raw):
            skipped_bad += 1; continue
        data = rle_decompress_from(raw, foff, sz)
        if len(data) < sz:
            skipped_bad += 1; continue
        res = decode_sprite(data)
        if res is None:
            skipped_bad += 1
            print(f"  decode failed: id {i} ai_type {ai} size {sz}")
            continue
        w, h, px = res
        sprites.append((i, w, h, ai, px))

    print(f"Extracted {len(sprites)} sprites "
          f"({skipped_bad} skipped, {skipped_empty} empty placeholders)")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(struct.pack("<HHH", MAGIC, VERSION, len(sprites)))
        for sid, w, h, ai, px in sprites:
            f.write(struct.pack("<HBBBBH", sid, w, h, 4, 0, ai))
            f.write(px)
    print(f"Wrote {out}")


if __name__ == "__main__":
    main()
