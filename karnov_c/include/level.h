/* level.h — загрузчик LEVEL%d.BIN.
 *
 * Формат описан в FORMATS.md + memory project_level0_format.md.
 * Парсер портирован из ../karnov_cpp/src/splash.cpp.
 */
#ifndef KARNOV_LEVEL_H
#define KARNOV_LEVEL_H

#include "karnov.h"

/* 12-байтная структура из LEVEL%d.BIN (см. FORMATS.md §3).
 * (x0,y0)–(x1,y1) — bbox области в world-пикселях; (vx,vy) — скорость скролла
 * или связанная точка (start camera position для региона). */
typedef struct {
    u16 x0, y0;
    u16 x1, y1;
    u16 vx, vy;
} object_region_t;

/* 20-байтная структура спауна врага. */
typedef struct {
    u16 type, subtype;
    u16 cur_x, cur_y;
    u16 range_x, range_y;
    u16 spawn_x, spawn_y;
    u16 trigger, flags;
} enemy_spawn_t;

#define LEVEL_MAX_OBJECTS  16
#define LEVEL_MAX_ENEMIES  256

extern object_region_t level_objects[LEVEL_MAX_OBJECTS];
extern enemy_spawn_t   level_enemies[LEVEL_MAX_ENEMIES];
extern u16             level_num_objects;
extern u16             level_num_enemies;

extern u8  level_tile_gfx[MAX_TILE_GFX][TILE_BYTES];
/* 4 bpp packed EGA-вариант той же tile-графики (заполняется из 80-байт-
 * ного блока на диске). Каждый байт = 2 пикселя × 4 bit EGA-индекс. */
extern u8  level_tile_gfx_ega[MAX_TILE_GFX][TILE_BYTES_EGA];
extern u8  level_tile_attr[MAX_TILE_GFX];
extern u16 level_num_tile_graphics;
extern u16 level_num_map_rows;
extern u16 level_num_map_cols;
extern u8  level_map[MAX_MAP_ROWS][MAX_MAP_COLS];

/* Загружает level_num (0..9). Возвращает 0 = OK, иначе код ошибки. */
int level_load(u16 level_num);

/* Запрос «есть ли твёрдый тайл в мире на пикселе (wx, wy)?».
 * Использует level_tile_attr[tile_id] & 1 — байт-верифицированный
 * критерий «solid tile» из KARNOV.EXE AI handler cs:0xFFA:0x1218
 * (ai_scripts.cpp:828):
 *   bx = ax * 2
 *   if ([bx + 0x84] & 1) == 0: return    // не «твёрдый» тайл
 * где [bx + 0x84] — tile_attr[tile_id] (u16-индексация).
 *
 * Для LEVEL0 (splash) attr ∈ {0,2} → нет solid'ов; для LEVEL1+
 * только specific платформы (attr ∈ {0x03, 0x07} в LEVEL1, 8 тайлов).
 * Возвращает false при out-of-bounds. */
bool level_solid_at(int wx, int wy);

/* Скан карты на «основной grounded-уровень»: row с наибольшим числом
 * solid-тайлов. Возвращает top_y этой строки в мировых пикселях.
 * Используется как fallback ground_y в physics — Karnov должен падать
 * до настоящей земли уровня, а не до жёстко зашитого LEVEL1-значения 290. */
int level_compute_ground_y(void);

#endif /* KARNOV_LEVEL_H */
