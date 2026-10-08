/* orig/todo.h — ещё не переведённые функции оригинала (заглушки в
 * todo.c). При переводе функция удаляется отсюда и из todo.c. */
#ifndef ORIG_TODO_H
#define ORIG_TODO_H
#include "orig/ds.h"

void herc_init(void);           /* 02C7:000E */
void herc_shutdown(void);       /* 02C7:0052 */
void tandy_convert(u8 *dst, const u8 *src); /* 0D18:000D */

/* asm-драйверы (сегмент 0x0920): пока пустые, без остановки */
void sprite_blit(u16 off, s16 sprite, s16 phase, s16 mode);     /* 0920:0002 */
void sprite_erase(u16 off, s16 sprite, s16 phase);              /* 0920:13FE */
void vb_present_rect(u16 off, s16 w, s16 h);                    /* 0920:2392 */
void scroll_blit(u16 off);
void hud_blit(u16 off, s16 sprite, s16 phase);                  /* 0920:24C8 */
void hud_erase(u16 off, s16 sprite, s16 phase);                 /* 0920:2A56 */                                      /* 05A3:0009 */
u16 tandy_sprite_convert(u8 *dst, const u8 *src); /* 0D18:009D */
void show_hiscores(s16 ticks);   /* 02D3:00FB */
void hiscore_enter(u8 *score);   /* 02D3:000C */

#endif
