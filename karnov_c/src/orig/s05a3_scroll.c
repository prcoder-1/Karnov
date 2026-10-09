/* Модуль 0x05A3 (file 0x7230) — scroll_blit: кадр буфера (120 строк от
 * смещения src) на экран. Оригинал полностью развёрнут; ветки «word» и
 * «byte» (cpu_type) равны по результату. */
#include "orig/fn.h"
#include "orig/hw.h"

/* n байт из банка буфера bank:si в видеопамять vseg:di (смещения по модулю 64К) */
static void copy_out(u16 bank, u16 si, u16 vseg, u16 di, u16 n)
{
    u16 k;
    for (k = 0; k < n; k++)
        hw_vmem_write(vseg, (u16)(di + k), vbuf[(u32)bank << 16 | (u16)(si + k)]);
}

/* @05A3:0009 file:07239 scroll_blit */
void scroll_blit(u16 src)
{
    u16 si = src, di, p, mask;
    s16 r;
    switch (level_mode) {
    case 0:                     /* CGA: строки поочерёдно в банки 0/2000 */
        for (r = 0, di = 0x1E0; r < 0x3C; r++) {
            copy_out(0, si, 0xB800, di, 0x50);
            si = (u16)(si + 0x90);
            copy_out(0, si, 0xB800, (u16)(di + 0x2000), 0x50);
            si = (u16)(si + 0x90);
            di = (u16)(di + 0x50);
        }
        break;
    case 2:                     /* Hercules: нечётная строка — ещё и в банк +4000 */
        for (r = 0, di = 0x1E0; r < 0x3C; r++) {
            copy_out(0, si, 0xB800, di, 0x50);
            si = (u16)(si + 0x90);
            copy_out(0, si, 0xB800, (u16)(di + 0x2000), 0x50);
            copy_out(0, si, 0xB800, (u16)(di + 0x4000), 0x50);
            si = (u16)(si + 0x90);
            di = (u16)(di + 0x50);
        }
        break;
    case 3:                     /* Tandy: по 4 строки в банки через 2000 */
        for (r = 0, di = 0xA0; r < 0x1E; r++, di = (u16)(di + 0xA0)) {
            for (p = 0; p < 4; p++) {
                copy_out(0, si, 0xB800, (u16)(di + p * 0x2000), 0xA0);
                si = (u16)(si + 0x120);
            }
        }
        break;
    case 1:                     /* EGA: невидимая страница, плоскости по маске */
        hw_out(0x3CE, 5);
        hw_out(0x3CF, 0);
        ega_page = ega_page != 0 ? 0 : 0x1F40;
        for (p = 0, mask = 1; p < 4; p++, mask <<= 1) {
            u16 psi = (u16)(si + p * 0x8000);
            hw_out(0x3C4, 2);
            hw_out(0x3C5, (u8)mask);
            for (r = 0, di = (u16)(0xF0 + ega_page); r < 0x78; r++, di = (u16)(di + 0x28)) {
                copy_out((u16)(p >> 1), psi, 0xA000, di, 0x28);
                psi = (u16)(psi + 0x48);
            }
        }
        hw_out(0x3D4, 0xC);
        hw_out(0x3D5, (u8)(ega_page >> 8));
        hw_out(0x3D4, 0xD);
        hw_out(0x3D5, (u8)ega_page);
        break;
    }
}
