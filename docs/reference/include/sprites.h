/* sprites.h — загрузка и декодер спрайтов Karnov.
 *
 * Кэш data/karnov_sprites.bin производится скриптом
 * tools/sprites_extract.py из runtime-дампа DOSBox.
 *
 * Формат in-memory (verified — block-major 4bpp EGA):
 *   +00..+05: 3 indir WORD (для mask=1..3 вариантов, indir1=0, indir2/3=size)
 *   +06:      WORD strip_hdr (count = high byte, width = low byte)
 *   +08+:     5 блоков (count*width) слов каждый:
 *               block 0: mask         (1 = прозрачно)
 *               block 1: plane 0 (R)
 *               block 2: plane 1 (G)
 *               block 3: plane 2 (B)
 *               block 4: plane 3 (I)
 *
 *   Декодированный пиксель: 4-битный EGA-индекс 0..15 (или -1 = прозрачный).
 *
 * Дизассемблированные адреса:
 *   0xD8CE  — sprite_loader_main
 *   0xDFC2  — ega_transform
 *   0xAA02  — sprite_blit
 *   0xBCA8  — inner_blit
 */
#ifndef KARNOV_SPRITES_H
#define KARNOV_SPRITES_H

#include "karnov.h"

namespace karnov {

constexpr int SPR_MAX_WIDTH  = 96;  /* count up to 6 → count*16 = 96 */
constexpr int SPR_MAX_HEIGHT = 60;

struct DecodedSprite {
    int  w;
    int  h;
    int  bpp;                                  /* всегда 4 (EGA) */
    int  px[SPR_MAX_WIDTH * SPR_MAX_HEIGHT];   /* 0..15 EGA-индекс или -1 = прозрачный */
    bool valid;
};

int sprites_load_cache(const char* path);
int sprites_count();
void sprites_decode(int idx, DecodedSprite& out);
int sprites_find_by_id(int sprite_id);

} /* namespace karnov */

#endif
