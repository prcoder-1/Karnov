/* hw.c — клавиатура, джойстик и порты для перевода (порт). */
#include "orig/hw.h"
#include "orig/port.h"

u8 bios_equip = 0x26, bios_kbflags;

static u16 kbuf[16];
static unsigned kb_head, kb_tail;

void hw_kbd_push(u16 code)
{
    if (kb_tail - kb_head < 15)
        kbuf[kb_tail++ % 16] = code;
}

u16 hw_int16_peek(void)
{
    port_pump_events();
    return kb_head == kb_tail ? 0 : kbuf[kb_head % 16];
}

u16 hw_int16_read(void)
{
    while (kb_head == kb_tail) {
        if (port_opt.headless)
            return 0x011B;      /* без окна ждать нечего: Esc */
        hw_wait_tick();
    }
    return kbuf[kb_head++ % 16];
}

/* Джойстика нет: порт 0x201 читается как 0xFF — за 255 опросов оси
 * «до упора», кнопки не нажаты (так в DOSBox). */
void hw_joystick(s16 *x, s16 *y, u8 *btn)
{
    *x = 255;
    *y = 255;
    *btn = 0;
}

void hw_out(u16 port, u8 v)
{
    if (!hw_sound_port_out(port, v))
        hw_video_port_out(port, v);
}

u8 hw_in(u16 port)
{
    u8 v = 0xFF;
    if (!hw_sound_port_in(port, &v))
        hw_video_port_in(port, &v);
    return v;
}
