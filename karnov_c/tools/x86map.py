#!/usr/bin/env python3
"""
x86map.py — манифест функций KARNOV.EXE и аннотированные листинги.

Рекурсивный спуск (capstone, 16 бит) от:
  * точки входа 0000:0000;
  * целей всех far-вызовов `9A off seg` (по релокациям);
  * far-указателей на код в инициализированном DS (по релокациям);
  * ближних вызовов, найденных в уже разобранном коде;
  * прологов в «дырах» между функциями (помечаются как unref).
Jump-таблицы `jmp [cs:bx+T]` раскрываются по предшествующему `cmp r,N`.

Использование:
  x86map.py scan                 → tools/functions.csv + сводка
  x86map.py list SEG:OFF         → листинг функции с подписями
  x86map.py gaps [SEG]           → неразобранные участки сегментов
  x86map.py xref SEG:OFF         → кто вызывает функцию
"""
import csv, os, re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone import x86 as X

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../.."))
EXE = os.path.join(ROOT, "KARNOV.EXE")
CSV = os.path.join(HERE, "functions.csv")

HDR = 0x1800
DS_SEG = 0x1D99
DS_FILE = HDR + DS_SEG * 16
ASM_SEGS = {0x5A3, 0x920}
RTL_FIRST = 0x1B02
RTL_SEGS = {0x1851, 0x1AB2}       # эмулятор 8087 Turbo-C

D = open(EXE, "rb").read()


def relocations():
    n, = struct.unpack_from("<H", D, 6)
    tab, = struct.unpack_from("<H", D, 0x18)
    out = set()
    for i in range(n):
        o, s = struct.unpack_from("<HH", D, tab + i * 4)
        out.add(HDR + s * 16 + o)
    return out


RELOCS = relocations()
CODE_SEGS = sorted({struct.unpack_from("<H", D, f)[0] for f in RELOCS
                    if HDR + struct.unpack_from("<H", D, f)[0] * 16 < DS_FILE})


def seg_end(seg):
    """Размер сегмента = до начала следующего (по релокациям)."""
    i = CODE_SEGS.index(seg)
    nxt = CODE_SEGS[i + 1] if i + 1 < len(CODE_SEGS) else DS_SEG
    return (nxt - seg) * 16


def seg_limit(seg):
    # Следующий модуль начинается с параграфа, где ещё лежит хвост этого.
    return seg_end(seg) + 16


def fileoff(seg, off):
    return HDR + seg * 16 + off


def klass(seg):
    if seg == 0 or seg >= RTL_FIRST or seg in RTL_SEGS:
        return "rtl"
    if seg in ASM_SEGS:
        return "asm"
    return "game"


def fmt(seg, off):
    return f"{seg:04X}:{off:04X}"


def parse_addr(s):
    a, b = s.split(":")
    return int(a, 16), int(b, 16)


# ---------------------------------------------------------------- symbols

def load_symbols():
    """tools/symbols_seed.txt + @-теги и DSVAR из src/orig (если есть)."""
    funcs, data = {}, {}
    seed = os.path.join(HERE, "symbols_seed.txt")
    if os.path.exists(seed):
        for line in open(seed, encoding="utf-8"):
            line = line.split(";")[0].strip()
            if not line:
                continue
            p = line.split()
            if p[0] == "F":
                funcs[parse_addr(p[1])] = p[2]
            elif p[0] == "D":
                data[int(p[1], 16)] = p[2]
    src = os.path.join(ROOT, "karnov_c/src/orig")
    tag = re.compile(r"/\*\s*@([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\b[^*]*?\*/\s*\n"
                     r"(?:static\s+)?[\w\s\*]*?\b(\w+)\s*\(")
    if os.path.isdir(src):
        for fn in sorted(os.listdir(src)):
            if fn.endswith(".c"):
                t = open(os.path.join(src, fn), encoding="utf-8").read()
                for m in tag.finditer(t):
                    funcs[(int(m.group(1), 16), int(m.group(2), 16))] = m.group(3)
    data["__vars__"] = ds_symbols()
    return funcs, data


