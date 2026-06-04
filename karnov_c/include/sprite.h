/* sprite.h — KS v3 cache loader.
 *
 * Формат кэша:
 *   magic 'KS' (0x534B), u16 version, u16 count
 *   per-sprite: u16 id, u8 w, u8 h, u8 bpp, u8 pixels[w*h]
 *     pixel: 0..15 (EGA) или 0..3 (CGA); 0xFF = прозрачный
 */
#ifndef KARNOV_SPRITE_H
#define KARNOV_SPRITE_H

#include "karnov.h"

typedef struct {
    u16 id;
    u8  w, h, bpp;
    /* body_cx — горизонтальный центр непрозрачного содержимого ((min_col+max_col)/2).
     * Используется как «анкер» при рендере. Karnov-спрайты имеют body
     * сдвинутый вправо (cols 10..31 в 32-wide canvas), поэтому fixed center=w/2
     * вызывает 20-px скачок при flip. body_cx обеспечивает seamless flip. */
    u8  body_cx;
    const u8 *pixels;   /* указатель в кэш-буфер */
} sprite_t;

int  sprite_cache_load(const char *path);
int  sprite_cache_count(void);
const sprite_t *sprite_find(u16 id);   /* NULL если не найден */
bool sprite_is_cga(void);

#endif
