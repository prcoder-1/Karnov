/* Детект CPU и машины — читают флаги процессора и ПЗУ BIOS; заменены
 * результатом эталонного DOSBox (cputype=386, machine=ega). */
#include "orig/fn.h"
#include "orig/hw.h"

/* @027D:000F file:03FDF detect_cpu HW — DOSBox 386 → 0x386 */
s16 detect_cpu(void)
{
    return 0x386;
}

/* @01F3:0007 file:03737 detect_machine HW — модель по F000:FFFE; DOSBox ega → 1 */
s16 detect_machine(void)
{
    return 1;
}

/* @01F3:0153 file:03883 rom_probe HW — часть detect_machine */
s16 rom_probe(void)
{
    return 0;
}

/* @01F3:015D file:0388D rom_compare HW — часть detect_machine */
s16 rom_compare(void)
{
    return 0;
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
    hw_pokeb(0x40, 0x10, (u8)((hw_peekb(0x40, 0x10) & 0xCF) | 0x10));
    hw_int10_set_mode(4);
    return 0;
}

/* @0D09:00A3 file:0E933 herc_detect HW — порт 3BAh; DOSBox ega: нет → 1 */
s16 herc_detect(void)
{
    return 1;
}
