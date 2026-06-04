/* palette.c — палитры EGA + CGA + per-level subpal tables.
 *
 * EGA Karnov не задокументирована (исходники потеряны); подбираем
 * sub-палитры визуально под DOSBox-капчуры:
 *
 *   LEVEL0 (KARNOV title)  — /tmp/karnov_dos/capture/before_debug.png
 *     attr=0  на верхних строках букв KARNOV: pixel0=Black, pixel2=Yellow,
 *             pixel3=White (чёрный фон + жёлтый/белый текст).
 *     attr=2  на нижних строках букв + горы/драконы: pixel0=Black,
 *             pixel1=Gray, pixel2=Yellow, pixel3=White.
 *
 *   LEVEL1  (game) — /tmp/dos_l1/L1_F0400.png
 *     attr=0  на полу/кирпичных колоннах: pixel0=Black, pixel2=Brown
 *             (кирпич), pixel3=Yellow (highlight). Floor row (29) полностью
 *             затемнён orange/brown.
 *     attr=2  на небе/горах: pixel0=LightBlue (sky), pixel1=White (snow dither),
 *             pixel2=BrightBlue, pixel3=White.
 *
 * Per-level switching: palette_set_level(level_num) выбирает таблицу.
 */
#include "palette.h"

const u32 CGA_PAL_1H[4] = {
    0xFF000000u, 0xFF55FFFFu, 0xFFFF55FFu, 0xFFFFFFFFu,
};

const u32 EGA_PAL16[16] = {
    0xFF000000u, 0xFF0000AAu, 0xFF00AA00u, 0xFF00AAAAu,
    0xFFAA0000u, 0xFFAA00AAu, 0xFFAA5500u, 0xFFAAAAAAu,
    0xFF555555u, 0xFF5555FFu, 0xFF55FF55u, 0xFF55FFFFu,
    0xFFFF5555u, 0xFFFF55FFu, 0xFFFFFF55u, 0xFFFFFFFFu,
};

/* ─── LEVEL0 (splash) sub-palettes ─────────────────────────────────────
 *
 * attr=0: верхняя срезка букв KARNOV — pixel 2 жёлтый, pixel 3 белый.
 * attr=2: нижние строки букв + горы/драконы. Pixel 1 идёт на серый.
 */
static const u8 L0_SUBPAL_ATTR0[4] = { 0x0, 0x6, 0xE, 0xF };  /* Black, Brown, Yellow, White */
static const u8 L0_SUBPAL_ATTR2[4] = { 0x0, 0x7, 0xE, 0xF };  /* Black, Gray, Yellow, White */

/* ─── LEVEL1 sub-palettes ──────────────────────────────────────────────
 *
 * Подбор визуально по DOSBox capture L1_F0400.png:
 *   sky (rows 0..23, tile_id 0, attr=0): pixel 0 = LightBlue (sky).
 *   mountains (rows 24..27, attr=2, tile 8/9/23): pixel 0 = LightBlue,
 *     pixel 1 = White (dither для snow caps), pixel 2/3 = BrightBlue/White.
 *   pillars (rows 26..28, attr=0, tile 5/6/7): pixel 0 = Black,
 *     pixel 2 = Brown (brick), pixel 3 = Yellow (highlight).
 *   floor (row 29, attr=0, tile 1): pixel 2 = Brown, pixel 3 = Yellow.
 *
 * Это решает конфликт: attr=0 одна и та же на «небе» (tile 0 all-zeros) и
 * «полу» (tile 1, smile-style pixels). Для tile 0 pixel 0 = LightBlue даёт
 * sky-blue фон. Для tile 1, преимущественно pixel 2/3 → коричневый/жёлтый.
 */
static const u8 L1_SUBPAL_ATTR0[4] = { 0x9, 0x6, 0xE, 0xF };  /* LightBlue, Brown, Yellow, White */
static const u8 L1_SUBPAL_ATTR2[4] = { 0x9, 0xF, 0xB, 0xF };  /* LightBlue, White, LightCyan, White */
/* attr 3/4/7/8 — редкие тайлы в LEVEL1 (≤37 штук каждый). Используем
 * наклон ATTR2 (мягко-голубая палитра). Если будет резкое различие,
 * подкорректируем по визуальной верификации. */
static const u8 L1_SUBPAL_ATTRX[4] = { 0x9, 0xF, 0xE, 0xF };

/* Active table set by palette_set_level. */
static const u8 *g_attr0 = L0_SUBPAL_ATTR0;
static const u8 *g_attr2 = L0_SUBPAL_ATTR2;
static const u8 *g_attrX = L0_SUBPAL_ATTR2;

void palette_set_level(u16 level_num)
{
    if (level_num == 0) {
        g_attr0 = L0_SUBPAL_ATTR0;
        g_attr2 = L0_SUBPAL_ATTR2;
        g_attrX = L0_SUBPAL_ATTR2;
    } else {
        /* LEVEL1+: используем blue-sky табличку. Дальнейшие уровни могут
         * получить свои таблицы при добавлении их в этот файл. */
        g_attr0 = L1_SUBPAL_ATTR0;
        g_attr2 = L1_SUBPAL_ATTR2;
        g_attrX = L1_SUBPAL_ATTRX;
    }
}

u32 palette_resolve_ega(u8 attr, u8 pixel_idx)
{
    const u8 *sub;
    if (attr == 0)      sub = g_attr0;
    else if (attr == 2) sub = g_attr2;
    else                sub = g_attrX;
    return EGA_PAL16[sub[pixel_idx & 3]];
}
