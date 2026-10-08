/* Модуль 0x0282 (file 0x4020) — пауза по тикам таймера. */
#include "orig/fn.h"
#include "orig/hw.h"

/* @0282:000F file:0402F delay — ждёт n тиков; timer_ticks затем
 * возвращается к значению на входе */
void delay(s16 n)
{
    u32 start = timer_ticks;
    while ((s32)(timer_ticks - start) < (s32)n)
        hw_wait_tick();
    timer_ticks = start;
}
