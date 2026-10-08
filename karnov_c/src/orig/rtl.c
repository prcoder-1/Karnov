/* Рантайм Turbo-C, который нельзя заменить libc напрямую. */
#include <stdio.h>
#include "fsopen.h"
#include "orig/fn.h"

/* @1BF7:0203 file:1D973 fopen RTL — имена DOS без учёта регистра */
FILE *rtl_fopen(const char *name, const char *mode)
{
    return karnov_fopen(name, mode);
}
