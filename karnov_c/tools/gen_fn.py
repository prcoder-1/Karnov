#!/usr/bin/env python3
"""gen_fn.py — по тегам `/* @SEG:OFF file:FFFFF имя [HW|RTL] ... */` перед
определениями функций в src/orig/*.c генерирует:
  include/orig/fn.h  — прототипы всех нестатических функций с адресами;
  src/orig/fnreg.c   — реестр {seg, off, указатель, имя} для far-указателей.
Проверяет, что file: совпадает с адресом, и что адреса не повторяются.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
KC = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(KC, "src/orig")

TAG = re.compile(
    r"/\*\s*@([0-9A-F]{4}):([0-9A-F]{4})\s+file:([0-9A-F]{5})\s+((?:[^*]|\*(?!/))*?)\*/\s*\n"
    r"((?:static\s+)?[^;{}()]*?\b(\w+)\s*\(([^)]*)\))\s*\{", re.S)


def collect():
    funcs, errors = [], []
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(".c"):
            continue
        text = open(os.path.join(SRC, fn), encoding="utf-8").read()
        for m in TAG.finditer(text):
            seg, off, fo = (int(m.group(i), 16) for i in (1, 2, 3))
            if fo != 0x1800 + seg * 16 + off:
                errors.append(f"{fn}: @{seg:04X}:{off:04X} file:{fo:05X} — "
                              f"должно быть {0x1800 + seg * 16 + off:05X}")
            sig = " ".join(m.group(5).split())
            funcs.append(dict(seg=seg, off=off, name=m.group(6), sig=sig,
                              static=sig.startswith("static"),
                              note=m.group(4).strip(), file=fn))
    seen = {}
    for f in funcs:
        k = (f["seg"], f["off"])
        if k in seen:
            errors.append(f"повтор @{k[0]:04X}:{k[1]:04X}: {seen[k]} и {f['name']}")
        seen[k] = f["name"]
    return funcs, errors


def main():
    funcs, errors = collect()
    for e in errors:
        print("gen_fn:", e, file=sys.stderr)
    if errors:
        sys.exit(1)
    pub = [f for f in funcs if not f["static"]]
    with open(os.path.join(KC, "include/orig/fn.h"), "w") as h:
        h.write("/* Сгенерировано tools/gen_fn.py — не править. */\n"
                "#ifndef ORIG_FN_H\n#define ORIG_FN_H\n#include <stdio.h>\n#include \"orig/ds.h\"\n\n")
        for f in sorted(pub, key=lambda f: (f["seg"], f["off"])):
            h.write(f"{f['sig']};{' ' * max(1, 52 - len(f['sig']))}"
                    f"/* @{f['seg']:04X}:{f['off']:04X} */\n")
        h.write("\n#endif\n")
    with open(os.path.join(SRC, "fnreg.c"), "w") as c:
        c.write("/* Сгенерировано tools/gen_fn.py — не править. */\n"
                "#include \"orig/fn.h\"\n\nconst FnReg fn_reg[] = {\n")
        for f in sorted(pub, key=lambda f: (f["seg"], f["off"])):
            c.write(f"    {{ 0x{f['seg']:04X}, 0x{f['off']:04X}, "
                    f"(farfn){f['name']}, \"{f['name']}\" }},\n")
        if not pub:
            c.write("    { 0, 0, 0, 0 },\n")
        c.write("};\nconst unsigned fn_nreg = %d;\n" % len(pub))


if __name__ == "__main__":
    main()
