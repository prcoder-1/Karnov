#!/usr/bin/env python3
"""Decode Karnov LEVEL%.BIN (RLE) and LEVEL%.MOV (demo) files."""
import struct, sys, os

def rle_decompress(data):
    """RLE: 0x5A is escape. 0x5A 0x5A => literal 0x5A. 0x5A N B (N!=0x5A) => byte B repeated N times (256 if N==0)."""
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]; i += 1
        if b != 0x5A:
            out.append(b); continue
        if i >= len(data): break
        n = data[i]; i += 1
        if n == 0x5A:
            out.append(0x5A); continue
        # 0x5A N B
        if i >= len(data): break
        repeat_byte = data[i]; i += 1
        cnt = 256 if n == 0 else n
        out.extend([repeat_byte]*cnt)
    return bytes(out)


def parse_mov(path):
    with open(path, 'rb') as f:
        data = f.read()
    start_frame, num_records = struct.unpack('<HH', data[:4])
    records = []
    pos = 4
    for _ in range(num_records):
        state, frame = struct.unpack('<BH', data[pos:pos+3])
        records.append((frame, state))
        pos += 3
    return start_frame, num_records, records


def parse_bin(path):
    with open(path, 'rb') as f:
        raw = f.read()
    dec = rle_decompress(raw)
    sections = {}
    p = 0
    # Section 1: object table — WORD count, then count*12 byte records
    n1 = struct.unpack('<H', dec[p:p+2])[0]; p += 2
    sec1 = []
    for _ in range(n1):
        rec = struct.unpack('<6H', dec[p:p+12])
        sec1.append(rec); p += 12
    sections['objects'] = sec1
    # Section 2: enemy/spawn table — WORD count, then count*20 byte records
    n2 = struct.unpack('<H', dec[p:p+2])[0]; p += 2
    sec2 = []
    for _ in range(n2):
        rec = struct.unpack('<10H', dec[p:p+20])
        sec2.append(rec); p += 20
    sections['enemies'] = sec2
    # Then 3 more WORD headers: num_map_rows, num_unique_tiles, tile_size_bytes
    num_map_rows, num_tiles, tile_size_bytes = struct.unpack('<HHH', dec[p:p+6]); p += 6
    sections['num_map_rows'] = num_map_rows
    sections['num_tiles'] = num_tiles
    sections['tile_size_bytes'] = tile_size_bytes
    # Aliases for backward compatibility
    sections['hdr_word'] = num_map_rows
    sections['num_blocks'] = num_tiles
    sections['blk_words'] = tile_size_bytes
    sections['pos_after_hdr'] = p
    sections['raw_size'] = len(raw)
    sections['decoded_size'] = len(dec)
    sections['decoded_data'] = dec
    return sections


if __name__ == '__main__':
    # os.chdir('/mnt/storage2/Games/Karnov')
    print("=== MOV (demo) files ===")
    for name in ['LEVEL0.MOV','LEVEL1.MOV','LEVEL2.MOV']:
        sf, n, recs = parse_mov(name)
        print(f"\n{name}: start_frame={sf}, num_records={n}, total_demo_frames={recs[-1][0] if recs else 0}")
        # Show first 5 and last 3 records
        for i, (frame, st) in enumerate(recs[:5]):
            dirs = []
            if st & 0x01: dirs.append('R')
            if st & 0x02: dirs.append('L')
            if st & 0x04: dirs.append('D')
            if st & 0x08: dirs.append('U')
            if st & 0x10: dirs.append('FIRE')
            if st & 0x20: dirs.append('USE')
            print(f"  [{i:3}] frame={frame:5} state=0x{st:02x} {'+'.join(dirs) or 'none'}")
        if len(recs) > 5:
            print(f"  ...")
            for i, (frame, st) in enumerate(recs[-2:], start=len(recs)-2):
                print(f"  [{i:3}] frame={frame:5} state=0x{st:02x}")

    print("\n\n=== BIN (level) files ===")
    for name in ['LEVEL0.BIN','LEVEL1.BIN','LEVEL2.BIN','LEVEL3.BIN','LEVEL9.BIN']:
        s = parse_bin(name)
        print(f"\n{name}: raw={s['raw_size']} decoded={s['decoded_size']}")
        print(f"  objects ({len(s['objects'])}):")
        for i, rec in enumerate(s['objects'][:3]):
            print(f"    [{i}] {rec}")
        print(f"  enemies ({len(s['enemies'])}):")
        for i, rec in enumerate(s['enemies'][:3]):
            print(f"    [{i}] {rec}")
        print(f"  hdr_word={s['hdr_word']:#x}, num_blocks={s['num_blocks']}, blk_words={s['blk_words']:#x}")
        print(f"  pos after fixed header: {s['pos_after_hdr']} of {s['decoded_size']}")
