/* sprite.h — KS v3/v4 cache loader.
 *
 * Формат кэша:
 *   magic 'KS' (0x534B), u16 version, u16 count
 *   per-sprite v3: u16 id, u8 w, u8 h, u8 bpp, u8 reserved, u8 pixels[w*h]
 *   per-sprite v4: ... + u16 ai_type перед pixels (слово 0 директории
 *     SPRITES.EGA/CGA — манифест sprite→AI type; 0xFFFF = неизвестно)
 *     pixel: 0..15 (EGA) или 0..3 (CGA); 0xFF = прозрачный
 */
#ifndef KARNOV_SPRITE_H
#define KARNOV_SPRITE_H

#include "karnov.h"

typedef struct {
    u16 id;
    u16 ai_type;        /* манифест из директории архива; 0xFFFF если v3 */
    u8  w, h, bpp;
    /* art_w — ширина непрозрачного содержимого. В архиве арт прижат к
     * ЛЕВОМУ краю канвы (проверено на всех 334 спрайтах), а хвост канвы —
     * прозрачный паддинг; поэтому art_w = max_ink_col + 1. Нужна для
     * зеркалирования (отражаем в пределах арта, а не канвы) и для оценки
     * bbox. Прежнее поле body_cx было костылём под зеркальный кэш. */
    u8  art_w;
    const u8 *pixels;   /* указатель в кэш-буфер */
} sprite_t;

int  sprite_cache_load(const char *path);
int  sprite_cache_count(void);
const sprite_t *sprite_find(u16 id);   /* NULL если не найден */
bool sprite_is_cga(void);

/* Манифест из директории архива: спрайты AI-типа ai_type — непрерывный
 * диапазон id (base..base+count-1). false = типа нет в кэше / кэш v3. */
bool sprite_manifest(u16 ai_type, u16 *base, u16 *count);

#endif
