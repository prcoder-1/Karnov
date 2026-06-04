/* file_io.h — RLE-распаковщик и буферизованное чтение.
 *
 * Порт KARNOV.EXE cs:0x58B (bin_fopen/fread/fclose, маркер 0x5A).
 */
#ifndef KARNOV_FILE_IO_H
#define KARNOV_FILE_IO_H

#include "karnov.h"
#include <stdio.h>

#define IO_BUF_SIZE 512
#define RLE_ESCAPE  0x5A

/* Открывает файл и сбрасывает состояние RLE-декомпрессора. */
FILE *bin_fopen(const char *filename, const char *mode);

/* Закрывает файл; декомпрессор не сбрасывает (вызывается отдельно). */
int   bin_fclose(FILE *fp);

/* Сбрасывает буфер чтения и флаг повтора. Не закрывает файл. */
void  bin_reset(void);

/* fread с прозрачной RLE-распаковкой. Возвращает фактически прочитанное
 * количество элементов (count) — может быть меньше при EOF. */
size_t bin_fread(void *buf, size_t size, size_t count, FILE *fp);

/* Прямое открытие без сброса декомпрессора (для MOV — не сжаты). */
FILE *karnov_fopen(const char *filename, const char *mode);

#endif /* KARNOV_FILE_IO_H */
