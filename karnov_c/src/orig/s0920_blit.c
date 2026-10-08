/* Модуль 0x0920 (file 0xAA00) — asm-драйверы спрайтов и HUD.
 *
 * Оригинал — развёрнутые построчные процедуры (по одной на ширину,
 * таблицы в CS) и цикл «h раз вызвать строку» (0920:12A8). Здесь те же
 * операции циклами, побайтово, со смещениями по модулю 64 КБ внутри
 * сегмента, как у lodsw/stosw/rep movs. Ветви «word» и «byte» оригинала
 * (выбор по cpu_type, чтобы не пересечь границу сегмента словом) дают
 * одинаковый результат.
 *
 * Буфер vbuf — в эмулированной памяти (hw_conv), сегмент video_seg:
 * CGA — страницы +0 (фон) и +0x8000 (кадр); EGA — 4 плоскости через
 * 0x8000 (2-я пара в сегменте +0x1000), кадр со смещением +0x4000;
 * Tandy — фон в сегменте +0x1000, кадр в +0. */
#include "orig/fn.h"
#include "orig/hw.h"
#include "orig/todo.h"

static u8 *mem(u16 seg, u16 off) { return &hw_conv[((u32)seg * 16 + off) & 0xFFFFF]; }

static u16 vseg(void) { return (u16)((u32)(vbuf - hw_conv) >> 4); }

/* far-указатель спрайта n из таблицы арены (lds si, [sprite_dir]; lds si, [si+n*4]) */
static void spr_ptr(s16 n, u16 *seg, u16 *off)
{
    u16 aseg = hw_heap_seg(sprite_arena);
    u16 aoff = (u16)((u32)(sprite_arena - hw_conv) - (u32)aseg * 16);
    u16 t = (u16)(aoff + n * 4);
    *off = (u16)(mem(aseg, t)[0] | mem(aseg, (u16)(t + 1))[0] << 8);
    *seg = (u16)(mem(aseg, (u16)(t + 2))[0] | mem(aseg, (u16)(t + 3))[0] << 8);
}

static u16 rd16(u16 seg, u16 off)
{
    return (u16)(mem(seg, off)[0] | mem(seg, (u16)(off + 1))[0] << 8);
}

/* фаза CGA-спрайта: при phase — si += [si]; затем пропуск слова смещения */
static u16 cga_phase(u16 seg, u16 si, s16 phase)
{
    if (phase != 0)
        si = (u16)(si + rd16(seg, si));
    return (u16)(si + 2);
}

/* фаза EGA-спрайта: si += [si + ((phase-1)&3)*2]; затем 3 слова смещений */
static u16 ega_phase(u16 seg, u16 si, s16 phase)
{
    if (phase != 0)
        si = (u16)(si + rd16(seg, (u16)(si + ((phase - 1) & 3) * 2)));
    return (u16)(si + 6);
}

/* строки: d = (d & m) | c — пары слов (маска, цвет) */
static void rows_andor(u16 ds, u16 *si, u16 es, u16 di, s16 w, s16 h, u16 stride)
{
    s16 r, k;
    if (h >= 0x51)                      /* 0920:12A8: не больше 80 строк */
        return;
    for (r = 0; r < h; r++, di = (u16)(di + stride))
        for (k = 0; k < w; k++) {
            u8 *d0 = mem(es, (u16)(di + k * 2)), *d1 = mem(es, (u16)(di + k * 2 + 1));
            *d0 = (u8)((*d0 & *mem(ds, *si)) | *mem(ds, (u16)(*si + 2)));
            *d1 = (u8)((*d1 & *mem(ds, (u16)(*si + 1))) | *mem(ds, (u16)(*si + 3)));
            *si = (u16)(*si + 4);
        }
}

/* строки: d &= s (and = 1) или d |= s */
static void rows_op(u16 ds, u16 *si, u16 es, u16 di, s16 w, s16 h, u16 stride, int and)
{
    s16 r, k;
    if (h >= 0x51)                      /* 0920:12A8: не больше 80 строк */
        return;
    for (r = 0; r < h; r++, di = (u16)(di + stride))
        for (k = 0; k < w * 2; k++, *si = (u16)(*si + 1)) {
            u8 *d = mem(es, (u16)(di + k));
            *d = and ? (u8)(*d & *mem(ds, *si)) : (u8)(*d | *mem(ds, *si));
        }
}

/* строки rep movsb: n байт, шаг stride у обоих указателей */
static void rows_copy(u16 ds, u16 si, u16 es, u16 di, u16 n, s16 h, u16 stride)
{
    s16 r;
    u16 k;
    for (r = 0; r < h; r++, si = (u16)(si + stride), di = (u16)(di + stride))
        for (k = 0; k < n; k++)
            *mem(es, (u16)(di + k)) = *mem(ds, (u16)(si + k));
}

