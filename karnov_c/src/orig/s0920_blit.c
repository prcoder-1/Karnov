/* Модуль 0x0920 (file 0xAA00) — asm-драйверы спрайтов и HUD.
 *
 * Оригинал — развёрнутые построчные процедуры (по одной на ширину,
 * таблицы в CS) и цикл «h раз вызвать строку» (0920:12A8). Здесь те же
 * операции циклами, побайтово, со смещениями по модулю 64 КБ внутри
 * сегмента, как у lodsw/stosw/rep movs. Ветви «word» и «byte» оригинала
 * (выбор по cpu_type, чтобы не пересечь границу сегмента словом) дают
 * одинаковый результат.
 *
 * Буфер vbuf — два банка по 64 КБ, смещения в банке 16-битные:
 * CGA — страницы +0 (фон) и +0x8000 (кадр); EGA — 4 плоскости через
 * 0x8000 (2-я пара во втором банке), кадр со смещением +0x4000;
 * Tandy — фон во втором банке, кадр в первом. */
#include "orig/fn.h"
#include "orig/hw.h"

/* байт буфера: банк (0, 1) и 16-битное смещение в нём */
static u8 *vb(u16 bank, u16 off) { return &vbuf[(u32)bank << 16 | off]; }

static u16 rd16(const u8 *p, u16 off)
{
    return (u16)(p[off] | p[(u16)(off + 1)] << 8);
}

/* фаза CGA-спрайта: при phase — si += [si]; затем пропуск слова смещения */
static u16 cga_phase(const u8 *spr, u16 si, s16 phase)
{
    if (phase != 0)
        si = (u16)(si + rd16(spr, si));
    return (u16)(si + 2);
}

/* фаза EGA-спрайта: si += [si + ((phase-1)&3)*2]; затем 3 слова смещений */
static u16 ega_phase(const u8 *spr, u16 si, s16 phase)
{
    if (phase != 0)
        si = (u16)(si + rd16(spr, (u16)(si + ((phase - 1) & 3) * 2)));
    return (u16)(si + 6);
}

/* Заголовок спрайта: данные, смещение после фазы (у Tandy фазы нет),
 * высота и ширина в словах. */
static const u8 *spr_head(s16 sprite, s16 phase, u16 *si, s16 *h, s16 *w)
{
    const u8 *p = sprite_data(sprite);
    *si = level_mode == 1 ? ega_phase(p, 0, phase) : level_mode == 3 ? 0 : cga_phase(p, 0, phase);
    *h = p[*si];
    *w = p[(u16)(*si + 1)];
    return p;
}

/* плоскость EGA p в буфере: банк p / 2, смещение off + p * 0x8000 */
static u8 *vb_plane(s16 p, u16 off) { return vb((u16)(p >> 1), (u16)(off + p * 0x8000)); }

/* строки: d = (d & m) | c — пары слов (маска, цвет) */
static void rows_andor(const u8 *spr, u16 *si, u16 bank, u16 di, s16 w, s16 h)
{
    s16 r, k;
    if (h >= 0x51)                      /* 0920:12A8: не больше 80 строк */
        return;
    for (r = 0; r < h; r++, di = (u16)(di + vb_row_bytes))
        for (k = 0; k < w; k++) {
            u8 *d0 = vb(bank, (u16)(di + k * 2)), *d1 = vb(bank, (u16)(di + k * 2 + 1));
            *d0 = (u8)((*d0 & spr[*si]) | spr[(u16)(*si + 2)]);
            *d1 = (u8)((*d1 & spr[(u16)(*si + 1)]) | spr[(u16)(*si + 3)]);
            *si = (u16)(*si + 4);
        }
}

/* строки плоскости EGA p: d &= s (and = 1) или d |= s */
static void rows_op(const u8 *spr, u16 *si, s16 p, u16 di, s16 w, s16 h, int and)
{
    s16 r, k;
    if (h >= 0x51)                      /* 0920:12A8: не больше 80 строк */
        return;
    for (r = 0; r < h; r++, di = (u16)(di + vb_row_bytes))
        for (k = 0; k < w * 2; k++, *si = (u16)(*si + 1)) {
            u8 *d = vb_plane(p, (u16)(di + k));
            *d = and ? (u8)(*d & spr[*si]) : (u8)(*d | spr[*si]);
        }
}

/* строки rep movsb: n байт, шаг vb_row_bytes у обоих указателей */
static void rows_copy(u16 sbank, u16 si, u16 dbank, u16 di, u16 n, s16 h)
{
    s16 r;
    u16 k;
    for (r = 0; r < h; r++, si = (u16)(si + vb_row_bytes), di = (u16)(di + vb_row_bytes))
        for (k = 0; k < n; k++)
            *vb(dbank, (u16)(di + k)) = *vb(sbank, (u16)(si + k));
}

/* то же для всех плоскостей EGA */
static void planes_copy(u16 si, u16 di, u16 n, s16 h)
{
    s16 p;
    for (p = 0; p < 4; p++)
        rows_copy((u16)(p >> 1), (u16)(si + p * 0x8000), (u16)(p >> 1), (u16)(di + p * 0x8000), n, h);
}

/* @0920:0002 file:0AA02 sprite_blit — спрайт в кадр буфера (mode 0) или
 * в фон (mode 1) */
