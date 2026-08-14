/* level.h — загрузчик LEVEL%d.BIN.
 *
 * Формат описан в FORMATS.md + memory project_level0_format.md.
 * Парсер портирован из ../karnov_cpp/src/splash.cpp.
 */
#ifndef KARNOV_LEVEL_H
#define KARNOV_LEVEL_H

#include "karnov.h"

/* 12-байтная структура региона из LEVEL%d.BIN.
 * Декомпиляция респавн-логики (KARNOV.EXE file 0x10019) показала настоящую
 * семантику: bbox области + точка возрождения игрока внутри неё.
 * read_object_regions (хвост, file 0x51F0) копирует region[0].respawn_x/y
 * в шаблон игрока DS:0x480 — это стартовая позиция Карнова на уровне.
 * X-координаты — в 2px-единицах движка, Y — в пикселях. */
typedef struct {
    u16 x_start, y_start;
    u16 x_end,   y_end;
    u16 respawn_x, respawn_y;
} object_region_t;

/* 20-байтная структура спауна врага.
 * Семантика (check_triggers, file 0x5011): спаун срабатывает, когда
 * |player_x - cur_x| < range_x И |player_y - cur_y| < range_y (строго),
 * flags — one-shot защёлка (0 = взведён; 1 ставится при успешном спауне;
 * файл может содержать записи с preset flags=1 — «death-only» враги,
 * взводимые только rearm-циклом после смерти игрока).
 * trigger>>8 = HP, trigger&0xFF = init_arg. X — в 2px-единицах. */
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

/* Регион для респавна после смерти (условие из KARNOV.EXE file 0x1008D).
 * checkpoint_x_units — X в 2px-единицах, y_px — Y в пикселях. NULL только
 * если у уровня нет регионов вовсе. */
const object_region_t *level_region_for_respawn(int checkpoint_x_units,
                                                int y_px,
                                                int max_progress_units);

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

/* Тот же тест, но для горизонтального движения Karnov-а. Использует более
 * широкую маску (passes attrs 0..4 → floor/wall/ladder/sky/decoration),
 * блокирует только attr 5..9 — особо обозначенные obstacles. Это обеспечивает
 * one-way-platform поведение каменных столбов: Karnov стоит на верху
 * (через level_solid_at), но проходит сквозь них боком. */
bool level_blocks_walk_h_at(int wx, int wy);

/* Первая строка карты (>= start_row) в колонке пикселя wx, тайл которой
 * удовлетворяет маске свойств. Порт ядра постановки на землю KARNOV.EXE
 * (cs:0xD4B:0x14C): у обёртки 0x19F маска = props[attr 0] = 0x0001, у
 * варианта «по центру спрайта» 0x1DE — 0x0089. −1 = подходящей строки нет. */
int level_ground_row_below(int wx, int start_row, u16 mask);

/* Возвращает true если тайл в пикселе (wx, wy) — вода (attr=08). Karnov
 * погибает от контакта с водой (анимация утопления). LEVEL1 имеет
 * водные тайлы 0x84/0x8f/0x90/0x91/0x96. */
bool level_is_water_at(int wx, int wy);

/* Скан карты на «основной grounded-уровень»: row с наибольшим числом
 * solid-тайлов. Возвращает top_y этой строки в мировых пикселях.
 * Используется как fallback ground_y в physics — Karnov должен падать
 * до настоящей земли уровня, а не до жёстко зашитого LEVEL1-значения 290. */
int level_compute_ground_y(void);

#endif /* KARNOV_LEVEL_H */
