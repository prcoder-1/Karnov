/* Модуль 0x0BFB (file 0xD7B0) — загрузка спрайтов SPRITES.EGA/CGA в арену.
 *
 * Арена (sprite_arena, куча): таблица из count far-указателей на
 * спрайты, затем данные. Здесь вместо far-указателя в таблицу пишется
 * смещение от начала арены (4 байта, как в оригинале). Слова данных
 * спрайтов — big-endian.
 *
 * EGA-спрайт: 3 слова смещений сдвинутых копий, заголовок (h, w в
 * словах), 5 плоскостей (маска + 4 цвета) по h строк × w слов; за ним
 * копия, сдвинутая на 4 бита вправо (ega_transform). */
#include <stdio.h>
#include "orig/fn.h"
#include "orig/todo.h"

static u16 cga_transform(u8 *buf, u16 size);
static u16 ega_transform(u8 *buf, u16 size, s16 wide);

static u16 rd_be(const u8 *b, u16 word)
{
    return (u16)(b[word * 2] << 8 | b[word * 2 + 1]);
}

static void wr_be(u8 *b, u16 word, u16 v)
{
    b[word * 2] = (u8)(v >> 8);
    b[word * 2 + 1] = (u8)v;
}

static void wr_le(u8 *b, u16 word, u16 v)
{
    b[word * 2] = (u8)v;
    b[word * 2 + 1] = (u8)(v >> 8);
}

/* @0BFB:0004 file:0D7B4 sprite_init — арена; ширины спрайтов → пары пикселей */
s16 sprite_init(void)
{
    u16 si;
    switch (level_mode) {
    case 0: case 2: sprite_arena = rtl_farmalloc(0x11000); break;
    case 1:         sprite_arena = rtl_farmalloc(0x1B800); break;
    case 3:         sprite_arena = rtl_farmalloc(0x20000); break;
    }
    if (level_mode <= 3 && sprite_arena == NULL)
        return 1;
    for (si = 0; si < 0x152; si++)
        sprite_w[si] = (u8)(sprite_w[si] / 2);
    return 0;
}

/* @0BFB:00B2 file:0D862 sprite_shutdown */
void sprite_shutdown(void)
{
    if (sprite_arena != NULL)
        rtl_farfree(sprite_arena);
}

/* @0BFB:00D9 file:0D889 sprite_needed — типы 0/1 (игрок) или есть в триггерах */
static s16 sprite_needed(s16 type)
{
    s16 si;
    EnemySpawn *r;
    if (type == 0 || type == 1)
        return 1;
    for (si = 0, r = spawns; si < spawn_count; si++, r++)
        if (r->type == type)
            return 1;
    return 0;
}

/* @0BFB:011C file:0D8CC sprite_loader_main — коды ошибок как в оригинале */
s16 sprite_loader_main(void)
{
    s16 need_seek = 1, first_time, di;
    u16 count, si;
    const char *name = NULL;
    FILE *fp = NULL;
    u8 *cursor, *empty, *dir, *table;
    s32 total;

    first_time = sprites_fixed == -1 ? 1 : 0;
    switch (level_mode) {
    case 0: case 2: name = s_sprites_cga; break;
    case 1: case 3: name = s_sprites_ega_d; break;
    }
    while (fp == NULL) {
        fp = bin_fopen(name, s_rb_spr_a);
        if (fp != NULL)
            break;
        clear_screen();
        draw_text(s_insert_disk, 10);
        draw_text(s_any_key, 12);
        while (read_key() == 0)
            ;
        clear_screen();
        clear_arg(0);
    }
    bin_fclose(fp);
    fp = bin_fopen(name, s_rb_spr_b);
    if (fp == NULL)
        return 1;
    if (bin_fread((u8 *)&count, 2, 1, fp) != 1) {
        bin_fclose(fp);
        return 3;
    }
    if (bin_fread(vbuf, 8, count, fp) != count) {
        bin_fclose(fp);
        return 4;
    }
    total = (s32)count * 4;
    cursor = sprite_arena + total;
    empty = cursor;
    dir = vbuf;
    table = sprite_arena;
    for (di = 0; di < (s16)count; di++, dir += 8, table += 4) {
        if ((di & 0xF) == 0)
            DOCONTROLS();
        si = (u16)(dir[2] | dir[3] << 8);
        if (first_time == 0 && di <= sprites_fixed) {
            need_seek = 1;
            cursor = sprites_fixed_end;
            total = (s32)(cursor - sprite_arena);
            continue;
        }
        if (!sprite_needed(dir[0])) {
            wr_le(table, 0, (u16)(empty - sprite_arena));
            wr_le(table, 1, (u16)((u32)(empty - sprite_arena) >> 16));
            need_seek = 1;
            continue;
        }
        if (need_seek) {
            bin_reset();
            if (rtl_fseek(fp, (s32)(dir[4] | dir[5] << 8 | (u32)dir[6] << 16 |
                                    (u32)dir[7] << 24), 0) != 0)
                goto err5;
            need_seek = 0;
        }
        switch (level_mode) {
        case 0: case 2:
            if (bin_fread(cursor + 2, si, 1, fp) != 1)
                goto err5;
            if (dir[1] != 0) {
                si = cga_transform(cursor, si + 2);
            } else {
                cursor[0] = 0;
                cursor[1] = 0;
                si += 2;
            }
            break;
        case 1:
            if (bin_fread(cursor + 6, si, 1, fp) != 1)
                goto err5;
            if (dir[0] == 0 || dir[1] != 0) {
                si = ega_transform(cursor, si + 6, 1);
            } else {
                cursor[0] = 0; cursor[1] = 0; cursor[2] = 0;
                cursor[3] = 0; cursor[4] = 0; cursor[5] = 0;
                si += 6;
            }
            break;
        case 3:
            if (bin_fread(vbuf + 0x4000, si, 1, fp) != 1)
                goto err5;
            si = tandy_sprite_convert(cursor, vbuf + 0x4000);
            break;
        }
        wr_le(table, 0, (u16)(cursor - sprite_arena));
        wr_le(table, 1, (u16)((u32)(cursor - sprite_arena) >> 16));
        cursor += si;
        total += si;
        if (first_time && (dir[0] == 0 || dir[0] == 1)) {
            sprites_fixed = di;
            sprites_fixed_end = cursor;
        }
        switch (level_mode) {
        case 0: case 2:
            if (total > 0x11000) goto err6;
            break;
        case 1:
            if (total > 0x1B800) goto err6;
            break;
        case 3:
            if (total > 0x20000) goto err6;
            break;
        }
    }
    bin_fclose(fp);
    return 0;
err5:
    bin_fclose(fp);
    return 5;
err6:
    bin_fclose(fp);
    return 6;
}