def ds_symbols():
    """Переменные DS перевода: адрес — из комментария /* XXXX: */ в
    include/orig/ds.h или из имени вида *_XXXX → [(адрес, имя, размер, n)]."""
    sizes = {"u8": 1, "s8": 1, "char": 1, "u32": 4, "s32": 4, "farfn": 4, "StateEntry": 8,
             "Motion": 16, "CollRes": 0x26, "ActiveObject": 0xF5, "EnemySpawn": 20,
             "ObjRegion": 12, "Demo": 0x304, "PlayerRec": 0xA5}
    decl = re.compile(r"^\s*(?:extern |static )?(\w+)\s*(\*?)\s*\(?\*?(\w+)\)?"
                      r"(?:\[(\w+)\])?[^;]*;\s*(?:/\*\s*([0-9A-F]{4})\b)?")
    out, prefix = [], ""
    for f in ("include/orig/ds.h", "src/orig/ds.c"):
        path = os.path.join(ROOT, "karnov_c", f)
        if not os.path.exists(path):
            continue
        for line in open(path, encoding="utf-8"):
            if line.startswith("typedef struct {"):
                prefix = "plr."
            elif line.startswith("}"):
                prefix = ""
            m = decl.match(line)
            if not m or m.group(1) in ("typedef", "return", "struct"):
                continue
            t, ptr, name, n, addr = m.groups()
            addr = addr or (re.search(r"_([0-9A-F]{4})$", name) or [None, None])[1]
            if addr is None or (prefix == "" and line.startswith("    ")):
                continue
            size = 4 if ptr else sizes.get(t, 2)
            n = int(n, 0) if n else 1
            if "far-указатель" in line:     # массив перевода, в DS — указатель
                size, n = 4, 1
            out.append((int(addr, 16), prefix + name, size, n))
    return out


def ds_name_of(addr, vars_):
    for a, n, size, cnt in vars_:
        if a <= addr < a + size * cnt:
            i, r = divmod(addr - a, size)
            s = f"{n}[{i}]" if cnt > 1 else n
            return s + (f"+{r}" if r else "")
    return "?"


FIELDS = {
    0x00: "x_screen", 0x02: "y_screen", 0x04: "+04", 0x06: "+06",
    0x08: "x_world", 0x0A: "+0A", 0x0C: "y_world",
    0x0E: "vx_lo", 0x10: "vx_hi", 0x12: "vy_lo", 0x14: "vy_hi",
    0x16: "ax_lo", 0x18: "ax_hi", 0x1A: "ay_lo", 0x1C: "ay_hi",
    0x1E: "num_bboxes",
    0xB0: "alive", 0xB2: "next_idx", 0xB4: "prev_idx", 0xB6: "state_byte",
    0xB7: "handler_lo", 0xB9: "handler_hi", 0xBB: "ai_init_lo",
    0xBD: "ai_init_hi", 0xBF: "ai_next_lo", 0xC1: "ai_next_hi",
    0xC3: "anim_len", 0xC5: "frame_idx", 0xC7: "frame_ctr",
    0xC9: "tick_period", 0xCB: "throttle", 0xCD: "state_anim_lo",
    0xCF: "state_anim_hi", 0xD1: "state_offs_lo", 0xD3: "state_offs_hi",
    0xD5: "state_list_lo", 0xD7: "state_list_hi", 0xD9: "next_state_id",
    0xDB: "active", 0xDD: "hit_flag", 0xDF: "hit_idx", 0xE1: "hit_box_self",
    0xE3: "hit_box_other", 0xE5: "obj_type", 0xE7: "hit_points",
    0xE9: "misc", 0xEB: "phase_ctr", 0xED: "phase", 0xEF: "aux",
    0xF1: "parent_lo", 0xF3: "parent_hi",
}
BOX = ["sprite", "dx", "dy", "x0", "y0", "x1", "y1", "hitflag", "was_vis"]


def field_name(off):
    if 0x20 <= off < 0xB0:
        i, r = divmod(off - 0x20, 18)
        return f"box{i}.{BOX[r // 2]}" + ("" if r % 2 == 0 else "+1")
    return FIELDS.get(off)


# ---------------------------------------------------------------- disasm

MD = Cs(CS_ARCH_X86, CS_MODE_16)
MD.detail = True

