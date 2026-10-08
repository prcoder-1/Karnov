/* fsopen.c — открытие файлов данных без учёта регистра (DOS → Linux). */
#include "fsopen.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* DOS — case-insensitive, Linux — нет. Пробуем имя как есть, затем
 * полностью в верхнем регистре, затем в нижнем. */
FILE *karnov_fopen(const char *filename, const char *mode)
{
    FILE *fp = fopen(filename, mode);
    if (fp) return fp;

    char buf[256];
    size_t n = strlen(filename);
    if (n >= sizeof(buf)) return NULL;

    for (size_t i = 0; i < n; ++i) {
        buf[i] = (char)toupper((unsigned char)filename[i]);
    }
    buf[n] = '\0';
    fp = fopen(buf, mode);
    if (fp) return fp;

    for (size_t i = 0; i < n; ++i) {
        buf[i] = (char)tolower((unsigned char)filename[i]);
    }
    buf[n] = '\0';
    return fopen(buf, mode);
}
