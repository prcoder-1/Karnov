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

/* Вектор INT 0: стартовый код Turbo-C (@0000:0122) ставит свой
 * обработчик деления на 0 — CS:0119; сегмент не эмулируется. */
static const u8 ivt0[4] = { 0x19, 0x01, 0x00, 0x00 };

u8 hw_peekb(u16 seg, u16 off)
{
    u32 a = (u32)seg * 16 + off;
    if (a < 4)
        return ivt0[a];
    if (a >= 0x400 && a < 0x500)
        return lowmem[a - 0x400];
    fprintf(stderr, "hw_peekb: %04X:%04X не эмулируется\n", seg, off);
    return 0;
}

u16 hw_peekw(u16 seg, u16 off)
{
    return (u16)(hw_peekb(seg, off) | hw_peekb(seg, (u16)(off + 1)) << 8);
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

/* BIOS: фон/рамка — регистр выбора цвета CGA */
void hw_int10_set_bg(u8 color) { hw_out(0x3D9, (u8)(color & 0x1F)); }
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

u8 hw_conv[HW_CONV_SIZE];

/* farmalloc возвращает нормализованный заголовок + 4 */
u16 hw_heap_seg(const u8 *p)
{
    return (u16)(((u32)(p - hw_conv) - 4) >> 4);
}

u8 *hw_next_paragraph(u8 *p)
{
    return &hw_conv[((u32)hw_heap_seg(p) + 1) * 16];
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
