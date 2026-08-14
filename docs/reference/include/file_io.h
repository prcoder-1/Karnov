/* file_io.h — обёртка чтения с прозрачной RLE-распаковкой.
 * Соответствует функциям cs:0x58B:0x06 (fopen), cs:0x58B:0x77 (fread),
 * cs:0x58B:0x4C (fclose), cs:0x7127 (RLE-распаковщик), cs:0x71E5 (буферизация).
 */
#ifndef KARNOV_FILE_IO_H
#define KARNOV_FILE_IO_H

#include "karnov.h"
#include <cstdio>

namespace karnov {

/* Регистронезависимое открытие файла: пробует имя как есть, затем
 * ВЕРХНИЙ регистр, затем нижний. Нужно потому, что DOS — case-insensitive,
 * а Linux/Unix — нет. Имена файлов игры на диске обычно в верхнем
 * регистре (LEVEL0.BIN), а в исходниках встречаются в нижнем. */
FILE* karnov_fopen(const char* filename, const char* mode);

/* KARNOV.EXE: cs:0x58B:0x06 */
FILE* bin_fopen(const char* filename, const char* mode);

/* KARNOV.EXE: cs:0x58B:0x77 — фактически fread, но с RLE-распаковкой. */
std::size_t bin_fread(void* buf, std::size_t size, std::size_t count, FILE* fp);

/* KARNOV.EXE: cs:0x58B:0x4C */
int bin_fclose(FILE* fp);

/* Сброс состояния декомпрессора (после смены файла). */
void bin_reset();

} /* namespace karnov */

#endif
