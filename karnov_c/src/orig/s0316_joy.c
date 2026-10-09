/* Модуль 0x0316 (file 0x4960) — джойстик (порт 0x201). Замеры — HW. */
#include "orig/fn.h"
#include "orig/hw.h"

/* @0316:00E0 file:04A40 joy_read — замер осей и кнопок. Оригинал при
 * первом вызове подбирает задержку опроса; у эмуляции порта её нет. */
void joy_read(void)
{
    hw_joystick(&joy_x, &joy_y, &joy_btn);
}
