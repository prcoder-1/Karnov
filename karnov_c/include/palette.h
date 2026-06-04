/* palette.h — палитры EGA + CGA, attr-driven sub-палитры.
 *
 * Реконструировано визуально из DOSBox titlescreen capture
 * (/tmp/karnov_dos/capture/before_debug.png) и LEVEL1 capture
 * (/tmp/dos_l1/L1_F0400.png).
 *
 * Палитры зависят от уровня: один и тот же tile_attr на LEVEL0 и
 * LEVEL1 даёт разные цвета (LEVEL0 attr=0 = чёрный фон букв KARNOV,
 * LEVEL1 attr=0 = синее небо). Перед рендером уровня вызывается
 * palette_set_level(level_num).
 */
#ifndef KARNOV_PALETTE_H
#define KARNOV_PALETTE_H

#include "karnov.h"

/* IBM CGA palette 1 high-intensity (mode 4). */
extern const u32 CGA_PAL_1H[4];

/* Полная EGA-палитра — 16 цветов 6-bit RGB. */
extern const u32 EGA_PAL16[16];

/* Переключает активную таблицу attr→subpal под конкретный уровень.
 * Вызывается из prepare_level() в game.c. */
void palette_set_level(u16 level_num);

/* Преобразование (attr, 2-битный pixel) → ARGB, используя текущую
 * активную палитру (см. palette_set_level). */
u32 palette_resolve_ega(u8 attr, u8 pixel_idx);

#endif /* KARNOV_PALETTE_H */
