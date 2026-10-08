/* file_io.c — обёртка чтения с прозрачной RLE-распаковкой.
 *
 * Пословный перевод из KARNOV.EXE:
 *   cs:0x58B:0x06  — bin_fopen   (file 0x70B6)
 *   cs:0x58B:0x4C  — bin_fclose  (file 0x70FC)
 *   cs:0x58B:0x77  — bin_fread   (file 0x7127)
 *   cs:0x58B:0x135 — read_byte   (file 0x71E5; 512-байтная буферизация)
 *
 * Состояние декомпрессора (флаг повтора + счётчик + текущий байт + буфер
 * и его позиция) — глобальное, чтобы поток мог «переходить» через границы
 * запрашиваемых блоков. Так устроен оригинал (DS:0xD568, 0xD76A, 0xD76D,
 * 0xD566, 0xD56A, 0xD76B).
 */
#include "file_io.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ─── Глобалы декомпрессора (соответствуют DS:* в оригинале) ─────────── */
static u16   rle_in_run    = 0;
static u8    rle_run_byte  = 0;
static u8    rle_run_left  = 0;
static u16   io_buf_avail  = IO_BUF_SIZE;
static u16   io_buf_pos    = IO_BUF_SIZE;
static u8    io_buffer[IO_BUF_SIZE] = {0};
static FILE *bin_stream    = NULL;

/* Низкоуровневое чтение байта из файла с буферизацией.
 * Возвращает 0 при успехе, 1 при EOF/ошибке. */
static int read_byte(u8 *dst, FILE *fp)
{
    if (io_buf_pos >= io_buf_avail) {
        if (io_buf_avail != IO_BUF_SIZE) {
            return 1; /* частичный блок ранее → EOF */
        }
        io_buf_pos = 0;
        io_buf_avail = (u16)fread(io_buffer, 1, IO_BUF_SIZE, fp);
        if (io_buf_avail == 0) return 1;
    }
    *dst = io_buffer[io_buf_pos++];
    return 0;
}

FILE *bin_fopen(const char *filename, const char *mode)
{
    FILE *fp = karnov_fopen(filename, mode);
    if (!fp) return NULL;
    rle_in_run   = 0;
    io_buf_pos   = IO_BUF_SIZE;
    io_buf_avail = IO_BUF_SIZE;
    bin_stream   = fp;
    return fp;
}

int bin_fclose(FILE *fp)
{
    if (fp == bin_stream) bin_stream = NULL;
    return fclose(fp);
}

void bin_reset(void)
{
    rle_in_run = 0;
    io_buf_pos = IO_BUF_SIZE;
}

/* KARNOV.EXE: cs:0x58B:0x77 — bin_fread.
 * Псевдокод см. в шапке. */
size_t bin_fread(void *buf, size_t size, size_t count, FILE *fp)
{
    u8 *p = (u8 *)buf;
    size_t items_done = 0;

    for (size_t i = 0; i < count; ++i) {
        size_t bytes_done = 0;
        while (bytes_done < size) {
            if (rle_in_run) {
                --rle_run_left;
                if (rle_run_left == 0) rle_in_run = 0;
                *p = rle_run_byte;
            } else {
                if (read_byte(p, fp) != 0) return items_done;
                if (*p == RLE_ESCAPE) {
                    if (read_byte(p, fp) != 0) return items_done;
                    if (*p != RLE_ESCAPE) {
                        rle_run_left = (u8)(*p - 1);
                        rle_in_run   = 1;
                        if (read_byte(p, fp) != 0) return items_done;
                        rle_run_byte = *p;
                    }
                    /* иначе: *p == 0x5A — литерал. */
                }
            }
            ++p;
            ++bytes_done;
        }
        ++items_done;
    }
    return items_done;
}
