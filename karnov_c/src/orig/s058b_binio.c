/* Модуль 0x058B (file 0x70B0) — чтение файлов уровня с RLE-распаковкой.
 * Байт 0x5A — escape: «5A n b» = n повторов b, «5A 5A» = сам 0x5A. */
#include <stdio.h>
#include "orig/fn.h"

/* @058B:0006 file:070B6 bin_fopen */
FILE *bin_fopen(const char *name, const char *mode)
{
    FILE *fp = rtl_fopen(name, mode);
    if (fp == NULL)
        return NULL;
    rle_in_run = 0;
    io_buf_pos = 0x200;
    io_buf_avail = 0x200;
    return fp;
}

/* @058B:004C file:070FC bin_fclose */
s16 bin_fclose(FILE *fp)
{
    return (s16)fclose(fp);
}

/* @058B:0066 file:07116 bin_reset */
void bin_reset(void)
{
    rle_in_run = 0;
    io_buf_pos = 0x200;
}

/* @058B:0077 file:07127 bin_fread */
u16 bin_fread(u8 *buf, u16 size, u16 count, FILE *fp)
{
    u16 n, i;
    for (n = 0; n < count; n++) {
        for (i = 0; i < size; i++, buf++) {
            if (rle_in_run) {
                if (--rle_run_left == 0)
                    rle_in_run = 0;
                *buf = rle_run_byte;
                continue;
            }
            if (read_byte(buf, fp))
                return n;
            if (*buf != 0x5A)
                continue;
            if (read_byte(buf, fp))
                return n;
            if (*buf == 0x5A)
                continue;
            rle_run_left = *buf - 1;
            rle_in_run = 1;
            if (read_byte(buf, fp))
                return n;
            rle_run_byte = *buf;
        }
    }
    return count;
}

/* @058B:0135 file:071E5 read_byte
 * После дозагрузки буфера байт берётся без проверки, сколько прочитано. */
s16 read_byte(u8 *dst, FILE *fp)
{
    if (io_buf_pos >= io_buf_avail) {
        if (io_buf_avail != 0x200)
            return 1;
        io_buf_pos = 0;
        io_buf_avail = (s16)fread(io_buffer, 1, 0x200, fp);
    }
    *dst = io_buffer[io_buf_pos];
    io_buf_pos++;
    return 0;
}
