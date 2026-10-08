#!/usr/bin/env python3
"""Патч KARNOV.EXE → DSDUMP.EXE: покадровый дамп DS (см. hook.asm)."""
import os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "../../../KARNOV.EXE"))
HDR = 0x1800
LOOP_SEG, LOOP_OFF = 0x4E4, 0x350          # inc [0xCDEA]; inc [0xCDD8]
HOOK_SEG, HOOK_OFF = 0x1851, 0x0000        # 200 нулевых байт


def main(out):
    subprocess.run(["nasm", "-f", "bin", "-o", os.path.join(HERE, "hook.bin"),
                    os.path.join(HERE, "hook.asm")], check=True)
    hook = open(os.path.join(HERE, "hook.bin"), "rb").read()
    d = bytearray(open(SRC, "rb").read())
    f = HDR + LOOP_SEG * 16 + LOOP_OFF
    assert d[f:f + 8] == bytes.fromhex("ff06eacdff06d8cd"), d[f:f + 8].hex()
    d[f:f + 8] = b"\x9a" + struct.pack("<HH", HOOK_OFF, HOOK_SEG) + b"\x90" * 3
    h = HDR + HOOK_SEG * 16 + HOOK_OFF
    assert len(hook) <= 200 and not any(d[h:h + len(hook)])
    d[h:h + len(hook)] = hook
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
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "DSDUMP.EXE"))
