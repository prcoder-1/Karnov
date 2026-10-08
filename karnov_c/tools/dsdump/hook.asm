; Хук начала кадра для DSDUMP.EXE: кладётся в 1851:0000 (нулевая область
; перед эмулятором 8087), вызывается far-call'ом из главного цикла
; 04E4:0350 вместо двух inc. Пишет весь DS (64 КБ) в DSDUMP.BIN.
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
.out:
        pop dx
        pop cx
        pop bx
        pop ax
        popf
        retf
handle: dw 0
fname:  db "DSDUMP.BIN", 0
