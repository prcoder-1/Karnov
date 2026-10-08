/* fsopen.h — открытие файлов данных без учёта регистра. */
#ifndef KARNOV_FSOPEN_H
#define KARNOV_FSOPEN_H
#include <stdio.h>
FILE *karnov_fopen(const char *filename, const char *mode);
#endif
