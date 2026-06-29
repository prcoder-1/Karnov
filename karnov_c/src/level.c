/* level.c — загрузчик LEVEL%d.BIN.
 *
 * Порт KARNOV.EXE:
 *   file 0x5350   load_level
 *   file 0x51B6   read_object_regions
 *   file 0x5216   read_enemies
 *   file 0x4076   draw_tile (формат 4×10 байт = 16×10 px @ 2bpp)
 */
#include "level.h"
#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

object_region_t level_objects[LEVEL_MAX_OBJECTS];
enemy_spawn_t   level_enemies[LEVEL_MAX_ENEMIES];
u16             level_num_objects = 0;
u16             level_num_enemies = 0;

u8  level_tile_gfx[MAX_TILE_GFX][TILE_BYTES];
u8  level_tile_gfx_ega[MAX_TILE_GFX][TILE_BYTES_EGA];
u8  level_tile_attr[MAX_TILE_GFX];
u16 level_num_tile_graphics = 0;
u16 level_num_map_rows = 0;
u16 level_num_map_cols = 0;
u8  level_map[MAX_MAP_ROWS][MAX_MAP_COLS];

/* Возможные пути к data — пробуем относительные от cwd, затем data/. */
static const char *DATA_DIRS[] = { "", "data/", "../" };

static FILE *try_open(const char *filename)
{
    char buf[256];
    for (size_t i = 0; i < sizeof(DATA_DIRS) / sizeof(DATA_DIRS[0]); ++i) {
        snprintf(buf, sizeof(buf), "%s%s", DATA_DIRS[i], filename);
        FILE *fp = bin_fopen(buf, "rb");
        if (fp) return fp;
    }
    return NULL;
}

static int read_object_regions(FILE *fp)
{
    if (bin_fread(&level_num_objects, 2, 1, fp) != 1) return 1;
    if (level_num_objects > LEVEL_MAX_OBJECTS) level_num_objects = LEVEL_MAX_OBJECTS;
    if (level_num_objects == 0) return 0;
    if (bin_fread(level_objects, sizeof(object_region_t),
                  level_num_objects, fp) != level_num_objects) return 1;
    return 0;
}

static int read_enemies(FILE *fp)
{
    if (bin_fread(&level_num_enemies, 2, 1, fp) != 1) return 1;
    if (level_num_enemies > LEVEL_MAX_ENEMIES) level_num_enemies = LEVEL_MAX_ENEMIES;
    if (level_num_enemies == 0) return 0;
    if (bin_fread(level_enemies, sizeof(enemy_spawn_t),
                  level_num_enemies, fp) != level_num_enemies) return 1;
    return 0;
}

