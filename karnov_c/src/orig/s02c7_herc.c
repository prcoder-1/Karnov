/* Модуль 0x02C7 (file 0x4470) — графический режим Hercules. */
#include "orig/fn.h"
#include "orig/hw.h"

/* @02C7:0087 file:044F7 herc_graphics — 6845 в графику 720×348,
 * страница 1 (B800) очищается */
static void herc_graphics(void)
{
    s16 si;
    hw_out(0x3BF, 3);
    hw_out(0x3B8, 2);
    for (si = 0; si < 0xC; si++) {
        hw_out(0x3B4, (u8)si);
        hw_out(0x3B5, herc_crtc[si]);
    }
    hw_vmem_fill(0xB800, 0, 0, 0x6000);
    hw_out(0x3B8, 0x8A);
}

/* @02C7:000E file:0447E herc_init — оборудование: «цветной 80×25»,
 * графика, BIOS-режим 4 для текста */
void herc_init(void)
{
    if (herc_detect() != 0)
        return;
    bios_equip = (u8)((bios_equip & 0xCF) | 0x20);
    herc_graphics();
    hw_int10_set_mode(4);
    hw_int10_set_bg(0);
}

/* @02C7:0052 file:044C2 herc_shutdown — монохромный режим 7 */
void herc_shutdown(void)
{
    bios_equip = (u8)((bios_equip & 0xCF) | 0x30);
    hw_out(0x3BF, 0);
    hw_int10_set_mode(7);
}
