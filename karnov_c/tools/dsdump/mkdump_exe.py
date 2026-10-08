#!/usr/bin/env python3
"""Патч KARNOV.EXE → DSDUMP.EXE: покадровый дамп DS (см. hook.asm)."""
import os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "../../../KARNOV.EXE"))
HDR = 0x1800
LOOP_SEG, LOOP_OFF = 0x4E4, 0x350          # inc [0xCDEA]; inc [0xCDD8]
HOOK_SEG, HOOK_OFF = 0x1851, 0x0000        # 200 нулевых байт


def main(out, level=None):
    defs = ["-D%s=%s" % (k, os.environ[k]) for k in ("HSKIP", "HCOUNT", "HSEG")
            if k in os.environ]
    subprocess.run(["nasm", "-f", "bin"] + defs +
                   ["-o", out + ".hook", os.path.join(HERE, "hook.asm")], check=True)
    hook = open(out + ".hook", "rb").read()
    os.remove(out + ".hook")
    d = bytearray(open(SRC, "rb").read())
    f = HDR + LOOP_SEG * 16 + LOOP_OFF
    assert d[f:f + 8] == bytes.fromhex("ff06eacdff06d8cd"), d[f:f + 8].hex()
    d[f:f + 8] = b"\x9a" + struct.pack("<HH", HOOK_OFF, HOOK_SEG) + b"\x90" * 3
    h = HDR + HOOK_SEG * 16 + HOOK_OFF
    assert len(hook) <= 200 and not any(d[h:h + len(hook)])
    d[h:h + len(hook)] = hook
    if level is not None:
        # new_game: уровень старта = level и в демо (je → jmp, mov ax,1 → mov ax,level)
        f = HDR + 0x4E4 * 16 + 0x448
        assert d[f:f + 9] == bytes.fromhex("740433c0eb03b80100"), d[f:f + 9].hex()
        d[f] = 0xEB
        d[f + 7] = level
    # Новая релокация на слово сегмента в far-call.
    n, = struct.unpack_from("<H", d, 6)
    tab, = struct.unpack_from("<H", d, 0x18)
    e = tab + n * 4
    assert e + 4 <= HDR and not any(d[e:e + 4])
    struct.pack_into("<HH", d, e, LOOP_OFF + 3, LOOP_SEG)
    struct.pack_into("<H", d, 6, n + 1)
    open(out, "wb").write(d)
    print("→", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "DSDUMP.EXE"),
         int(sys.argv[2]) if len(sys.argv) > 2 else None)
