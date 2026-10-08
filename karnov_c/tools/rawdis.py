#!/usr/bin/env python3
"""rawdis.py SEG A-B ... — сырая дизассемблировка диапазонов сегмента (для
развёрнутого asm-кода, не видимого x86map)."""
import sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
d=open('/home/prcoder/claude-experiments/Karnov/KARNOV.EXE','rb').read()
md=Cs(CS_ARCH_X86, CS_MODE_16)
seg=int(sys.argv[1],16)
base=0x1800+seg*16
for r in sys.argv[2:]:
    a,b=[int(x,16) for x in r.split('-')]
    print('---',r)
    for i in md.disasm(d[base+a:base+b], a):
        print('%04X  %-12s %-6s %s'%(i.address,i.bytes.hex(),i.mnemonic,i.op_str))
