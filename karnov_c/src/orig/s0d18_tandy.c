/* Модуль 0x0D18 (file 0xE980) — преобразования в формат Tandy (4 бита
 * на пиксель, 2 пикселя в байте) из 4 плоскостей EGA. */
#include "orig/fn.h"

/* @0D18:000D file:0E98D tandy_convert — тайл 8×20: 4 плоскости по 20
 * байт → 80 байт (с конца); бит i плоскостей → младшая тетрада */
void tandy_convert(u8 *dst, const u8 *src)
{
    s16 di, k = 0x4F, c, b;
    u8 p[4], dl;
    for (di = 0x13; di >= 0; di--) {
        p[0] = src[di];
        p[1] = src[di + 0x14];
        p[2] = src[di + 0x28];
        p[3] = src[di + 0x3C];
        for (c = 0; c < 4; c++) {
            dl = 0;
            for (b = 0; b < 2; b++) {           /* ror + rcr dl: младший бит первым */
                s16 j;
                for (j = 0; j < 4; j++) {
                    dl = (u8)(dl >> 1 | (p[j] & 1) << 7);
                    p[j] = (u8)(p[j] >> 1 | p[j] << 7);
                }
            }
            dst[k--] = dl;
        }
    }
}

/* маска: 2 бита → 2 тетрады (бит 1 — тетрада 0, бит 0 — 0xF) */
static u8 mask2(u8 *m)
{
    u8 hi = *m & 0x80 ? 0xF0 : 0, lo = *m & 0x40 ? 0x0F : 0;
    *m = (u8)(*m << 2);
    return (u8)(hi | lo);
}

/* цвет: по 2 бита плоскостей c4 c3 c2 c1 старшими первыми */
static u8 color2(u8 c[4])
{
    u8 al = 0;
    s16 b, j;
    for (b = 0; b < 2; b++)
        for (j = 3; j >= 0; j--) {
            al = (u8)(al << 1 | c[j] >> 7);
            c[j] = (u8)(c[j] << 1);
        }
    return al;
}

/* @0D18:009D file:0EA1D tandy_sprite_convert — спрайт EGA (h, w в
 * словах, маска и 4 плоскости) → пары (маска, цвет) Tandy; размер */
u16 tandy_sprite_convert(u8 *dst, const u8 *src)
{
    u16 h = src[0], w = src[1], S = (u16)(w * 2 * h), o = 2, k;
    u16 pm = 2, p1 = (u16)(2 + S), p2 = (u16)(2 + 2 * S), p3 = (u16)(2 + 3 * S), p4 = (u16)(2 + 4 * S);
    u8 m, c[4];
    dst[0] = (u8)h;
    dst[1] = (u8)(w << 2);
    for (k = 0; k < h * w * 2; k++) {
        m = src[pm++];
        c[0] = src[p1++];
        c[1] = src[p2++];
        c[2] = src[p3++];
        c[3] = src[p4++];
        dst[o++] = mask2(&m);
        dst[o++] = mask2(&m);
        dst[o++] = color2(c);
        dst[o++] = color2(c);
        dst[o++] = mask2(&m);
        dst[o++] = mask2(&m);
        dst[o++] = color2(c);
        dst[o++] = color2(c);
    }
    return o;
}
