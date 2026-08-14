#!/usr/bin/env python3
"""Decode the runtime sprite-buffer dump (SPRDUMP.BIN) produced by the
KARNOV_DUMP.EXE patch, extract the scroll/level-end sprites #329..#333
(ai_type56, composite/reserved=1 on disk) and append them to the C-port
sprite cache karnov_c/data/karnov_sprites_ega.bin (KS v3).

Dump layout (written by the injected routine at the blit hook 0xCF44):
  offset 0 : "KSPD"
  offset 4 : u16 table_off, u16 table_seg, u16 buf_seg
  offset 16: 256 KB of contiguous memory starting at buf_seg:0

Runtime sprite format (from the blit disasm): the far-ptr table lives at
table_seg:table_off, indexed by id*4 (4-byte far ptr per sprite). Each sprite
is [6-byte frame-offset table][wid(1),cnt(1)][5 planes p0..p3,mask]. Frame 0 =
skip the 6 bytes; decode_ega handles the rest.
"""
import sys, struct, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from karnov_atlas import decode_ega, write_png

ROOT  = os.path.abspath(os.path.join(HERE, "..", ".."))
CACHE = os.path.join(ROOT, "karnov_c", "data", "karnov_sprites_ega.bin")
IDS   = list(range(328, 334))          # 328 (type55) + 329..333 (type56 scroll)

def decode_dump(path):
    d = open(path, "rb").read()
    if d[:4] != b"KSPD":
        sys.exit(f"bad magic {d[:4]!r} — not a SPRDUMP.BIN (size={len(d)})")
    table_off, table_seg, buf_seg = struct.unpack("<HHH", d[4:10])
    HDR = 16
    base_lin = buf_seg * 16
    print(f"table={table_seg:04X}:{table_off:04X} buf_seg={buf_seg:04X} data={len(d)-HDR} bytes")

    def idx(lin):                       # linear addr -> index in dump
        j = HDR + (lin - base_lin)
        return j if 0 <= j < len(d) - 4 else None

    table_lin = table_seg * 16 + table_off
    out = {}
    for sid in IDS:
        j = idx(table_lin + sid * 4)
        if j is None:
            print(f"#{sid}: table entry out of range"); continue
        soff, sseg = struct.unpack("<HH", d[j:j+4])
        k = idx(sseg * 16 + soff)
        if k is None:
            print(f"#{sid}: sprite ptr {sseg:04X}:{soff:04X} out of dumped range"); continue
        raw = d[k:k+4096]
        best = None
        # try frame 0 (skip 6) and a couple of fallbacks (skip 0/2) just in case
        for skip in (6, 0, 2, 8):
            if len(raw) < skip + 2:
                continue
            wid, cnt = raw[skip], raw[skip+1]
            size = 2 + cnt * wid * 10
            if 0 < size <= len(raw) - skip:
                res = decode_ega(raw[skip:skip+size])
                if res:
                    best = (skip, wid, cnt, res); break
        if best:
            skip, wid, cnt, (iw, ih, pix) = best
            print(f"#{sid}: {sseg:04X}:{soff:04X} skip={skip} {iw}x{ih}  OK")
            out[sid] = (iw, ih, pix)
            png = os.path.join(HERE, f"scroll_{sid}.png")
            rgb = bytearray(iw*ih*3)
            PAL = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),
                   (170,85,0),(170,170,170),(85,85,85),(85,85,255),(85,255,85),
                   (85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
            for p, v in enumerate(pix):
                r,g,b = (255,0,255) if v==0xFF else PAL[v & 15]
                rgb[p*3:p*3+3] = bytes((r,g,b))
            write_png(png, iw, ih, bytes(rgb)); print(f"   -> {png}")
        else:
            print(f"#{sid}: {sseg:04X}:{soff:04X} decode FAILED  raw={raw[:24].hex(' ')}")
    return out

def append_cache(sprites):
    if not sprites:
        print("nothing to append"); return
    d = bytearray(open(CACHE, "rb").read())
    magic, ver, count = struct.unpack("<HHH", d[:6])
    assert magic == 0x534B and ver == 3, f"unexpected cache header {magic:04X} v{ver}"
    # index existing ids
    off = 6; existing = {}
    for _ in range(count):
        sid, w, h, bpp, _rsv = struct.unpack("<HBBBB", d[off:off+6])
        existing[sid] = off; off += 6 + w*h
    add = 0
    for sid, (w, h, pix) in sorted(sprites.items()):
        if sid in existing:
            print(f"#{sid} already in cache — skipping (delete it first to replace)"); continue
        d += struct.pack("<HBBBB", sid, w, h, 4, 1) + pix
        add += 1
    struct.pack_into("<H", d, 4, count + add)
    open(CACHE, "wb").write(d)
    print(f"appended {add} sprites; cache now {count+add} entries, {len(d)} bytes")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: decode_sprdump.py SPRDUMP.BIN [--append]")
    spr = decode_dump(sys.argv[1])
    if "--append" in sys.argv:
        append_cache(spr)
