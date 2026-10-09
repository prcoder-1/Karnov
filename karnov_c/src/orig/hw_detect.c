/* Детект машины и видеоадаптеров — читают ПЗУ BIOS и порты; заменены
 * результатом DOSBox (machine=ega). */
#include "orig/fn.h"
#include "orig/hw.h"

/* @01F3:0007 file:03737 detect_machine HW — модель по F000:FFFE; DOSBox ega → 1 */
s16 detect_machine(void)
{
    return 1;
}

/* @0D09:0006 file:0E896 ega_detect HW — INT 10h AH=12h; DOSBox ega: есть,
 * ставит режим 0Dh, возвращает 0 */
s16 ega_detect(void)
{
    hw_int10_set_mode(0x0D);
    return 0;
}

/* @0D09:0050 file:0E8E0 cga_detect HW — проба B800:3FFF; есть → режим 4, 0 */
s16 cga_detect(void)
{
    bios_equip = (u8)((bios_equip & 0xCF) | 0x10);
    hw_int10_set_mode(4);
    return 0;
}

/* @0D09:00A3 file:0E933 herc_detect HW — порт 3BAh; эмуляция показывает и
 * Hercules (выбор в меню видеорежимов) → 0 */
s16 herc_detect(void)
{
    return 0;
}
