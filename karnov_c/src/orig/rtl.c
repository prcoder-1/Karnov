/* Рантайм Turbo-C, который нельзя заменить libc напрямую. */
#include <stdio.h>
#include <string.h>
#include "fsopen.h"
#include "orig/fn.h"
#include "orig/hw.h"

/* @1BF7:0203 file:1D973 fopen RTL — имена DOS без учёта регистра */
FILE *rtl_fopen(const char *name, const char *mode)
{
    return karnov_fopen(name, mode);
}

/* @1C92:000E file:1E12E fmemcpy RTL — far-копирование */
void rtl_fmemcpy(void *dst, const void *src, u16 n)
{
    memcpy(dst, src, n);
}

/* @1B89:0005 file:1D095 exit RTL */
void rtl_exit(s16 code)
{
    hw_exit(code);
}

/* @1BE0:0008 file:1D608 farmalloc RTL */
u8 *rtl_farmalloc(u32 size)
{
    return hw_farmalloc(size);
}

/* @1BBC:000A file:1D3CA farfree RTL */
void rtl_farfree(u8 *p)
{
    hw_farfree(p);
}

/* @1C31:0053 file:1DB63 fseek RTL */
s16 rtl_fseek(FILE *fp, s32 off, s16 whence)
{
    return (s16)fseek(fp, off, whence);
}

/* @1B7A:0000 file:1CFA0 bioskey RTL — INT 16h: 0 — чтение (ожидание),
 * 1 — проверка (0, если пусто) */
s16 rtl_bioskey(s16 cmd)
{
    if (cmd == 1)
        return (s16)hw_int16_peek();
    return (s16)hw_int16_read();
}
