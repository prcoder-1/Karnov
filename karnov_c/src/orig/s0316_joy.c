/* Модуль 0x0316 (file 0x4960) — джойстик (порт 0x201). Замеры — HW. */
#include "orig/fn.h"
#include "orig/hw.h"

/* @0316:000D file:0496D joy_sample HW — 255 опросов порта */
static u8 *joy_sample(void)
{
    hw_joystick(255, &joy_x, &joy_y, &joy_btn);
    return (u8 *)&joy_x;
}

/* @0316:0071 file:049D1 joy_sample_delay HW — то же с паузой n опросов */
static u8 *joy_sample_delay(s16 n)
{
    (void)n;
    hw_joystick(255, &joy_x, &joy_y, &joy_btn);
    return (u8 *)&joy_x;
}

/* @0316:00E0 file:04A40 joy_read — при первом вызове подбирает задержку
 * опроса (joy_delay) так, чтобы отсчёты были меньше 0x80. Отсчёты
 * читаются байтами (x и старший байт x). */
u8 *joy_read(void)
{
    u8 *p = NULL;
    s16 sx, sy, k;
    if (joy_autotune != 0) {
        joy_delay = 0x14;
        while (joy_delay >= 0) {
            sx = sy = 0;
            for (k = 0; (u16)k < 1; k++) {
                p = joy_delay == 0 ? joy_sample() : joy_sample_delay(joy_delay);
                sx += p[0];
                sy += p[1];
            }
            if (p[0] < 0x3C && p[1] < 0x3C)
                joy_delay -= (0x80 - p[0]) / 0x28;
            if (p[0] >= 0x80 || p[1] >= 0x80)
                break;
            joy_delay--;
        }
        joy_delay++;
        joy_autotune = 0;
    }
    return joy_delay == 0 ? joy_sample() : joy_sample_delay(joy_delay);
}
