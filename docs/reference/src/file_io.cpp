/* file_io.cpp — обёртка чтения с прозрачной RLE-распаковкой.
 *
 * Это пословный перевод из KARNOV.EXE:
 *   cs:0x58B:0x06  — bin_fopen   (файл 0x70B6)
 *   cs:0x58B:0x4C  — bin_fclose  (файл 0x70FC)
 *   cs:0x58B:0x77  — bin_fread   (файл 0x7127)
 *   cs:0x58B:0x135 — read_byte   (файл 0x71E5; 512-байтная буферизация)
 *
 * Состояние декомпрессора (флаг повтора + счётчик + текущий байт + буфер
 * и его позиция) — глобальное, чтобы фактический поток мог «переходить»
 * через границы запрашиваемых блоков. Точно так же устроен оригинал
 * (DS:0xD568, 0xD76A, 0xD76D, 0xD566, 0xD56A, 0xD76B).
 */
#include "file_io.h"
#include "globals.h"
#include <cctype>
#include <cstdio>
#include <cstring>

namespace karnov {

/* DOS — case-insensitive, Linux — нет. Пробуем имя как есть, затем
 * полностью в верхнем регистре, затем в нижнем. */
FILE* karnov_fopen(const char* filename, const char* mode) {
    FILE* fp = std::fopen(filename, mode);
    if (fp) return fp;

    char buf[256];
    std::size_t n = std::strlen(filename);
    if (n >= sizeof(buf)) return nullptr;

    for (std::size_t i = 0; i < n; ++i) {
        buf[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(filename[i])));
    }
    buf[n] = '\0';
    fp = std::fopen(buf, mode);
    if (fp) return fp;

    for (std::size_t i = 0; i < n; ++i) {
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(filename[i])));
    }
    buf[n] = '\0';
    return std::fopen(buf, mode);
}

/* Низкоуровневое чтение одного байта из файла с буферизацией 512 байт.
 * KARNOV.EXE: cs:0x58B:0x135 (cs:0x71E5).
 *
 * Возвращает 0 при успехе, 1 при EOF/ошибке. Прочитанный байт помещается
 * по дальнему указателю dst.
 */
static int read_byte(u8* dst, FILE* fp) {
    /* if (io_buf_pos >= io_buf_avail) — буфер пуст, надо перечитать. */
    if (io_buf_pos >= io_buf_avail) {
        /* В оригинале: если предыдущий fread вернул не-512, EOF;
         * иначе перечитать ещё 512 байт. */
        if (io_buf_avail != IO_BUF_SIZE) {
            /* Уже был частичный блок — конец файла. */
            return 1;
        }
        io_buf_pos = 0;
        io_buf_avail = (u16)std::fread(io_buffer, 1, IO_BUF_SIZE, fp);
    }
    *dst = io_buffer[io_buf_pos++];
    return 0;
}

/* KARNOV.EXE: cs:0x58B:0x06 (файл 0x70B6).
 *
 * Открывает файл и инициализирует состояние декомпрессора:
 *   io_buf_avail = 512   (пока ничего не считано → форсировать первый fread)
 *   io_buf_pos   = 512   (то же)
 *   rle_in_run   = 0     (не в режиме повтора)
 */
FILE* bin_fopen(const char* filename, const char* mode) {
    FILE* fp = karnov_fopen(filename, mode);
    if (!fp) return nullptr;
    rle_in_run    = 0;
    io_buf_pos    = IO_BUF_SIZE;
    io_buf_avail  = IO_BUF_SIZE;
    bin_stream    = fp;
    return fp;
}

/* KARNOV.EXE: cs:0x58B:0x4C (файл 0x70FC). */
int bin_fclose(FILE* fp) {
    if (fp == bin_stream) bin_stream = nullptr;
    return std::fclose(fp);
}

/* Сброс состояния (используется при перечитывании файла).
 * KARNOV.EXE: cs:0x58B:0x10B (файл 0x7116). */
void bin_reset() {
    rle_in_run = 0;
    io_buf_pos = IO_BUF_SIZE;
}

/* KARNOV.EXE: cs:0x58B:0x77 (файл 0x7127) — bin_fread.
 *
 * Псевдокод оригинала (с учётом перевода адресов в имена):
 *   size_t bin_fread(buf, size, count, fp):
 *       items_done = 0
 *       for i in 0..count-1:
 *           bytes_done = 0
 *           while bytes_done < size:
 *               if rle_in_run:
 *                   --rle_run_left
 *                   if rle_run_left == 0: rle_in_run = 0
 *                   *buf = rle_run_byte
 *               else:
 *                   if read_byte(buf, fp) != 0: return items_done
 *                   if *buf == 0x5A:
 *                       if read_byte(buf, fp) != 0: return items_done
 *                       if *buf != 0x5A:               # 5A N != 5A 5A
 *                           rle_run_left = (*buf) - 1  # (N - 1) mod 256
 *                           rle_in_run = 1
 *                           if read_byte(buf, fp) != 0: return items_done
 *                           rle_run_byte = *buf
 *                       # иначе: '*buf' уже равен 0x5A — литерал
 *               ++buf; ++bytes_done
 *           ++items_done
 *       return items_done
 */
std::size_t bin_fread(void* buf, std::size_t size, std::size_t count, FILE* fp) {
    u8* p = static_cast<u8*>(buf);
    std::size_t items_done = 0;

    for (std::size_t i = 0; i < count; ++i) {
        std::size_t bytes_done = 0;
        while (bytes_done < size) {
            if (rle_in_run) {
                /* cs:0x7136..0x7157 */
                --rle_run_left;
                if (rle_run_left == 0) rle_in_run = 0;
                *p = rle_run_byte;
            } else {
                /* cs:0x715A..0x71C6: прочитать байт. Если 0x5A — это маркер. */
                if (read_byte(p, fp) != 0) return items_done;
                if (*p == RLE_ESCAPE) {
                    /* Прочитать следующий байт. */
                    if (read_byte(p, fp) != 0) return items_done;
                    if (*p != RLE_ESCAPE) {
                        /* RLE: следующая операция — повтор. */
                        rle_run_left = static_cast<u8>(*p - 1); /* (N-1) mod 256 */
                        rle_in_run   = 1;
                        if (read_byte(p, fp) != 0) return items_done;
                        rle_run_byte = *p;
                    }
                    /* иначе: '*p' уже содержит 0x5A — литерал. */
                }
            }
            ++p;
            ++bytes_done;
        }
        ++items_done;
    }
    return items_done;
}

/* ─── Глобалы декомпрессора ─────────────────────────────────────────── */
u16  rle_in_run    = 0;
u8   rle_run_byte  = 0;
u8   rle_run_left  = 0;
u16  io_buf_avail  = IO_BUF_SIZE;
u16  io_buf_pos    = IO_BUF_SIZE;
u8   io_buffer[IO_BUF_SIZE] = {0};
FILE* bin_stream    = nullptr;

} /* namespace karnov */