int level_load(u16 level_num)
{
    char filename[32];
    snprintf(filename, sizeof(filename), "level%u.bin", level_num);

    FILE *fp = try_open(filename);
    if (!fp) {
        fprintf(stderr, "level_load: не могу открыть %s\n", filename);
        return 1;
    }
    /* Оригинал переоткрывает файл для сброса RLE-state. */
    fclose(fp);
    fp = try_open(filename);
    if (!fp) return 1;

    if (read_object_regions(fp)) { fclose(fp); return 0x0B; }
    if (read_enemies(fp))        { fclose(fp); return 0x15; }

    if (bin_fread(&level_num_tile_graphics, 2, 1, fp) != 1) { fclose(fp); return 0x1F; }
    if (bin_fread(&level_num_map_rows,      2, 1, fp) != 1) { fclose(fp); return 0x29; }
    if (bin_fread(&level_num_map_cols,      2, 1, fp) != 1) { fclose(fp); return 0x33; }

    if (level_num_tile_graphics > MAX_TILE_GFX) level_num_tile_graphics = MAX_TILE_GFX;
    if (level_num_map_rows > MAX_MAP_ROWS) level_num_map_rows = MAX_MAP_ROWS;
    if (level_num_map_cols > MAX_MAP_COLS) level_num_map_cols = MAX_MAP_COLS;

    for (u16 i = 0; i < level_num_tile_graphics; ++i) {
        u8 tile_idx = 0, attr = 0;
        if (bin_fread(&tile_idx, 1, 1, fp) != 1) { fclose(fp); return 0x3D; }
        if (bin_fread(&attr,     1, 1, fp) != 1) { fclose(fp); return 0x47; }
        level_tile_attr[tile_idx] = attr;
        if (bin_fread(level_tile_gfx[tile_idx], TILE_BYTES, 1, fp) != 1) {
            fclose(fp);
            return 0x51;
        }
        /* 80 bytes = 4 bpp packed EGA pixel data. Сохраняем в отдельный
         * буфер для EGA-рендера (см. render.c::render_blit_tile_ega). */
        if (bin_fread(level_tile_gfx_ega[tile_idx], TILE_BYTES_EGA, 1, fp) != 1) {
            fclose(fp);
            return 0x52;
        }
    }

    size_t mapsz = (size_t)level_num_map_rows * level_num_map_cols;
    u8 flat[MAX_MAP_ROWS * MAX_MAP_COLS];
    if (bin_fread(flat, mapsz, 1, fp) != 1) { fclose(fp); return 0x65; }
    for (u16 r = 0; r < level_num_map_rows; ++r) {
        memcpy(level_map[r], flat + r * level_num_map_cols, level_num_map_cols);
    }

    fclose(fp);
    memset(level_tile_gfx[0], 0, TILE_BYTES);          /* file 0x57C5. */
    memset(level_tile_gfx_ega[0], 0, TILE_BYTES_EGA);  /* sky/background = EGA index 0 (палитра переназначает на нужный цвет). */
    /* tile_id 0 ≡ «пустота» (sky / интерьер дверных проёмов). В файле LEVEL%
     * этот тайл НЕ загружается, поэтому attr остался бы по умолчанию 0
     * (= standable floor через LUT). KARNOV.EXE @ file 0x36FE инициализирует
     * `level_tile_attr[0] = 2` (категория «sky/passable»), чтобы Karnov
     * проходил сквозь пустые тайлы и не «стоял» на чёрных дверных проёмах. */
    level_tile_attr[0] = 2;

    return 0;
}

/* Per-tile attr → 16-bit "props" bitmask. Байт-верифицированная таблица из
 * KARNOV.EXE @ DS:[0x84] (file 0x1F214, 10 WORDs). Каждый attr 0..9 — это
 * КАТЕГОРИЯ тайла; LUT отображает её в битовый набор свойств:
 *
 *   attr 0 → 0x0001  bit 0  (standable floor — sand, dragon platform,
 *                            yellow column tops, тайлы 1/5/6/7/10/11/...)
 *   attr 1 → 0x0002  bit 1  (ladder / climb)
 *   attr 2 → 0x0004  bit 2  (sky / background — passable)
 *   attr 3 → 0x0008  bit 3  (wall — rocks/grey blocks)
 *   attr 4 → 0x0009  bits 0+3  (floor + wall — decorations с твёрдой кромкой)
 *   attr 5..9 → 0x0020..0x0200  (специальные категории — стены/декорации)
 *
 * Оригинальный probe_tile (cs:0xD4B:0x564, file 0xF214) проверяет
 *   ax = props AND mask;  cmp ax, props;  jnz collision
 * — коллизия, если у тайла есть бит ВНЕ маски «прохождения» объекта.
 * Маска per-object из obj.struct+0x08 (двойная косвенность через DS:[0xD8D2]).
 *
 * Маска для Karnov-WALK дотрассирована: struct @ DS:[0x1580]+0x08 = 0x0006
 * (биты 1+2 = «лестница + небо»). Сетится в state-dispatch функции @ file
 * 0x10FBC. Karnov проходит сквозь тайлы, у которых props ⊆ {bit1, bit2};
 * остальные категории (0, 3..9) — solid. Прочие state-структуры:
 *   DS:[0x1510] mask 0x0081 — spawn/respawn (passes attrs 0, 7)
 *   DS:[0x151A] mask 0x00C0 — некий fall/jump?
 *   DS:[0x1546] mask 0xFFFF — ghost / spawn-FX (всё проходит)
 *   DS:[0x15B6] mask 0x0084 — crouch/climb-вариант
 *   DS:[0x164A] mask 0x0200 — bird/fly?
 *   DS:[0x169A] mask 0x038E — death/animation? */
