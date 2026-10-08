/* Модуль 0x0361 (file 0x4E10) — чтение клавиши через BIOS. */
#include "orig/fn.h"
#include "orig/hw.h"

/* @0361:001F file:04E2F get_key — для расширенных клавиш (AL=0)
 * возвращает 0, а скан-код отдаёт следующим вызовом. */
s16 get_key(void)
{
    u16 ax;
    if (key_pending) {
        s16 k = key_pending;
        key_pending = 0;
        return k;
    }
    ax = hw_int16_read();
    if ((u8)ax == 0)
        key_pending = (u8)(ax >> 8);
    return (u8)ax;
}