COND_JMP = {X.X86_INS_JAE, X.X86_INS_JA, X.X86_INS_JBE, X.X86_INS_JB,
            X.X86_INS_JCXZ, X.X86_INS_JE, X.X86_INS_JGE, X.X86_INS_JG,
            X.X86_INS_JLE, X.X86_INS_JL, X.X86_INS_JNE, X.X86_INS_JNO,
            X.X86_INS_JNP, X.X86_INS_JNS, X.X86_INS_JO, X.X86_INS_JP,
            X.X86_INS_JS, X.X86_INS_LOOP, X.X86_INS_LOOPE, X.X86_INS_LOOPNE}
RETS = {X.X86_INS_RET, X.X86_INS_RETF, X.X86_INS_IRET}


def insn_at(seg, off):
    base = fileoff(seg, off)
    for i in MD.disasm(D[base:base + 16], off, 1):
        return i
    return None


def far_target(i):
    """call/jmp ptr16:16 → (seg, off), только если сегмент релоцирован."""
    if i.bytes[0] in (0x9A, 0xEA) and len(i.bytes) == 5:
        off, seg = struct.unpack_from("<HH", bytes(i.bytes), 1)
        return seg, off
    return None


def jump_table(seg, insns, i):
    """jmp [cs:bx+T] → список целей. Длина — из ближайшего cmp r,N выше по адресу."""
    op = i.operands[0]
    if op.type != X.X86_OP_MEM or i.prefix[1] != 0x2E:
        return None
    tbl = op.mem.disp & 0xFFFF
    n = None
    prev = [insns[a] for a in sorted(insns) if i.address - 0x20 <= a < i.address]
    for p in reversed(prev[-8:]):
        if p.id == X.X86_INS_OR and p.op_str.split(", ")[0] == p.op_str.split(", ")[-1]:
            n = 1                       # sub ax,K; or ax,ax; ja → единственный case
            break
        if p.id == X.X86_INS_CMP and len(p.operands) == 2 and \
                p.operands[1].type == X.X86_OP_IMM:
            n = (p.operands[1].imm & 0xFFFF) + 1
            break
    if n is None or n > 256:
        return tbl, []
    tgts = [struct.unpack_from("<H", D, fileoff(seg, tbl + 2 * k))[0]
            for k in range(n)]
    return tbl, tgts


def walk(seg, entry, limit):
    """Разбор одной функции. → insns{off:insn}, near_calls, far_calls, tables, issues."""
    insns, todo = {}, [entry]
    near, far, tables, issues = set(), set(), {}, []
    order = []
    while todo:
        pc = todo.pop()
        while pc not in insns:
            if pc >= limit:
                issues.append(f"выход за сегмент @{pc:04X}")
                break
            i = insn_at(seg, pc)
            if i is None:
                issues.append(f"не дизассемблируется @{pc:04X}")
                break
            insns[pc] = i
            order.append(i)
            nxt = pc + i.size
            if i.id == X.X86_INS_CALL:
                ft = far_target(i)
                if ft:
                    far.add(ft)
                elif i.operands[0].type == X.X86_OP_IMM:
                    near.add(i.operands[0].imm & 0xFFFF)
                pc = nxt
                continue
            if i.id in RETS:
                break
            if i.id in (X.X86_INS_JMP, X.X86_INS_LJMP):
                ft = far_target(i)
                if ft:
                    far.add(ft)
                    break
                o = i.operands[0]
                if o.type == X.X86_OP_IMM:
                    pc = o.imm & 0xFFFF
                    continue
                jt = jump_table(seg, insns, i)
                if jt:
                    tbl, tg = jt
                    tables[tbl] = tg
                    if not tg:
                        issues.append(f"таблица без длины @{pc:04X}")
                    todo.extend(tg)
                else:
                    issues.append(f"косвенный jmp @{pc:04X}: {i.mnemonic} {i.op_str}")
                break
            if i.id in COND_JMP:
                todo.append(i.operands[0].imm & 0xFFFF)
            if i.id == X.X86_INS_INT and i.operands[0].imm == 0x20:
                break
            pc = nxt
    return insns, near, far, tables, issues