void sprite_blit(u16 off, s16 sprite, s16 phase, s16 mode)
{
    u16 si, mask_si;
    s16 h, w, p;
    const u8 *spr = spr_head(sprite, phase, &si, &h, &w);
    si = (u16)(si + 2);
    switch (level_mode) {
    case 0: case 2:
        if (w < 0x14)
            rows_andor(spr, &si, 0, (u16)(off + (mode == 0 ? 0x8000 : 0)), w, h);
        break;
    case 1:
        if (w >= 0x14)
            return;
        off = (u16)(off + (mode == 0 ? 0x4000 : 0));
        mask_si = si;
        for (p = 0; p < 4; p++) {           /* маска во все 4 плоскости */
            si = mask_si;
            rows_op(spr, &si, p, off, w, h, 1);
        }
        for (p = 0; p < 4; p++)             /* цвет: плоскости подряд */
            rows_op(spr, &si, p, off, w, h, 0);
        break;
    case 3:
        if (w < 0x50)
            rows_andor(spr, &si, mode != 0, off, w, h);
        break;
    }
}

/* @0920:13FE file:0BDFE sprite_erase — вернуть фон под спрайтом в кадр */
void sprite_erase(u16 off, s16 sprite, s16 phase)
{
    u16 si;
    s16 h, w;
    spr_head(sprite, phase, &si, &h, &w);
    if (h >= 0x12C)
        return;
    switch (level_mode) {
    case 0: case 2:
        rows_copy(0, off, 0, (u16)(off + 0x8000), (u16)(w * 2), h);
        break;
    case 1:                             /* сдвинутая копия шире на слово */
        planes_copy(off, (u16)(off + 0x4000), (u16)((w + 1) * 2), h);
        break;
    case 3:
        rows_copy(1, off, 0, off, (u16)(w * 2), h);
        break;
    }
}

/* @0920:2392 file:0CD92 vb_present_rect — фон прямоугольника (w байт,
 * округление до слова) в кадр */
void vb_present_rect(u16 off, s16 w, s16 h)
{
    u16 n = (u16)((w + 1) & ~1);
    if (h >= 0x12C)
        return;
    switch (level_mode) {
    case 0: case 2:
        rows_copy(0, off, 0, (u16)(off + 0x8000), n, h);
        break;
    case 1:
        planes_copy(off, (u16)(off + 0x4000), n, h);
        break;
    case 3:
        rows_copy(1, off, 0, off, n, h);
        break;
    }
}

/* Строка HUD в видеопамять CGA/Tandy/Hercules: пропустить маску, слово
 * цвета — в память (или 0 при стирании); конец строки — переход по
 * банкам чередования, как в оригинале. */
static void hud_rows(const u8 *spr, u16 si, u16 di, s16 w, s16 h, int erase)
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
                    hw_vmem_write(0xB800, (u16)(di + k), spr[si]);
                    hw_vmem_write(0xB800, (u16)(di + k + 1), spr[(u16)(si + 1)]);
                    si = (u16)(si + 2);
                }
            }
            switch (level_mode) {
            case 0:
                di = di < 0x2000 ? (u16)(di + 0x2000) : (u16)(di - 0x1FB0);
                break;
            case 3:
                di = di < 0x6000 ? (u16)(di + 0x2000) : (u16)(di - 0x5F60);
                break;
            case 2:
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
    u16 si, k, di;
    s16 h, w, r, p;
    const u8 *spr;
    if (level_mode == 1) {
        hw_out(0x3CE, 5);
        hw_out(0x3CF, 0);
    }
    spr = spr_head(sprite, phase, &si, &h, &w);
    si = (u16)(si + 2);
    if (w >= (level_mode == 3 ? 0x50 : 0x14))
        return;
    if (level_mode != 1) {
        hud_rows(spr, si, off, w, h, 0);
        return;
    }
    if (h >= 0x51)
        return;
    si = (u16)(si + h * w * 2);         /* плоскость маски пропускается */
    for (p = 0; p < 4; p++) {
        hw_out(0x3C4, 2);
        hw_out(0x3C5, (u8)(1 << p));
        for (r = 0, di = off; r < h; r++, di = (u16)(di + 0x28))
            for (k = 0; k < (u16)(w * 2); k++, si = (u16)(si + 1))
                hw_vmem_write(0xA000, (u16)(di + k), spr[si]);
    }
}

/* @0920:2A56 file:0D456 hud_erase — обнулить место спрайта панели */
void hud_erase(u16 off, s16 sprite, s16 phase)
{
    u16 si, k, di;
    s16 h, w, r;
    const u8 *spr = spr_head(sprite, phase, &si, &h, &w);
    if (w >= (level_mode == 3 ? 0x50 : 0x14))
        return;
    if (level_mode != 1) {
        hud_rows(spr, si, off, w, h, 1);
        return;
    }
    hw_out(0x3C4, 2);
    hw_out(0x3C5, 0xF);
    if (h >= 0x51)
        return;
    for (r = 0, di = off; r < h; r++, di = (u16)(di + 0x28))
        for (k = 0; k < (u16)(w * 2); k++)
            hw_vmem_write(0xA000, (u16)(di + k), 0);
}
