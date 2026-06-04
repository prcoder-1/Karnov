/* render.h — SDL2 окно + framebuffer + тайл/спрайт-блиттеры.
 */
#ifndef KARNOV_RENDER_H
#define KARNOV_RENDER_H

#include "karnov.h"

/* Открывает SDL2 окно WIN_W*scale × WIN_H*scale. Возвращает 0 при OK.
 * Если headless=true (для PPM-режима) — окно/рендерер не создаются. */
int  render_init(int scale, bool headless);
void render_shutdown(void);

/* Возвращает framebuffer ARGB8888, размер WIN_W × WIN_H. */
u32 *render_framebuffer(void);

/* Очищает framebuffer цветом. */
void render_clear(u32 color);

/* Прокачивает SDL события. Возвращает true если получили quit (ESC/X). */
bool render_pump_events(void);

/* Презентует текущий framebuffer в окне (no-op в headless). */
void render_present(void);

/* Рисует тайл (16×10) в позиции (dst_x, dst_y).
 * gfx — 40 байт packed 2bpp. mode_cga=true — CGA-палитра. */
void render_blit_tile(int dst_x, int dst_y, const u8 *gfx, u8 attr, bool mode_cga);

/* EGA-вариант блитера тайла: gfx_ega — 80 байт packed 4bpp (2 пикселя/байт,
 * каждый пиксель = прямой EGA-индекс 0..15). Используется per-pixel
 * EGA_PAL16 (без attr-based sub-palette). attr остаётся параметром для
 * будущих использований (collision flag etc.), сейчас не влияет на цвета. */
void render_blit_tile_ega(int dst_x, int dst_y, const u8 *gfx_ega, u8 attr);

/* Сохраняет текущий framebuffer как PPM (P6). */
int  render_save_ppm(const char *path);

/* Рисует прямоугольник без обводки. */
void render_fill_rect(int x, int y, int w, int h, u32 color);

/* Рисует декодированный спрайт (массив пикселей 0..15 или 0xFF=transparent).
 * mirror=true — горизонтальное отражение. */
void render_blit_sprite(int dst_x, int dst_y, const u8 *pixels, int w, int h,
                        bool mirror, const u32 *palette16);

/* Тайлит спрайт вертикально между cy_top и cy_bot (раскладная лесенка). */
void render_blit_ladder(int cx, int cy_top, int cy_bot,
                        const u8 *pixels, int w, int h, const u32 *palette16);

/* 5×7 битмапный шрифт для HUD; строка ASCII. */
void render_draw_text(int x, int y, const char *s, u32 color);

#endif /* KARNOV_RENDER_H */
