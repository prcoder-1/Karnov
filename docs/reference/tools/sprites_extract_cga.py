#!/usr/bin/env python3
"""
sprites_extract_cga.py — extract sprite frames from SPRITES.CGA and
produce a cache file (data/karnov_sprites_cga.bin) for karnov_cpp.

Unlike the EGA extractor which reads a runtime DOSBox memory dump,
this reads SPRITES.CGA directly. Key insight: entry.offset in the
directory is a COMPRESSED file offset (used with bin_fseek), so we
must seek the raw file then reset the RLE decompressor.

On-disk per-sprite layout (after fresh decompression):
  +0:    width  (rows count)
  +1:    count  (number of 8-pixel words per row — NOT 16-pixel strips)
  +2..:  count*width pairs of (mask_word, pixel_word)
         - mask: 2bpp same layout as pixel; mask == 0b11 → transparent
         - pixel: standard CGA Mode 4 chunky 2bpp

Directory entry (8 bytes): u16 ai_type, u16 size, u16 offset, u16 bank.
Четвёртое слово — индекс 64КБ-банка: file_offset = bank*65536 + offset
(offset 16-битный, а архив ~110КБ; bank=1 у id 211..337).

Cache output (v4; bpp=2):
  uint16 magic   = 'KS' = 0x534B
  uint16 version = 4
  uint16 count
  per sprite:
    uint16 sprite_id, uint8 width, uint8 height, uint8 bpp=2, uint8 reserved,
    uint16 ai_type (слово 0 директории — манифест sprite→AI type)
    uint8  pixels[width*height]   (0..3 CGA color, 0xFF = transparent)
"""
import os, sys, struct

MAGIC   = 0x534B
VERSION = 4
# 128×63 — по факту: id 180 = 128×30 (cnt=16 8-пикс слов), id 172 = 64×63.
MAX_W   = 128
MAX_H   = 63


def rle_decompress_from(data, start_off, num_bytes):
    """Decompress num_bytes from compressed offset start_off (state reset).
       Matches bin_fseek + bin_fread semantics in KARNOV.EXE."""
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


def decode_sprite(sprite_bytes):
    """Decode one CGA sprite block.
       Returns (width_px, height_px, pixels[]) where pixel ∈ 0..3 or 0xFF."""
    if len(sprite_bytes) < 4:
        return None
    wid = sprite_bytes[0]
    cnt = sprite_bytes[1]
    if not (1 <= cnt <= MAX_W // 8 and 1 <= wid <= MAX_H):
        return None
    if cnt * wid * 4 + 2 != len(sprite_bytes):
        return None
    iw, ih = cnt * 8, wid
    if iw > MAX_W or ih > MAX_H:
        return None
    pix = bytearray(iw * ih)
    for r in range(wid):
        for s in range(cnt):
            o = 2 + (r * cnt + s) * 4
            m_lo = sprite_bytes[o]
            m_hi = sprite_bytes[o + 1]
            p_lo = sprite_bytes[o + 2]
            p_hi = sprite_bytes[o + 3]
            for px in range(8):
                # Горизонтальный flip (см. EGA decoder note про KARNOV.EXE
                # inner blitter): спрайты на диске хранятся mirror-image от
                # того, как игра их показывает на экране.
                col = iw - 1 - (s * 8 + px)
                byte_idx = px // 4
                in_byte = px % 4
                shift = 6 - in_byte * 2
                m_byte = m_lo if byte_idx == 0 else m_hi
                p_byte = p_lo if byte_idx == 0 else p_hi
                m_bits = (m_byte >> shift) & 3
                if m_bits == 3:
                    pix[r * iw + col] = 0xFF
                else:
                    pix[r * iw + col] = (p_byte >> shift) & 3
    return (iw, ih, bytes(pix))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    cga_path = sys.argv[1] if len(sys.argv) > 1 \
                            else os.path.join(here, "../../SPRITES.CGA")
    out_path = sys.argv[2] if len(sys.argv) > 2 \
                            else os.path.join(here, "../data/karnov_sprites_cga.bin")

    with open(cga_path, "rb") as f:
        raw = f.read()
    print(f"SPRITES.CGA: {len(raw)} bytes compressed")

    dir_bytes = rle_decompress_from(raw, 0, 2 + 338 * 8)
    count = dir_bytes[0] | (dir_bytes[1] << 8)
    print(f"Directory: {count} entries")

    sprites = []
    skipped_oor = skipped_bad = skipped_empty = 0
    for i in range(count):
        eoff = 2 + i * 8
        ai, sz, doff, bank = struct.unpack("<HHHH", dir_bytes[eoff:eoff + 8])
        foff = bank * 65536 + doff
        if sz <= 2:  # заглушки без пиксельных данных
            skipped_empty += 1
            continue
        if foff >= len(raw):
            skipped_oor += 1
            continue
        data = rle_decompress_from(raw, foff, sz)
        if len(data) < sz:
            skipped_bad += 1
            continue
        decoded = decode_sprite(data)
        if decoded is None:
            skipped_bad += 1
            print(f"  decode failed: id {i} ai_type {ai} size {sz}")
            continue
        w, h, px = decoded
        sprites.append((i, w, h, ai, px))

    print(f"Extracted {len(sprites)} valid sprites "
          f"({skipped_oor} out-of-range, {skipped_bad} bad-format, "
          f"{skipped_empty} empty placeholders)")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<HHH", MAGIC, VERSION, len(sprites)))
        for sid, w, h, ai, px in sprites:
            f.write(struct.pack("<HBBBBH", sid, w, h, 2, 0, ai))
            f.write(px)
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
