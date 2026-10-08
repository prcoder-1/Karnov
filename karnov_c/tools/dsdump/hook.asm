; Хук начала кадра для DSDUMP.EXE: кладётся в 1851:0000 (нулевая область
; перед эмулятором 8087), вызывается far-call'ом из главного цикла
; 04E4:0350 вместо двух inc. Пишет весь DS (64 КБ) в DSDUMP.BIN и в
; первых HEAPN кадрах — far-кучу (2DE3:0000, 0x50000 байт) в HEAP.BIN.
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
        mov bx, [cs:handle]
        or bx, bx
        jnz .write
        push ds
        push cs
        pop ds
        mov dx, fname
        xor cx, cx
        mov ah, 0x3c
        int 0x21
        pop ds
        jc .out
        mov bx, ax
        mov [cs:handle], bx
.write:
        xor dx, dx
        mov cx, 0x8000
        mov ah, 0x40
        int 0x21
        mov dx, 0x8000
        mov cx, 0x8000
        mov ah, 0x40
        int 0x21
        call heap
.out:
        pop dx
        pop cx
        pop bx
        pop ax
        popf
        retf
heap:
        cmp word [cs:hcount], 0
        je .ret
        dec word [cs:hcount]
        mov bx, [cs:hhandle]
        or bx, bx
        jnz .w
        push ds
        push cs
        pop ds
        mov dx, hname
        xor cx, cx
        mov ah, 0x3c
        int 0x21
        pop ds
        jc .ret
        mov bx, ax
        mov [cs:hhandle], bx
.w:
        push ds
        push si
        mov si, 0x2de3
.lp:
        mov ds, si
        xor dx, dx
        mov cx, 0x8000
        mov ah, 0x40
        int 0x21
        add si, 0x800
        cmp si, 0x7de3
        jb .lp
        pop si
        pop ds
.ret:
        ret
handle: dw 0
hhandle: dw 0
hcount: dw 150
fname:  db "DSDUMP.BIN", 0
hname:  db "HEAP.BIN", 0
