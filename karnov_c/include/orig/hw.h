/* orig/hw.h — замены железа и BIOS/DOS для перевода (не из оригинала).
 * Значения, которые оригинал узнаёт у машины, взяты из эталонного
 * DOSBox (machine=ega, cputype=386), на котором снимается DSDUMP. */
#ifndef ORIG_HW_H
#define ORIG_HW_H
#include "orig/ds.h"

/* Нижняя память BIOS 0000:0400..04FF. */
u8   hw_peekb(u16 seg, u16 off);
void hw_pokeb(u16 seg, u16 off, u8 v);

/* INT 10h */
void hw_int10_set_mode(u8 mode);
u8   hw_int10_get_mode(void);
void hw_int10_set_cursor(u8 row, u8 col);
u16  hw_int10_get_cursor(void);              /* DH:DL */
void hw_int10_teletype(u8 ch);
/* INT 16h AH=0: ожидание клавиши → AH скан-код, AL ASCII; AH=1: без
 * извлечения, 0 — буфер пуст. */
u16  hw_int16_read(void);
u16  hw_int16_peek(void);
/* Положить код в буфер клавиатуры BIOS (порт: из событий SDL). */
void hw_kbd_push(u16 code);
/* Джойстик за портом 0x201: отсчёты осей за n опросов и кнопки. */
void hw_joystick(s16 n, s16 *x, s16 *y, u8 *btn);

void hw_exit(int code);

/* Видеопамять: A000 — плоскости EGA через логику карты, B800 — CGA. */
void hw_vmem_write(u16 seg, u16 off, u8 v);
u8   hw_vmem_read(u16 seg, u16 off);
void hw_vmem_fill(u16 seg, u16 off, u8 v, u16 n);
void hw_vmem_copy(u16 dseg, u16 doff, u16 sseg, u16 soff, u16 n);
void hw_video_reset(void);
void hw_video_present(void);
int  hw_video_save_ppm(const char *path);
int  hw_video_port_out(u16 port, u8 v);
int  hw_video_port_in(u16 port, u8 *v);
int  hw_sound_port_out(u16 port, u8 v);
int  hw_sound_port_in(u16 port, u8 *v);

#endif

/* Порты: PIT (0x40..0x43), динамик (0x61), PIC (0x20). */
void hw_out(u16 port, u8 v);
u8   hw_in(u16 port);

/* Векторы прерываний; вектор 8 вызывается на каждом тике PIT. */
farfn hw_getvect(u8 n);
void  hw_setvect(u8 n, farfn f);

/* Ожидание следующего тика PIT: вызывается из циклов, где оригинал ждёт
 * изменения переменной, которую двигает ISR. Выполняет ISR один раз;
 * в реальном времени сначала выдерживает период PIT. */
void hw_wait_tick(void);

/* Куча (farmalloc/farfree Turbo-C). */
u8  *hw_farmalloc(u32 size);
void hw_farfree(u8 *p);
/* (seg:off + 0xFFFF) & 0xFFFF0000 — начало следующего параграфа. */
u8  *hw_next_paragraph(u8 *p);
