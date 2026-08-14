#!/usr/bin/env python3
"""
sprites_extract.py — extract sprite frames from a DOSBox runtime dump of
the Karnov in-memory sprite buffer and produce a cache file for karnov_cpp.

Usage:
  ./sprites_extract.py [<output_path>]

How to capture the runtime dump (single DEBUG session, two writes):
  See /tmp/karnov_dos/CMDST3.TXT — writes seg 0x6003 to SPRA.BIN and
  seg 0x7003 to SPRB.BIN. Concatenate them; the directory is at +0x12 in
  the first half.

In-memory format (verified — block-major 4bpp EGA, mask LAST):
  Buffer offset 0x12: 338 × 4-byte FAR PTR entries
  Each FAR PTR (off, seg) → sprite data at seg*16 + off - BUFFER_SEG*16

  Sprite data:
    +00..+05: 3 indirection WORDs (first = 0, others = sprite total size)
    +06:      WORD strip header — high byte = count (strips per row,
                                                     each strip = 16 px),
                                  low byte  = width (rows count)
    +08+:     pixel data = 5 blocks of (count*width) WORDs each:
              block 0: plane 0 (R bit)
              block 1: plane 1 (G bit)
              block 2: plane 2 (B bit)
              block 3: plane 3 (I bit)
              block 4: mask  (1 = transparent at that bit)

  Pixel address (block b, row r, strip s):
    +8 + (b * count * width + r * count + s) * 2

  Color for a non-masked pixel = bit-i of (p0, p1, p2, p3) packed into
  4-bit EGA color index 0..15.

Output cache format (little-endian, version 3):
  uint16 magic = 'KS' = 0x534B
  uint16 version = 3
  uint16 count
  per sprite:
    uint16 sprite_id
    uint8  width    (= count*16, max 96)
    uint8  height   (= width field from strip header)
    uint8  bpp      (always 4)
    uint8  reserved
    uint8  pixels[height*width]  (pixel value 0..15 = EGA color, 0xFF = transparent)
"""
import os, sys, struct

BUFFER_BASE = 0x60030          # SEG 0x6003 × 16
MAGIC       = 0x534B
VERSION     = 3
MAX_W       = 96               # count up to 6 → 6*16 = 96
MAX_H       = 60

def decode_sprite(buf, off):
    if off + 8 > len(buf):
        return None
    sh = buf[off+6] | (buf[off+7] << 8)
    width = sh & 0xFF       # rows
    count = (sh >> 8) & 0xFF
    if not (1 <= count <= 6 and 1 <= width <= MAX_H):
        return None
    sz = buf[off+2] | (buf[off+3] << 8)
    expected = count * width * 10 + 2
    if sz != expected:
        return None
    img_w = count * 16
    img_h = width
    if img_w > MAX_W or img_h > MAX_H:
        return None
    if off + 8 + count * width * 10 > len(buf):
        return None
    pixels = bytearray(img_w * img_h)
    block_words = count * width
    base = off + 8
    for r in range(img_h):
        for s in range(count):
            ws_idx = r * count + s
            def W(b):
                """BIG-ENDIAN word read: byte 0 = leftmost 8 px, byte 1 = right.
                EGA stores leftmost-pixel in bit 7 of FIRST byte; combined с
                `wb = 15-bit` индексацией это даёт правильное соответствие.
                LE-чтение свопит две 8-pixel половинки strip'а."""
                p = base + (b * block_words + ws_idx) * 2
                return (buf[p] << 8) | buf[p + 1]
            p0 = W(0)   # plane 0 (R bit)
            p1 = W(1)   # plane 1 (G bit)
            p2 = W(2)   # plane 2 (B bit)
            p3 = W(3)   # plane 3 (I bit)
            m  = W(4)   # mask — at END (verified empirically: mask-first
                        # interpretation produced 3 ghost figures per sprite)
            for bit in range(16):
                wbit = 15 - bit
                col = s * 16 + bit
                if (m >> wbit) & 1:
                    pixels[r * img_w + col] = 0xFF
                else:
                    ega = (((p0 >> wbit) & 1) << 0) | \
                          (((p1 >> wbit) & 1) << 1) | \
                          (((p2 >> wbit) & 1) << 2) | \
                          (((p3 >> wbit) & 1) << 3)
                    pixels[r * img_w + col] = ega
    return (img_w, img_h, bytes(pixels))

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    a_path = os.path.join(here, "../../out/spr_full_a.bin")
    b_path = os.path.join(here, "../../out/spr_full_b.bin")
    out_path = sys.argv[1] if len(sys.argv) > 1 \
                            else os.path.join(here, "../data/karnov_sprites.bin")

    with open(a_path, "rb") as f:
        pa = f.read()
    with open(b_path, "rb") as f:
        pb = f.read()
    combined = pa + pb
    print(f"Combined buffer: {len(combined)} bytes")

    sprites = []
    skipped_oor = 0
    skipped_bad = 0
    for i in range(338):
        p = 0x12 + i * 4
        off_w = pa[p]   | (pa[p+1] << 8)
        seg_w = pa[p+2] | (pa[p+3] << 8)
        abs_off = seg_w * 16 + off_w - BUFFER_BASE
        if not (0 <= abs_off < len(combined) - 8):
            skipped_oor += 1
            continue
        decoded = decode_sprite(combined, abs_off)
        if decoded is None:
            skipped_bad += 1
            continue
        w, h, px = decoded
        sprites.append((i, w, h, px))

    print(f"Extracted {len(sprites)} valid sprites "
          f"({skipped_oor} out-of-range, {skipped_bad} bad-format)")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<HHH", MAGIC, VERSION, len(sprites)))
        for sid, w, h, px in sprites:
            f.write(struct.pack("<HBBBB", sid, w, h, 4, 0))
            f.write(px)
    print(f"Wrote {out_path}")

if __name__ == "__main__":
    main()
