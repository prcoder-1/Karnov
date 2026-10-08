; Хук начала кадра для DSDUMP.EXE: кладётся в 1851:0000 (нулевая область
; перед эмулятором 8087, 200 байт), вызывается far-call'ом из главного
; цикла 04E4:0350 вместо двух inc. Каждый кадр: DS (64 КБ) → DSDUMP.BIN,
; 0000:0000..1A30 (векторы, BIOS, DOS, PSP) → LOW.BIN; в первых hcount
; кадрах после первых hskip — far-куча (2DE3:0000, 0x50000 байт) → HEAP.BIN.
%ifndef HSEG
%define HSEG 0x2de3
%endif
%ifndef HSKIP
%define HSKIP 0
%endif
%ifndef HCOUNT
%define HCOUNT 150
%endif
        bits 16
        org 0
hook:
        inc word [0xcdea]
        inc word [0xcdd8]
        pushf
        push ax
        push bx
        push cx
        push dx
        push si
        push ds
        mov si, fds
        call open                       ; bx = дескриптор
        jc .out
        xor dx, dx
        mov cx, 0x8000
        call wr
        mov dx, cx
        call wr
        mov si, flow
        call open
        jc .out
        push ds
        xor dx, dx
        mov ds, dx
        mov cx, 0x1a30
        call wr
        pop ds
        cmp word [cs:hskip], 0
        je .h
        dec word [cs:hskip]
        jmp .out
.h:
        cmp word [cs:hcount], 0
        je .out
        dec word [cs:hcount]
        mov si, fheap
        call open
        jc .out
        mov ax, HSEG
.lp:
        mov ds, ax
        xor dx, dx
        mov cx, 0x8000
        call wr
        add ax, 0x800
        cmp ax, HSEG + 0x5000
        jb .lp
.out:
        pop ds
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        popf
        retf
wr:                                     ; ds:dx, cx байт в bx; ax сохраняется
        push ax
        mov ah, 0x40
        int 0x21
        pop ax
        ret
open:                                   ; cs:si — {дескриптор, имя}; CF при ошибке
        mov bx, [cs:si]
        or bx, bx
        jnz .ok
        push ds
        push cs
        pop ds
        lea dx, [si + 2]
        xor cx, cx
        mov ah, 0x3c
        int 0x21
        pop ds
        jc .ret
        mov bx, ax
        mov [cs:si], bx
.ok:
        clc
.ret:
        ret
hskip:  dw HSKIP
hcount: dw HCOUNT
fds:    dw 0
        db "DSDUMP.BIN", 0
flow:   dw 0
        db "LOW.BIN", 0
fheap:  dw 0
        db "HEAP.BIN", 0
