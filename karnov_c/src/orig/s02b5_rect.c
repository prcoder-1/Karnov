/* Модуль 0x02B5 (file 0x4350) — прямоугольники {x0, y0, x1, y1}. */
#include "orig/fn.h"

/* @02B5:0001 file:04351 rect_move_inside — сдвинуть a внутрь b */
void rect_move_inside(s16 *a, const s16 *b)
{
    s16 dx = b[0] - a[0], dy = b[1] - a[1];
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    a[0] += dx; a[1] += dy; a[2] += dx; a[3] += dy;
    dx = b[2] - a[2];
    dy = b[3] - a[3];
    if (dx > 0) dx = 0;
    if (dy > 0) dy = 0;
    a[0] += dx; a[1] += dy; a[2] += dx; a[3] += dy;
}

/* @02B5:009D file:043ED rect_intersect */
void rect_intersect(const s16 *a, const s16 *b, s16 *out)
{
    out[0] = a[0] > b[0] ? a[0] : b[0];
    out[1] = a[1] > b[1] ? a[1] : b[1];
    out[2] = a[2] < b[2] ? a[2] : b[2];
    out[3] = a[3] < b[3] ? a[3] : b[3];
}