PROLOGUES = [b"\x55\x8b\xec", b"\x56\x55\x8b\xec", b"\x57\x55\x8b\xec",
             b"\x56\x57\x55\x8b\xec"]


def seeds():
    s = {(0, 0): "entry"}
    for k in load_symbols()[0]:
        s[k] = "sym"
    for f in range(HDR, DS_FILE - 4):
        if D[f] == 0x9A and (f + 3) in RELOCS:
            off, seg = struct.unpack_from("<HH", D, f + 1)
            if seg in CODE_SEGS:
                s.setdefault((seg, off), "far")
    for f in RELOCS:
        if f >= DS_FILE:
            seg, = struct.unpack_from("<H", D, f)
            off, = struct.unpack_from("<H", D, f - 2)
            if seg in CODE_SEGS:
                s.setdefault((seg, off), "dsptr")
    return s


def scan():
    funcs = {}           # (seg,off) → dict
    work = dict(seeds())

    def run():
        while work:
            key, how = work.popitem()
            if key in funcs:
                continue
            seg, off = key
            if off >= seg_limit(seg):
                continue                # мусорный «указатель» (напр. B800 в DS)
            ins, near, far, tables, issues = walk(seg, off, seg_limit(seg))
            funcs[key] = dict(insns=ins, near=near, far=far, tables=tables,
                              issues=issues, how=how)
            for n in near:
                if (seg, n) not in funcs:
                    work.setdefault((seg, n), "near")
            for f in far:
                if f[0] in CODE_SEGS and f not in funcs:
                    work.setdefault(f, "far")
    run()
    # Прологи в непокрытых участках — функции без явных ссылок.
    for _ in range(4):
        for seg in CODE_SEGS:
            if klass(seg) == "rtl" and seg != 0:
                pass
            cov = coverage_map(funcs, seg)
            end = seg_end(seg)
            off = 0
            while off < end:
                if not cov[off]:
                    b = D[fileoff(seg, off):fileoff(seg, off) + 5]
                    if any(b.startswith(p) for p in PROLOGUES):
                        work.setdefault((seg, off), "unref")
                        off += 3
                        continue
                off += 1
        if not work:
            break
        run()
    return funcs


def coverage_map(funcs, seg):
    end = seg_end(seg)
    cov = bytearray(end)
    for (s, _), f in funcs.items():
        if s != seg:
            continue
        for o, i in f["insns"].items():
            for k in range(o, min(end, o + i.size)):
                cov[k] = 1
        for t, tg in f["tables"].items():
            for k in range(t, min(end, t + 2 * len(tg))):
                cov[k] = 1
    return cov


def callers(funcs):
    rev = {}
    for (seg, off), f in funcs.items():
        for n in f["near"]:
            rev.setdefault((seg, n), set()).add((seg, off))
        for t in f["far"]:
            rev.setdefault(t, set()).add((seg, off))
    return rev


def write_csv(funcs):
    syms, _ = load_symbols()
    rev = callers(funcs)
    with open(CSV, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["addr", "file", "size", "ninsn", "class", "how", "name",
                    "ncallers", "callees", "issues"])
        for key in sorted(funcs):
            f = funcs[key]
            seg, off = key
            hi = max(o + i.size for o, i in f["insns"].items()) if f["insns"] else off
            callees = sorted({(seg, n) for n in f["near"]} | f["far"])
            w.writerow([fmt(*key), f"{fileoff(*key):05X}", hi - off,
                        len(f["insns"]), klass(seg), f["how"],
                        syms.get(key, ""), len(rev.get(key, ())),
                        " ".join(fmt(*c) for c in callees),
                        " | ".join(f["issues"])])


def cmd_scan():
    funcs = scan()
    write_csv(funcs)
    by = {}
    for (seg, _), f in funcs.items():
        by.setdefault(klass(seg), 0)
        by[klass(seg)] += 1
    print(f"функций: {len(funcs)}  " +
          "  ".join(f"{k}={v}" for k, v in sorted(by.items())))
    tot_iss = sum(1 for f in funcs.values() if f["issues"])
    print(f"с проблемами разбора: {tot_iss}")
    print(f"{'seg':>6} {'class':5} {'funcs':>5} {'covered':>8} {'size':>6}")
    for seg in CODE_SEGS:
        cov = coverage_map(funcs, seg)
        n = sum(1 for s, _ in funcs if s == seg)
        print(f"{seg:#06x} {klass(seg):5} {n:5d} {sum(cov):8d} {len(cov):6d}")
    print("→", CSV)


