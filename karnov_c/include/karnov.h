/* karnov.h — общие типы и константы для karnov_c (C99 порт).
 *
 * Karnov, Data East / Quicksilver, 1987. Оригинал собран Turbo-C compact
 * model для 8086. Реверс-инжиниринг см. ../karnov_cpp/DECOMPILE_NOTES.md.
 */
#ifndef KARNOV_H
#define KARNOV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

/* Окно рендера: 320 × 144 — оригинальная игровая область (без HUD-полосы
 * статуса). HUD занимает дополнительные 24 строки внизу. */
#define VIEW_W   320
#define VIEW_H   144
#define HUD_H    24
#define WIN_W    VIEW_W
#define WIN_H    (VIEW_H + HUD_H)

/* Размер тайла в пикселях. На диске у каждого тайла ДВЕ копии графики:
 *   - 40 байт: 2 bpp packed (для CGA/attr-based EGA).
 *   - 80 байт: 4 bpp packed (1 nibble = 1 EGA индекс 0..15, прямой).
 * EGA-режим использует 4 bpp вариант для точного match'а с оригиналом
 * (DOSBox показывает разные цвета на attr=2 для гор vs кирпич — это
 * возможно только при per-pixel EGA-индексе, не attr-based palette). */
#define TILE_W   16
#define TILE_H   10
#define TILE_BYTES 40
#define TILE_BYTES_EGA 80

/* Лимиты карты. Хранилищу хватает с запасом для самого широкого уровня.
 * Реальные размеры: LEVEL0 = 12×20, LEVEL3 = 64×208, LEVEL9 = 63×247. */
#define MAX_TILE_GFX 512
#define MAX_MAP_ROWS  80
#define MAX_MAP_COLS  256

/* Биты input_state (соответствуют KARNOV.EXE bitfield для ввода). */
#define INP_R     0x01
#define INP_L     0x02
#define INP_D     0x04
#define INP_U     0x08
#define INP_FIRE  0x10
#define INP_OPT   0x20
/* INP_START — Enter/Return; используется ТОЛЬКО для UI-переходов
 * (splash→LEVEL1, skip loading). НЕ часть оригинальной KARNOV.EXE
 * input-bitfield (бит 0x40 в оригинале не задействован). Демо никогда
 * не выставляет этот бит — он только от клавиатуры. */
#define INP_START 0x40

/* Режимы видео (level_mode в оригинале, DS:0x0360). */
typedef enum {
    VIDEO_CGA = 0,   /* level_mode 0, sprites.cga */
    VIDEO_EGA = 1    /* level_mode 1, sprites.ega */
} video_mode_t;

#endif /* KARNOV_H */
