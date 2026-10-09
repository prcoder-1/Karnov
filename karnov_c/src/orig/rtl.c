/* Рантайм Turbo-C, который нельзя заменить libc напрямую. */
#include "orig/fn.h"
#include "orig/hw.h"

/* @1B7A:0000 file:1CFA0 bioskey RTL — INT 16h: 0 — чтение (ожидание),
 * 1 — проверка (0, если пусто) */
s16 rtl_bioskey(s16 cmd)
{
    if (cmd == 1)
        return (s16)hw_int16_peek();
    return (s16)hw_int16_read();
}

/* @1CE7:0017 file:1E687 rand RTL — LCG Turbo-C */
s16 rtl_rand(void)
{
    rand_seed = rand_seed * 0x15A4E35UL + 1;
    return (s16)((rand_seed >> 16) & 0x7FFF);
}