/* @0920:0002 file:0AA02 sprite_blit — спрайт в кадр буфера (mode 0) или
 * в фон (mode 1) */
void sprite_blit(u16 off, s16 sprite, s16 phase, s16 mode)
{
    u16 seg, si, di, v = vseg(), mask_si, es;
    s16 h, w, p;
    switch (level_mode) {
    case 0: case 2:
        di = (u16)(off + (mode == 0 ? 0x8000 : 0));
        spr_ptr(sprite, &seg, &si);
        si = cga_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        si = (u16)(si + 2);
        if (w >= 0x14)
            return;
        rows_andor(seg, &si, v, di, w, h, (u16)vb_row_bytes);
        break;
    case 1:
        di = (u16)(off + (mode == 0 ? 0x4000 : 0));
        spr_ptr(sprite, &seg, &si);
        si = ega_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        si = (u16)(si + 2);
        if (w >= 0x14)
            return;
        mask_si = si;
        for (p = 0; p < 4; p++) {           /* маска во все 4 плоскости */
            si = mask_si;
            es = (u16)(v + (p >= 2 ? 0x1000 : 0));
            rows_op(seg, &si, es, (u16)(di + p * 0x8000), w, h, (u16)vb_row_bytes, 1);
        }
        for (p = 0; p < 4; p++) {           /* цвет: плоскости подряд */
            es = (u16)(v + (p >= 2 ? 0x1000 : 0));
            rows_op(seg, &si, es, (u16)(di + p * 0x8000), w, h, (u16)vb_row_bytes, 0);
        }
        break;
    case 3:
        di = off;
        es = (u16)(v + (mode != 0 ? 0x1000 : 0));
        spr_ptr(sprite, &seg, &si);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        si = (u16)(si + 2);
        if (w >= 0x50)
            return;
        rows_andor(seg, &si, es, di, w, h, (u16)vb_row_bytes);
        break;
    }
}

/* @0920:13FE file:0BDFE sprite_erase — вернуть фон под спрайтом в кадр */
void sprite_erase(u16 off, s16 sprite, s16 phase)
{
    u16 seg, si, di, v = vseg();
    s16 h, w, p;
    switch (level_mode) {
    case 0: case 2:
        di = (u16)(off + 0x8000);
        spr_ptr(sprite, &seg, &si);
        si = cga_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        if (h >= 0x12C)
            return;
        rows_copy(v, (u16)(di + 0x8000), v, di, (u16)(w * 2), h, (u16)vb_row_bytes);
        break;
    case 1:
        di = (u16)(off + 0x4000);
        spr_ptr(sprite, &seg, &si);
        si = ega_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1)) + 1;   /* сдвинутая копия шире на слово */
        if (h >= 0x12C)
            return;
        for (p = 0; p < 4; p++) {
            u16 s = (u16)(v + (p >= 2 ? 0x1000 : 0));
            rows_copy(s, (u16)(di - 0x4000 + p * 0x8000), s, (u16)(di + p * 0x8000),
                      (u16)(w * 2), h, (u16)vb_row_bytes);
        }
        break;
    case 3:
        di = off;
        spr_ptr(sprite, &seg, &si);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        if (h >= 0x12C)
            return;
        rows_copy((u16)(v + 0x1000), di, v, di, (u16)(w * 2), h, (u16)vb_row_bytes);
        break;
    }
}

/* @0920:2392 file:0CD92 vb_present_rect — фон прямоугольника (w байт,
 * округление до слова) в кадр */
void vb_present_rect(u16 off, s16 w, s16 h)
{
    u16 v = vseg(), n = (u16)((w + 1) & ~1);
    s16 p;
    if (h >= 0x12C)
        return;
    switch (level_mode) {
    case 0: case 2:
        rows_copy(v, off, v, (u16)(off + 0x8000), n, h, (u16)vb_row_bytes);
        break;
    case 1:
        for (p = 0; p < 4; p++) {
            u16 s = (u16)(v + (p >= 2 ? 0x1000 : 0));
            rows_copy(s, (u16)(off + p * 0x8000), s, (u16)(off + 0x4000 + p * 0x8000), n, h,
                      (u16)vb_row_bytes);
        }
        break;
    case 3:
        rows_copy((u16)(v + 0x1000), off, v, off, n, h, (u16)vb_row_bytes);
        break;
    }
}

/* Строка HUD в видеопамять CGA/Tandy/Hercules: пропустить маску, слово
 * цвета — в память (или 0 при стирании); конец строки — переход по
 * банкам чередования, как в оригинале. */
