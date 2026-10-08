#!/usr/bin/env python3
"""orig.py — выполнение кода KARNOV.EXE в unicorn на снимке памяти.

Образ EXE грузится с сегмента LOAD (как в DOSBox, 0x1A3) с релокациями;
DS (64 КБ) и far-куча (0x2DE30.., 0x50000) — из дампов. Вызов far-функции:
аргументы-слова в стек, возврат на заглушку. Прерывания: 16h — «нет
клавиш», 10h/21h — игнор; 8087-эмуляция (34h–3Dh) — исключение FPU."""
import re, os, struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_INSN, UcError
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.normpath(os.path.join(HERE, "../../../KARNOV.EXE"))
LOAD = 0x1A3
DS_SEG = 0x1D99 + LOAD
HEAP_LIN, HEAP_N = 0x2DE30, 0x50000
STUB_SEG = 0x9F00                  # far-возврат сюда = конец вызова
# стек как у оригинала в главном цикле: SS=2B3F (+загрузка), SP=0FCA
SS_SEG, SP0 = 0x2CE2, 0x0FCA


class FpuUsed(Exception):
    pass


def lowmem():
    """(память 0..1A2F, [(смещение в образе, байты)]) из src/orig/hw_lowmem.c"""
    t = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "../../src/orig/hw_lowmem.c")).read()
    low, patch = t.split("hw_code_patch")
    low = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2}),", low))
    patch = [(int(a, 16), bytes(int(x, 16) for x in b.split(",")))
             for a, b in re.findall(r"\{ 0x([0-9A-F]+), \d+, \{ ([^}]*) \} \}", patch)]
    return low, patch


class Orig:
    def __init__(self):
        d = open(EXE, "rb").read()
        hdr = struct.unpack_from("<H", d, 8)[0] * 16
        img = bytearray(d[hdr:])
        n, tab = struct.unpack_from("<H", d, 6)[0], struct.unpack_from("<H", d, 0x18)[0]
        for i in range(n):
            o, s = struct.unpack_from("<HH", d, tab + i * 4)
            a = s * 16 + o
            v, = struct.unpack_from("<H", img, a)
            struct.pack_into("<H", img, a, (v + LOAD) & 0xFFFF)
        self.img = bytes(img)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x100000)
        self.uc.hook_add(UC_HOOK_INTR, self._intr)
        self.uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        self.calls = []

    def _in(self, uc, port, size, ud):
        return 0

    def _out(self, uc, port, size, value, ud):
        pass

    def _intr(self, uc, intno, ud):
        if 0x34 <= intno <= 0x3D:
            raise FpuUsed()
        if intno == 0x16:                       # bioskey: нет клавиш
            uc.reg_write(UC_X86_REG_AX, 0)
            fl = uc.reg_read(UC_X86_REG_EFLAGS)
            uc.reg_write(UC_X86_REG_EFLAGS, fl | 0x40)   # ZF=1
        # прочие (10h, 21h, 1Ah) — без эффекта

    def load(self, ds_img, heap_img=None):
        uc = self.uc
        uc.mem_write(0, b"\0" * 0x100000)
        uc.mem_write(LOAD * 16, self.img)
        # векторы и данные BIOS — тот же снимок DOSBox, что у перевода
        low, patch = lowmem()
        uc.mem_write(0, low)
        for a, b in patch:
            uc.mem_write(LOAD * 16 + a, b)
        uc.mem_write(DS_SEG * 16, bytes(ds_img))
        if heap_img is not None:
            uc.mem_write(HEAP_LIN, bytes(heap_img))
        uc.mem_write(STUB_SEG * 16, b"\xf4")     # hlt

    def call(self, seg, off, args=(), limit=50_000_000):
        """far-вызов SEG:OFF (сегмент без релокации), args — слова слева направо"""
        uc = self.uc
        sp = SP0
        for a in reversed(args):
            sp -= 2
            uc.mem_write(SS_SEG * 16 + sp, struct.pack("<H", a & 0xFFFF))
        sp -= 4
        uc.mem_write(SS_SEG * 16 + sp, struct.pack("<HH", 0, STUB_SEG))
        for r, v in ((UC_X86_REG_SS, SS_SEG), (UC_X86_REG_SP, sp), (UC_X86_REG_BP, 0),
                     (UC_X86_REG_DS, DS_SEG), (UC_X86_REG_ES, DS_SEG),
                     (UC_X86_REG_CS, seg + LOAD), (UC_X86_REG_IP, off)):
            uc.reg_write(r, v)
        uc.emu_start(((seg + LOAD) * 16 + off), STUB_SEG * 16, count=limit)
        return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX)

    def ds(self):
        return bytes(self.uc.mem_read(DS_SEG * 16, 0x10000))

    def heap(self):
        return bytes(self.uc.mem_read(HEAP_LIN, HEAP_N))
