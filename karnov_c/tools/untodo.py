#!/usr/bin/env python3
"""untodo.py имя... — убрать переведённые функции из todo.h/todo.c;
todo.py add 'тип имя(void)' SEG:OFF — добавить заглушку."""
import os, re, sys
KC = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
H, C = os.path.join(KC, "include/orig/todo.h"), os.path.join(KC, "src/orig/todo.c")


def main():
    h, c = open(H).read(), open(C).read()
    if sys.argv[1] == "add":
        sig, addr = sys.argv[2], sys.argv[3]
        m = re.match(r"(.*?\b)(\w+)\((.*)\)$", sig)
        ret, name = m.group(1).strip(), m.group(2)
        h = h.replace("\n#endif", f"{sig};{' ' * max(1, 32 - len(sig))}/* {addr} */\n\n#endif", 1)
        body = f'    port_todo("{addr} {name}");' + ("" if ret == "void" else "\n    return 0;")
        c += f"\n{sig}\n{{\n{body}\n}}\n"
    else:
        for n in sys.argv[1:]:
            h = re.sub(r"^.*\b%s\(.*\);.*\n" % n, "", h, flags=re.M)
            c = re.sub(r"\n?[^\n;{}]*\b%s\([^)]*\)\n\{\n.*?\n\}\n" % n, "\n", c, flags=re.S)
    h = re.sub(r"\n{3,}", "\n\n", h)
    c = re.sub(r"\n{3,}", "\n\n", c)
    open(H, "w").write(h); open(C, "w").write(c)


if __name__ == "__main__":
    main()
