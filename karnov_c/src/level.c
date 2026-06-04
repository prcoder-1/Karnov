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

/* Группировка «кластеров» из LEVEL%d.BIN — серии последовательных записей
 * с одинаковым type, близкими cur_x (≤32 px) и cur_y (Δ≤16 px). В оригинале
 * это WAVE-SPAWN slots: первый враг активен, при гибели спавнится следующий
 * (state_list 0x1880 для type 17). У нас head активен, member'ы помечаются
 * flags|=0x100 и не спавнятся отдельно.
 *
 * `trigger` НЕ требуется одинаковым: данные LEVEL1 иногда имеют trg=2560/2570
 * у соседних entries того же типа (например type 10 enemy[4,5]) — это всё
 * равно один визуальный объект.
 *
 * cur_x ПЕРВОЙ записи переписывается на MAX кластера (правый край) —
 * это совпадает с визуальной серединой decoration'а (cluster 488..520 →
 * cur_x=520, sprite #88 body_cx=20 → визуальный центр sprite-а ~524
 * = центр каменной колонны @ 544..572 LEVEL1).
 *
 * O(N²) на N≤256 — приемлемо. */
/* Возвращает tile id в позиции (wx, wy). 0 если out of bounds. */
static u8 tile_at(int wx, int wy)
{
    int col = wx / TILE_W;
    int row = wy / TILE_H;
    if (col < 0 || col >= (int)level_num_map_cols) return 0;
    if (row < 0 || row >= (int)level_num_map_rows) return 0;
    return level_map[row][col];
}

/* Решает, является ли тайл «декорацией» — видимым тайлом NON-BG,
 * NON-SAND, NON-SOLID. В LEVEL1 декорации над solid платформой (cyan
 * stone top) имеют attr=0x04. */
static bool is_decoration_tile(u8 t)
{
    u8 a = level_tile_attr[t];
    if (a & 1) return false;             /* solid — нет */
    if (t == 23 || t == 1 || t == 0) return false; /* sky/sand/empty */
    return (a & 4) != 0;
}

/* Находит solid платформу с TOP=ry рядом с rx (exact-y match), затем
 * сдвигает out_y вверх на каждый ряд декорации над solid'ом (visual top
 * каменной колонны включает cyan decoration tile).
 *
 * Для LEVEL1 enemy[20] (rx=520, ry=270, radius=64):
 *   RIGHT solid 544..575 top=270 — matches → x_center=559.
 *   Row above (y=260..269) col 34-35: tile 130/131 attr=0x04 — decoration
 *   → out_y = 270 - 10 = 260 (visual top of cyan column).
 *   LEFT (top=280) НЕ matches т.к. exact ry=270. */
static bool find_platform_center(int rx, int ry, int search_radius,
                                  int *out_x, int *out_y)
{
    int best_dx = 1<<30;
    int best_x = -1;
    for (int dx = -search_radius; dx <= search_radius; ++dx) {
        int wx = rx + dx;
        if (wx < 0) continue;
        if (level_solid_at(wx, ry + 1) && !level_solid_at(wx, ry - 1)) {
            int ad = dx < 0 ? -dx : dx;
            if (ad < best_dx) { best_dx = ad; best_x = wx; }
        }
    }
    if (best_x < 0) return false;

    /* Расширяем сегмент на y=ry. */
    int seg_start = best_x, seg_end = best_x;
    while (seg_start > 0 &&
           level_solid_at(seg_start - 1, ry + 1) &&
           !level_solid_at(seg_start - 1, ry - 1)) --seg_start;
    while (level_solid_at(seg_end + 1, ry + 1) &&
           !level_solid_at(seg_end + 1, ry - 1)) ++seg_end;

    int center = (seg_start + seg_end) / 2;
    int top = ry;

    /* Подъём по декорации: проверяем каждый row выше solid_top, если
     * tile в КОЛОНКЕ center декорация (attr&4 и не sky/sand), сдвигаем
     * top на 10 px вверх. Останавливаемся при первом не-decoration row. */
    while (top >= TILE_H) {
        u8 t_above = tile_at(center, top - TILE_H);
        if (!is_decoration_tile(t_above)) break;
        top -= TILE_H;
    }

    *out_x = center;
    *out_y = top;
    return true;
}

static void group_enemy_clusters(void)
{
    for (u16 i = 0; i < level_num_enemies; ++i) {
        enemy_spawn_t *e = &level_enemies[i];
        if (e->type == 0) continue;
        if (e->flags & 0x0100) continue;   /* уже member — пропускаем */

        int max_x = e->cur_x;
        for (u16 j = i + 1; j < level_num_enemies; ++j) {
            enemy_spawn_t *m = &level_enemies[j];
            if (m->type != e->type)         break;
            int dy = (int)m->cur_y - (int)e->cur_y;
            if (dy < 0) dy = -dy;
            if (dy > 16) break;
            int dx = (int)m->cur_x - (int)e->cur_x;
            if (dx < 0) dx = -dx;
            if (dx > 32) break;
            m->flags |= 0x0100;
            if ((int)m->cur_x > max_x) max_x = m->cur_x;
        }
        e->cur_x = (u16)max_x;

        /* Snap к solid платформе с EXACT top=cur_y, затем подъём cur_y на
         * декорацию (cyan stone top tile) если есть. cur_y из данных (270
         * для LEVEL1 enemy[20]) совпадает с RIGHT solid top, после
         * decoration-walkup → cur_y=260 (visual top цианового верха колонны).
         *
         * Если solid'а нет — type 17 entry подавляем (cluster overflow). */
        if (e->type == 17 && (int)e->cur_y < 290) {
            int sx = 0, sy = 0;
            if (find_platform_center((int)e->cur_x, (int)e->cur_y, 64, &sx, &sy)) {
                e->cur_x = (u16)sx;
                e->cur_y = (u16)sy;
            } else {
                e->type = 0;
            }
        }
    }
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

    /* После загрузки tile-данных доступен level_solid_at — теперь можно
     * группировать кластеры и snap'ить type 17 head'ов к solid платформам. */
    group_enemy_clusters();
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