/* @0BFB:05A3 file:0DD53 cga_transform — добавить копию, сдвинутую на 2
 * пикселя; слова (маска, пиксели) по столбцам. Возвращает размер или 0. */
static u16 cga_transform(u8 *buf, u16 size)
{
    u16 w = buf[3], h = buf[2], neww = w, extend = 0;
    u16 di, r, c, out, v, si, c0, c1;

    di = (w - 1) * 2 + 2;
    for (r = 0; r < h; r++) {
        if ((rd_be(buf, di) & 0xF) != 0xF) {
            extend = 1;
            neww++;
            break;
        }
        di += w * 2;
    }
    di = 2;
    out = w * 2 * h + di;
    if ((u16)(out * 2) != size)
        return 0;
    wr_le(buf, 0, (u16)((out - 1) * 2));
    wr_le(buf, out, (u16)((neww << 8) + h));
    out++;
    for (r = 0; r < h; r++) {
        c1 = 0;
        c0 = 0xFFFF;
        for (c = 0; c < w; c++) {
            v = rd_be(buf, di);
            c0 &= 0xF;
            c0 |= (v & 0xF) << 4;
            si = (v >> 4) & 0xFFF;
            si |= (c0 << 12) & 0xF000;
            c0 >>= 4;
            wr_be(buf, out, si);
            di++;
            out++;
            v = rd_be(buf, di);
            c1 &= 0xF;
            c1 |= (v & 0xF) << 4;
            si = (v >> 4) & 0xFFF;
            si |= (c1 << 12) & 0xF000;
            c1 >>= 4;
            wr_be(buf, out, si);
            di++;
            out++;
        }
        if (extend) {
            si = 0xFFF | ((c0 << 12) & 0xF000);
            wr_be(buf, out++, si);
            si = (c1 << 12) & 0xF000;
            wr_be(buf, out++, si);
        }
    }
    return (u16)(out * 2);
}

/* @0BFB:0812 file:0DFC2 ega_transform — дописать сдвинутые копии.
 * wide = 1: одна копия со сдвигом 4 бита; иначе три со сдвигом 2.
 * Строки маски сдвигаются с подстановкой единиц (прозрачно), цветов —
 * нулей. Возвращает размер в байтах или 0 при несовпадении размеров. */
static u16 ega_transform(u8 *buf, u16 size, s16 wide)
{
    u16 shift, mask_lo, mask_hi, copies, hdr, w, h, k, extend, neww;
    u16 idx, out, r, c, v, si, di;

    if (size & 1)
        return 0;
    size = (u16)((s16)size / 2);
    if (wide == 1) {
        shift = 4; mask_lo = 0xFFF0; mask_hi = 0x0FFF; copies = 1;
    } else {
        shift = 2; mask_lo = 0xFFFC; mask_hi = 0x3FFF; copies = 3;
    }
    hdr = 3;
    w = buf[hdr * 2 + 1];
    h = buf[hdr * 2];
    out = 0;
    for (k = 0; k < copies; k++) {
        extend = 0;
        neww = w;
        idx = w - 1 + hdr + 1;
        for (r = 0; r < h; r++) {
            if ((rd_be(buf, idx) & (u16)~mask_lo) != (u16)~mask_lo) {
                extend = 1;
                neww++;
                break;
            }
            idx += w;
        }
        idx = hdr + 1;
        out = h * w * 5 + hdr + 1;
        if (out != size)
            return 0;
        if (shift == 4) {
            wr_le(buf, 0, 0);
            wr_le(buf, 1, (u16)((out - 3) * 2));
            wr_le(buf, 2, (u16)((out - 3) * 2));
        } else {
            wr_le(buf, k, (u16)((out - 3) * 2));
        }
        wr_le(buf, out, (u16)((neww << 8) + h));
        out++;
        for (r = 0; r < h * 5; r++) {
            di = r < h ? mask_hi : 0;
            for (c = 0; c < w; c++) {
                v = rd_be(buf, idx);
                di &= (u16)~mask_lo;
                di |= (u16)((v & (u16)~mask_lo) << shift);
                si = (v >> shift) & mask_hi;
                si |= (u16)(di << (16 - shift)) & (u16)~mask_hi;
                di >>= shift;
                wr_be(buf, out, si);
                idx++;
                out++;
            }
            if (extend) {
                si = r < h ? mask_hi : 0;
                si |= (u16)(di << (16 - shift)) & (u16)~mask_hi;
                wr_be(buf, out, si);
                out++;
            }
        }
        hdr = h * w * 5 + hdr + 1;
        w = buf[hdr * 2 + 1];
        h = buf[hdr * 2];
        size = h * w * 5 + hdr + 1;
        if (out != size)
            return 0;
    }
    return (u16)(out * 2);
}