enum { HUD_CGA, HUD_TANDY, HUD_HERC };

static void hud_rows(int kind, u16 ds, u16 si, u16 di, s16 w, s16 h, int erase)
{
    u16 cx = (u16)(w * 2), k;
    s16 r;
    if (h >= 0x51)                      /* 0920:12A8: не больше 80 строк */
        return;
    for (r = 0; r < h; r++) {
        int again;
        do {
            again = 0;
            for (k = 0; k < cx; k += 2) {
                if (erase) {
                    hw_vmem_write(0xB800, (u16)(di + k), 0);
                    hw_vmem_write(0xB800, (u16)(di + k + 1), 0);
                } else {
                    si = (u16)(si + 2);                         /* lodsw: маска */
                    hw_vmem_write(0xB800, (u16)(di + k), *mem(ds, si));
                    hw_vmem_write(0xB800, (u16)(di + k + 1), *mem(ds, (u16)(si + 1)));
                    si = (u16)(si + 2);
                }
            }
            switch (kind) {
            case HUD_CGA:
                di = di < 0x2000 ? (u16)(di + 0x2000) : (u16)(di - 0x1FB0);
                break;
            case HUD_TANDY:
                di = di < 0x6000 ? (u16)(di + 0x2000) : (u16)(di - 0x5F60);
                break;
            case HUD_HERC:
                if (di >= 0x4000) {
                    di = (u16)(di - 0x3FB0);
                } else if (di >= 0x2000) {          /* строка ещё раз в банк +2 */
                    if (!erase)
                        si = (u16)(si - cx * 2);
                    di = (u16)(di + 0x2000);
                    again = 1;
                } else {
                    di = (u16)(di + 0x2000);
                }
                break;
            }
        } while (again);
    }
}

/* @0920:24C8 file:0CEC8 hud_blit — спрайт панели прямо в видеопамять */
void hud_blit(u16 off, s16 sprite, s16 phase)
{
    u16 seg, si, k;
    s16 h, w, r, p;
    switch (level_mode) {
    case 0: case 2:
        spr_ptr(sprite, &seg, &si);
        si = cga_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        si = (u16)(si + 2);
        if (w >= 0x14)
            return;
        hud_rows(level_mode == 0 ? HUD_CGA : HUD_HERC, seg, si, off, w, h, 0);
        break;
    case 1:
        hw_out(0x3CE, 5);
        hw_out(0x3CF, 0);
        spr_ptr(sprite, &seg, &si);
        si = ega_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        si = (u16)(si + 2);
        if (w >= 0x14 || h >= 0x51)
            return;
        si = (u16)(si + h * w * 2);         /* плоскость маски пропускается */
        for (p = 0; p < 4; p++) {
            u16 di = off;
            hw_out(0x3C4, 2);
            hw_out(0x3C5, (u8)(1 << p));
            for (r = 0; r < h; r++, di = (u16)(di + 0x28))
                for (k = 0; k < (u16)(w * 2); k++, si = (u16)(si + 1))
                    hw_vmem_write(0xA000, (u16)(di + k), *mem(seg, si));
        }
        break;
    case 3:
        spr_ptr(sprite, &seg, &si);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        si = (u16)(si + 2);
        if (w >= 0x50)
            return;
        hud_rows(HUD_TANDY, seg, si, off, w, h, 0);
        break;
    }
}

/* @0920:2A56 file:0D456 hud_erase — обнулить место спрайта панели */
void hud_erase(u16 off, s16 sprite, s16 phase)
{
    u16 seg, si, k, di;
    s16 h, w, r;
    switch (level_mode) {
    case 0: case 2:
        spr_ptr(sprite, &seg, &si);
        si = cga_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        if (w >= 0x14)
            return;
        hud_rows(level_mode == 0 ? HUD_CGA : HUD_HERC, seg, si, off, w, h, 1);
        break;
    case 1:
        spr_ptr(sprite, &seg, &si);
        si = ega_phase(seg, si, phase);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        if (w >= 0x14)
            return;
        hw_out(0x3C4, 2);
        hw_out(0x3C5, 0xF);
        if (h >= 0x51)
            return;
        for (r = 0, di = off; r < h; r++, di = (u16)(di + 0x28))
            for (k = 0; k < (u16)(w * 2); k++)
                hw_vmem_write(0xA000, (u16)(di + k), 0);
        break;
    case 3:
        spr_ptr(sprite, &seg, &si);
        h = *mem(seg, si);
        w = *mem(seg, (u16)(si + 1));
        if (w >= 0x50)
            return;
        hud_rows(HUD_TANDY, seg, si, off, w, h, 1);
        break;
    }
}