def cmd_gaps(only=None):
    funcs = scan()
    for seg in CODE_SEGS:
        if only is not None and seg != only:
            continue
        cov = coverage_map(funcs, seg)
        off, end = 0, len(cov)
        while off < end:
            if cov[off]:
                off += 1
                continue
            a = off
            while off < end and not cov[off]:
                off += 1
            blob = D[fileoff(seg, a):fileoff(seg, off)]
            if blob.strip(b"\x00\x90") == b"":
                continue
            print(f"{fmt(seg, a)}..{off:04X} ({off - a} байт) "
                  f"{blob[:16].hex(' ')}")


def cmd_xref(addr):
    funcs = scan()
    for c in sorted(callers(funcs).get(addr, ())):
        print(fmt(*c))


def annotate(i, seg, syms, data):
    s = ""
    ft = far_target(i)
    if ft:
        s = syms.get(ft, "")
    elif i.id == X.X86_INS_CALL and i.operands[0].type == X.X86_OP_IMM:
        s = syms.get((seg, i.operands[0].imm & 0xFFFF), "")
    notes = [s] if s else []
    for o in i.operands:
        if o.type != X.X86_OP_MEM:
            continue
        disp = o.mem.disp & 0xFFFF
        seg_r = o.mem.segment
        base = o.mem.base
        if base == 0 and o.mem.index == 0 and seg_r in (0, X.X86_REG_DS):
            nm = data.get(disp)
            if nm is None and "__vars__" in data:
                v = ds_name_of(disp, data["__vars__"])
                nm = None if v == "?" else v
            if nm is None:
                for k in range(1, 4):
                    if disp - k in data:
                        nm = f"{data[disp - k]}+{k}"
                        break
            notes.append(f"{nm or 'DS'}[{disp:04X}]")
        elif seg_r in (0, X.X86_REG_DS) and disp >= 0x100 and "__vars__" in data \
                and base not in (X.X86_REG_BP,):
            v = ds_name_of(disp, data["__vars__"])
            if v != "?":
                notes.append(f"&{v}[{disp:04X}]+idx")
        elif seg_r == X.X86_REG_ES and base == X.X86_REG_BX and disp:
            fn = field_name(disp)
            if fn:
                notes.append("obj." + fn)
    return "  ; " + ", ".join(notes) if notes else ""


def cmd_list(addr):
    seg, off = addr
    syms, data = load_symbols()
    ins, near, far, tables, issues = walk(seg, off, seg_limit(seg))
    labels = set()
    for i in ins.values():
        if (i.id in COND_JMP or i.id == X.X86_INS_JMP) and \
                i.operands[0].type == X.X86_OP_IMM:
            labels.add(i.operands[0].imm & 0xFFFF)
    for tg in tables.values():
        labels.update(tg)
    name = syms.get(addr, "")
    print(f"; {fmt(seg, off)}  file {fileoff(seg, off):05X}  {name}")
    for t, tg in sorted(tables.items()):
        print(f"; table {t:04X}: " + " ".join(f"{k}:{x:04X}" for k, x in enumerate(tg)))
    for s in issues:
        print("; !", s)
    prev = None
    for o in sorted(ins):
        i = ins[o]
        if prev is not None and o != prev:
            print("        ; ...")
        if o in labels:
            print(f"L{o:04X}:")
        print(f"  {o:04X}  {i.bytes.hex():<14} {i.mnemonic:<6} {i.op_str}"
              f"{annotate(i, seg, syms, data)}")
        prev = o + i.size


def main():
    a = sys.argv[1:]
    if not a or a[0] == "scan":
        cmd_scan()
    elif a[0] == "list":
        for x in a[1:]:
            cmd_list(parse_addr(x))
    elif a[0] == "gaps":
        cmd_gaps(int(a[1], 16) if len(a) > 1 else None)
    elif a[0] == "xref":
        cmd_xref(parse_addr(a[1]))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
