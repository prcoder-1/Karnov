/* hw.c — BIOS и нижняя память для перевода (порт). */
#include <stdio.h>
#include <stdlib.h>
#include "orig/hw.h"
#include "orig/port.h"

static u8 lowmem[0x100] = {
    [0x10] = 0x26,          /* equipment: 80×25 цвет, без FPU (DOSBox ega) */
    [0x17] = 0x00,          /* флаги клавиатуры */
};
static u8 video_mode = 3;
static u8 cur_row, cur_col;

u8 hw_peekb(u16 seg, u16 off)
{
    u32 a = (u32)seg * 16 + off;
    if (a >= 0x400 && a < 0x500)
        return lowmem[a - 0x400];
    fprintf(stderr, "hw_peekb: %04X:%04X не эмулируется\n", seg, off);
    return 0;
}

void hw_pokeb(u16 seg, u16 off, u8 v)
{
    u32 a = (u32)seg * 16 + off;
    if (a >= 0x400 && a < 0x500)
        lowmem[a - 0x400] = v;
}

void hw_int10_set_mode(u8 mode)
{
    video_mode = mode;
    hw_video_reset();
}
u8 hw_int10_get_mode(void) { return video_mode; }
void hw_int10_set_cursor(u8 row, u8 col) { cur_row = row; cur_col = col; }
u16 hw_int10_get_cursor(void) { return (u16)(cur_row << 8 | cur_col); }

void hw_int10_teletype(u8 ch)
{
    if (port_opt.verbose)
        fputc(ch == '\r' ? '\n' : ch, stderr);
}

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

/* Джойстика нет: порт 0x201 читается как 0xFF — оси «до упора»,
 * кнопки не нажаты (так в эталонном DOSBox). */
void hw_joystick(s16 n, s16 *x, s16 *y, u8 *btn)
{
    *x = n;
    *y = n;
    *btn = 0;
}

void hw_exit(int code)
{
    exit(code);
}

/* farmalloc Turbo-C возвращает указатель со смещением 4 внутри параграфа;
 * блок выделяется с запасом, чтобы выравнивание оригинала давало тот же
 * объём полезной памяти. */
u8 *hw_farmalloc(u32 size)
{
    u8 *p = calloc(1, size + 16);
    return p ? p + 4 : NULL;
}

void hw_farfree(u8 *p)
{
    if (p)
        free(p - 4);
}

u8 *hw_next_paragraph(u8 *p)
{
    return p + 12;
}

void hw_out(u16 port, u8 v)
{
    if (hw_sound_port_out(port, v) || hw_video_port_out(port, v))
        return;
    if (port_opt.verbose)
        fprintf(stderr, "hw_out: порт %03X не эмулируется\n", port);
}

u8 hw_in(u16 port)
{
    u8 v = 0xFF;
    if (hw_sound_port_in(port, &v) || hw_video_port_in(port, &v))
        return v;
    if (port_opt.verbose)
        fprintf(stderr, "hw_in: порт %03X не эмулируется\n", port);
    return v;
}