static const u16 LEVEL_ATTR_TO_PROPS[10] = {
    0x0001, 0x0002, 0x0004, 0x0008, 0x0009,
    0x0020, 0x0040, 0x0080, 0x0100, 0x0200,
};
#define KARNOV_WALK_MASK  0x0006   /* DS:[0x1580]+0x08 — нормальная ходьба */

bool level_solid_at(int wx, int wy)
{
    if (wx < 0 || wy < 0) return false;
    int col = wx / TILE_W;
    int row = wy / TILE_H;
    if (col >= (int)level_num_map_cols) return false;
    if (row >= (int)level_num_map_rows) return false;
    u8 tile_id = level_map[row][col];
    u8 attr    = level_tile_attr[tile_id];
    /* Категория вне таблицы — трактуем как solid (defensive: незнакомый
     * тайл не должен пропускать Karnov-а сквозь себя). */
    if (attr >= 10) return true;
    u16 props = LEVEL_ATTR_TO_PROPS[attr];
    /* probe_tile-эквивалент: коллизия, если props НЕ подмножество mask. */
    return (props & KARNOV_WALK_MASK) != props;
}

/* Горизонтальная маска: passes bits 0..4 (= attrs 0..4: floor, ladder, sky,
 * wall, floor+wall), блокирует bits 5..9 (special obstacles). Это
 * one-way-platform поведение: каменные столбы (attr 3/4) являются
 * landable-полом для find_floor_y, но прозрачны для бокового движения.
 *
 * 0x001F = bits 0+1+2+3+4 = 0x01|0x02|0x04|0x08|0x10. (attr 4 props=0x0009
 * полностью покрыт 0x000F, не требует bit 4; bit 4 включён за-доп.) */
#define KARNOV_WALK_H_MASK  0x001F

bool level_blocks_walk_h_at(int wx, int wy)
{
    if (wx < 0 || wy < 0) return false;
    int col = wx / TILE_W;
    int row = wy / TILE_H;
    if (col >= (int)level_num_map_cols) return false;
    if (row >= (int)level_num_map_rows) return false;
    u8 tile_id = level_map[row][col];
    u8 attr    = level_tile_attr[tile_id];
    if (attr >= 10) return true;
    u16 props = LEVEL_ATTR_TO_PROPS[attr];
    return (props & KARNOV_WALK_H_MASK) != props;
}

bool level_is_water_at(int wx, int wy)
{
    if (wx < 0 || wy < 0) return false;
    int col = wx / TILE_W;
    int row = wy / TILE_H;
    if (col >= (int)level_num_map_cols) return false;
    if (row >= (int)level_num_map_rows) return false;
    /* attr=08 = вода (категория из LUT props=0x0100). LEVEL1 tile_ids
     * 0x84/0x8f/0x90/0x91/0x96 — всё это синяя вода. */
    return level_tile_attr[level_map[row][col]] == 0x08;
}

int level_compute_ground_y(void)
{
    /* Найти row с максимумом solid-тайлов (= основной grounded-floor
     * уровня). Используется как fallback ground_y, чтобы Karnov падал
     * до настоящей земли — LEVEL1 row 29 (sand y=290), LEVEL2 row 31
     * (y=310), LEVEL3 row 40 (y=400), и т.д. */
    int max_solid = -1;
    int ground_row = (int)level_num_map_rows - 1;  /* default: bottom row */
    for (int r = 0; r < (int)level_num_map_rows; ++r) {
        int solid_count = 0;
        for (int c = 0; c < (int)level_num_map_cols; ++c) {
            u8 attr = level_tile_attr[level_map[r][c]];
            if (attr < 10 &&
                (LEVEL_ATTR_TO_PROPS[attr] & KARNOV_WALK_MASK) != LEVEL_ATTR_TO_PROPS[attr]) {
                ++solid_count;
            }
        }
        if (solid_count > max_solid) {
            max_solid = solid_count;
            ground_row = r;
        }
    }
    return ground_row * TILE_H;
}
