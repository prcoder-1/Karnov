/* Модуль 0x0287 (file 0x4070) — тайл карты во внеэкранный буфер.
 *
 * Раскладка vbuf по режимам (вставки movsb оригинала):
 *   CGA/Hercules — 4 байта × 10 строк, шаг 0x90;
 *   Tandy        — 8 байт × 10 строк, шаг 0x120, во втором банке 64 КБ;
 *   EGA          — 4 плоскости по 2 байта × 10 строк, шаг 0x48; плоскости
 *                  через 0x8000 (смещение 16-битное, с переносом по кругу),
 *                  плоскости 2–3 — во втором банке 64 КБ. */
#include "orig/fn.h"

static void copy_rows(u8 *bank, u16 di, const u8 **src, s16 n, u16 step)
{
    s16 r, k;
    for (r = 0; r < 10; r++, di += step)
        for (k = 0; k < n; k++, di++)
            bank[di] = *(*src)++;
}

/* @0287:0006 file:04076 draw_tile */
void draw_tile(s16 col, s16 row)
{
    u8 t = MAP_RD(row, (u16)(col * 2));
    const u8 *src;
    u16 di = vb_trow_off[row] + vb_col_off[col];
    if (level_mode == 0 || level_mode == 2) {
        src = tile_gfx + (u16)(t * 40);
        copy_rows(vbuf, di, &src, 4, 0x8C);
    } else if (level_mode == 3) {
        src = tile_gfx + (u16)(t * 80);
        copy_rows(vbuf + 0x10000, di, &src, 8, 0x118);
    } else {
        src = tile_gfx + (u16)(t * 80);
        copy_rows(vbuf, di, &src, 2, 0x46);
        copy_rows(vbuf, (u16)(di + 0x8000), &src, 2, 0x46);
        copy_rows(vbuf + 0x10000, di, &src, 2, 0x46);
        copy_rows(vbuf + 0x10000, (u16)(di + 0x8000), &src, 2, 0x46);
    }
}
