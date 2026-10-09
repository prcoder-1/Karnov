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

/* Порт 0x201 за 255 опросов (joy_sample): бит оси держится, пока идёт
 * отсчёт, число опросов с битом — отсчёт оси. Счётчики джойстика A — в
 * *a (X — младший байт, Y — старший: add si, bx), B — в *b; кнопки
 * нажаты — биты 0..3. Без геймпада порт читается как 0xFF: отсчёты 255,
 * кнопки не нажаты (так в DOSBox без джойстика). */
void hw_joystick(s16 *a, s16 *b, u8 *btn)
{
    u8 x, y;
    *a = *b = (s16)0xFFFF;
    *btn = 0;
    if (port_gamepad(&x, &y, btn))
        *a = (s16)(y << 8 | x);
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
