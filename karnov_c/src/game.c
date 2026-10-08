/* game.c — splash_run: оркестровка LEVEL%d.BIN + attract demo + аудио. */

#include "game.h"
#include "level.h"
#include "ai.h"
#include "audio.h"
#include "demo.h"
#include "file_io.h"
#include "input.h"
#include "inventory.h"
#include "palette.h"
#include "physics.h"
#include "render.h"
#include "sprite.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

/* ─── Sprite cache loading helpers ─────────────────────────────────── */

static int try_load_sprites(const char *sprite_file)
{
    char path[4096];
    const char *prefixes[] = { "", "data/", "../data/", NULL };
    for (int i = 0; prefixes[i]; ++i) {
        snprintf(path, sizeof(path), "%s%s", prefixes[i], sprite_file);
        if (sprite_cache_load(path) == 0) return 0;
    }
    /* Search exe directory */
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            const char *rel_list[] = { "/data/", "/../data/", NULL };
            for (int i = 0; rel_list[i]; ++i) {
                /* exe/data/... и exe/../data/... — двух вариантов достаточно.
                 * (Раньше здесь был третий snprintf, результат которого тут же
                 * затирался следующей строкой, плюс -Wformat-truncation.) */
                snprintf(path, sizeof(path), "%.2000s%s%s", exe, rel_list[i], sprite_file);
                if (sprite_cache_load(path) == 0) return 0;
            }
        }
    }
    return 1;
}

/* ─── Brown dragon at LEVEL1 start ─────────────────────────────────── */

#define LEVEL1_DRAGON_X        0      /* world x, левый край */
#define LEVEL1_DRAGON_BOT_Y  260      /* world y, низ туловища = stone platform */
#define LEVEL1_DRAGON_UPPER  145      /* upper sprite */
#define LEVEL1_DRAGON_LOWER  146      /* lower sprite */

static void draw_level1_dragon(int scroll_x, int scroll_y)
{
    const sprite_t *up = sprite_find(LEVEL1_DRAGON_UPPER);
    const sprite_t *lo = sprite_find(LEVEL1_DRAGON_LOWER);
    if (!up || !lo) return;
    int lo_top = LEVEL1_DRAGON_BOT_Y - lo->h;
    int up_top = lo_top - up->h;
    const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
    u8 ck = sprite_is_cga() ? 0xFF : 1;

    /* Обе половины якорятся левым краем — как и любой спрайт (кэш больше
     * не зеркальный, выравнивание по body_cx не нужно). */
    int x0_lower = LEVEL1_DRAGON_X - scroll_x;
    int x0_upper = LEVEL1_DRAGON_X - scroll_x;

    const sprite_t *parts[2] = { up, lo };
    int tops[2] = { up_top, lo_top };
    int x0s[2]  = { x0_upper, x0_lower };
    for (int p = 0; p < 2; ++p) {
        const sprite_t *s = parts[p];
        int x0 = x0s[p];
        int y0 = tops[p] - scroll_y;
        for (int r = 0; r < s->h; ++r) {
            int yy = y0 + r;
            if (yy < 0 || yy >= WIN_H) continue;
            for (int c = 0; c < s->w; ++c) {
                u8 v = s->pixels[r * s->w + c];
                if (v == 0xFF) continue;
                if (v == ck) continue;
                int xx = x0 + c;
                if (xx < 0 || xx >= WIN_W) continue;
                u32 col = sprite_is_cga() ? pal[v & 3] : pal[v & 15];
                render_framebuffer()[yy * WIN_W + xx] = col;
            }
        }
    }
}

/* ─── Scroll ───────────────────────────────────────────────────────── */

static int g_scroll_x = 0;
static int g_scroll_y = 0;

/* ─── Level-end (взятие свитка) ────────────────────────────────────────
 * KARNOV.EXE type56 (ТРИГГЕР ЗАВЕРШЕНИЯ УРОВНЯ, cs:0x17EC:0x03B2, file
 * 0x19A72): при подборе свитка-сокровища вызывает play_sound(9) +
 * level-completion jingle (music track 2 = DS:0x06FC), проигрывает
 * анимацию (Karnov садится), затем переходит на следующий уровень.
 *
 * Первая фаза байт-верифицирована (2026-09), file 0x19BD4-0x19BE7:
 *   mov word[es:bx+0xc7],0                ; frame_ctr := 0 (сброс при входе)
 *   ...
 *   cmp word[es:bx+0xc7],byte 0x32 ; jg → следующая подфаза
 *                                  ; else jmp (остаться, ждать ещё)
 * т.е. подфаза длится, пока frame_ctr НЕ станет СТРОГО БОЛЬШЕ 50 — ровно
 * 51 кадр (не «~50+» на глаз). После этого — ЕЩЁ несколько подфаз
 * (file 0x19BF4..0x19C88), которые ждут не по счётчику кадров, а пока
 * frame_idx (+0xC5) анимации сидения дойдёт до конца соответствующего
 * AnimScript (таблицы DS:0x1F34/0x1F48/0x1F62), и в конце — случайный
 * выбор 1-из-3 (call cs:0x1ce7:0x17, div 3) перед удалением триггер-
 * объекта. Эти дальнейшие подфазы НЕ портированы байт-в-байт (нужны
 * данные AnimScript-таблиц сидения и семантика RNG-вызова) — порт
 * приближает их через «ждать окончания jingle», что по духу совпадает
 * (тоже «жди пока не доиграет анимация/музыка»), но не по кадру. Здесь:
 * запускаем jingle, кормим Karnov'у INP_D (он садится в crouch-позу
 * #103/#111) и держим сцену пока не доиграет jingle, но не короче
 * LEVEL_END_MIN_TICKS (чтобы посадка была видна). */
static bool s_level_end       = false;
/* Level select: нажата «L» при активном чите — ждём цифру (file 0x3DB0). */
static bool s_level_select_armed = false;
static u16  s_level_end_start = 0;
/* Максимальный достигнутый прогресс игрока по X (в 2px-единицах) —
 * аналог DS:0xD8CC; участвует в выборе региона респауна после смерти. */
static int  s_max_progress_units = 0;
/* Жизни Karnov'а. Байт-верифицировано (2026-09), инициализация DS:0xCF4E:
 *   file 0x6A37: mov word[0xcf50],1
 *   file 0x6A3D: cmp word[0xcdec],0 ; jz 0x6A66     («in_game» ещё 0 при
 *                                                     старте новой игры)
 *   file 0x6A66: mov ax,[0x35a] ; add [0xcf50],ax   ; DS:0x35A = 1 (файл,
 *                                                     initialized data)
 *   file 0x6B17-0x6B1A: mov ax,[0xcf50] ; mov [0xcf4e],ax
 * т.е. DS:0xCF50 = 1 + [0x35A](=1) = 2, копируется в DS:0xCF4E (lives).
 * game-over — когда DS:0xCF4E ДОХОДИТ до 0 после декремента при смерти
 * (file 0x6821/0x68D7 — dec word[0xcf4e]; file 0x6825 — cmp …,0). Раз
 * CF4E считает «жизни СВЕРХ текущей», 2 = ровно manual'ьное «three
 * lives» (1 текущая + 2 про запас) — прежнее значение 4 (5 суммарных)
 * было user-угадано неверно. */
#define KARNOV_START_LIVES 2    /* байт-верифицировано: DS:0xCF50 = 1+DS:0x35A(1) → DS:0xCF4E */
#define KARNOV_MAX_LIVES   9
#define LIFE_ICON_SPRITE   0x32 /* белый череп-иконка жизни */
static int  s_lives = KARNOV_START_LIVES;
static bool s_game_over = false;
#define LEVEL_END_MIN_TICKS 51    /* байт-верифицировано: frame_ctr > 0x32 (file 0x19BDF) */
#define LEVEL_END_MAX_TICKS 165   /* ~6 сек — потолок, чтобы не зависнуть если jingle не сигналит конец */

/* ─── Таймер уровня ─────────────────────────────────────────────────
 * Байт-верифицировано (2026-09): file 0x60BD (вызывается из главного
 * цикла file 0x6966) — трёхзначный счётчик-цифры [DS:0xD93A..0xD93C]
 * (по байту на разряд, не BCD), декремент на 1 раз в 16 кадров (throttle
 * `test [0xCDEA],0xF; jz`, [0xCDEA] — счётчик кадров, ++ в file 0x6990,
 * т.е. ровно аналог локальной переменной `frame`). Гейты: player_state
 * ([0xD8CE])==6 («уже гибнет») — не тикать вовсе; ==9 (invuln) — не
 * декрементировать и не убивать, но порог ниже всё равно проверяется.
 *
 * Сброс — file 0x1021E (та же level-init функция, что respawn/rearm):
 *   mov bx,[0xD8AA] (=номер уровня); shl bx,1; shl bx,1
 *   push [bx+0x14A8]; push [bx+0x14A6]; push 3; push ds; push 0xD93A
 *   call 0x41D:0x2BF   ; распаковать 32-бит BCD → 3 цифровых байта
 * Таблица DS:0x14A6 (10×4 байта, младшее слово = 3-цифровое BCD):
 *   L0=0x0999 L1=0x0151 L2=0x0251 L3=0x0301 L4=0x0231
 *   L5=0x0376 L6=0x0226 L7=0x0301 L8=0x0151 L9=0x0651
 * (т.е. в десятичном виде ровно эти же числа — совпадение цифр BCD).
 * [DS:0xD93D] («hurry»-флаг) сбрасывается в 0 там же (file 0x10237).
 *
 * Каждый throttled тик после декремента: если счётчик стал 000 —
 * far call cs:0xE62:0x6FE (file 0x60F7→0x1051E→0x104A3) — ТОТ ЖЕ
 * die_hit-путь, что у колонны голов (см. physics_kill_by_head_touch),
 * т.е. Карнов гибнет от истечения времени. Отдельно (не завязано на
 * итог декремента): если сотни==0 И десятки<3 (т.е. осталось <30
 * единиц) и [D93D] ещё не взведён — [D93D]=1, play_music(DS:0x0663,
 * prio 0, loop) — трек 1, идентифицированный пользователем по слуху как
 * «когда время заканчивается» (см. AUDIO.md §2/§7, там же мигающий
 * индикатор в HUD file 0x612A — glyph 0x3D в позиции (50,150), мигает
 * по биту 0x10 [0xCDEA], рядом с самими цифрами таймера (54,150,
 * glyph-база 0x3E, file 0x60FC/0x5DEA) — то, что предыдущий проход не
 * нашёл, ища ASCII "TIME": это отрисовка готовых спрайтов-цифр, не
 * текст). Порт не рисует оригинальные глиф-спрайты HUD (score/lives тоже
 * выведены отладочным текстом) — таймер и мигание добавлены в draw_hud
 * тем же текстовым способом. */
static const int LEVEL_TIMER_INIT[10] = {
    999, 151, 251, 301, 231, 376, 226, 301, 151, 651
};
static int  s_level_timer       = 0;
static bool s_level_timer_hurry = false;

/* once per 16 frames — прямой порт throttle-гейта file 0x60C0. */
static void level_timer_tick(u16 frame, bool audio_running)
{
    if (physics_is_dying()) return;             /* [0xD8CE]==6 */
    if ((frame & 0xF) != 0) return;
    if (!physics_is_invuln()) {                 /* [0xD8CE]!=9 */
        if (s_level_timer > 0) --s_level_timer;
        if (s_level_timer == 0) {
            physics_kill_by_head_touch();        /* cs:0xE62:0x6FE die_hit */
        }
    }
    if (!s_level_timer_hurry && s_level_timer < 30) {
        s_level_timer_hurry = true;
        if (audio_running) audio_play_music(1, /*loop=*/true);  /* track1 */
    }
}

static void update_scroll(int karnov_x, int karnov_y)
{
    int level_w_px = level_num_map_cols * TILE_W;
    int level_h_px = level_num_map_rows * TILE_H;
    if (level_w_px > VIEW_W) {
        int target = karnov_x - VIEW_W / 2;
        if (target < 0) target = 0;
        if (target > level_w_px - VIEW_W) target = level_w_px - VIEW_W;
        g_scroll_x = target;
    } else {
        g_scroll_x = 0;
    }
    if (level_h_px > VIEW_H) {
        int target = karnov_y - VIEW_H / 2;
        if (target < 0) target = 0;
        if (target > level_h_px - VIEW_H) target = level_h_px - VIEW_H;
        g_scroll_y = target;
    } else {
        g_scroll_y = 0;
    }
    /* physics.c нужен текущий scroll_y для байт-точного порога «скрыть
     * тело гибели за экраном» (death_tick, [0xCC94]+0x64 в оригинале). */
    physics_set_scroll_y(g_scroll_y);
}

/* ─── Draw level tiles ─────────────────────────────────────────────── */

static void draw_level(bool mode_cga)
{
    int row0 = g_scroll_y / TILE_H;
    int row1 = (g_scroll_y + VIEW_H + TILE_H - 1) / TILE_H;
    int col0 = g_scroll_x / TILE_W;
    int col1 = (g_scroll_x + VIEW_W + TILE_W - 1) / TILE_W;
    if (col1 > level_num_map_cols) col1 = level_num_map_cols;
    if (row1 > level_num_map_rows) row1 = level_num_map_rows;
    for (int r = row0; r < row1; ++r) {
        for (int c = col0; c < col1; ++c) {
            u8 tid = level_map[r][c];
            int sx = c * TILE_W - g_scroll_x;
            int sy = r * TILE_H - g_scroll_y;
            if (mode_cga) {
                render_blit_tile(sx, sy, level_tile_gfx[tid], level_tile_attr[tid], true);
            } else {
                render_blit_tile_ega(sx, sy, level_tile_gfx_ega[tid], level_tile_attr[tid]);
            }
        }
    }
}

/* ─── Ladder ───────────────────────────────────────────────────────── */

static void draw_ladder(u16 frame, int scroll_x, int scroll_y)
{
    if (!physics_ladder_placed()) return;
    int cx = physics_ladder_x();
    int cy_top = physics_ladder_top();
    int cy_bot = physics_ladder_bot();
    /* physics_ladder_bot() — поверхность, на которой лестница установлена
     * (find_floor_y в момент постановки), уже выровненная по tile-границе.
     * Прежнее округление вниз к TILE_H здесь было компенсацией того, что
     * низ брался из s_ground_y (пол уровня); теперь оно только сдвигало бы
     * лестницу, поставленную на нерегулярную поверхность (буквы LEVEL0). */
    /* Sprite #24 = ladder visual (8 px wide × ~8 tall, тайлится по Y). */
    const sprite_t *s = sprite_find(24);
    if (!s) return;
    const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
    render_blit_ladder(cx - scroll_x, cy_top - scroll_y, cy_bot - scroll_y,
                       s->pixels, s->w, s->h, pal);
}

/* ─── HUD bar ──────────────────────────────────────────────────────── */

/* Блит одного спрайта в абсолютных координатах (без scroll), colorkey 1/0xFF —
 * тот же паттерн, что в ai_render/draw_level1_dragon. (x0,y0) — верхний левый. */
static void blit_sprite_abs(const sprite_t *s, int x0, int y0)
{
    if (!s) return;
    const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
    u8 ck = sprite_is_cga() ? 0xFF : 1;
    for (int r = 0; r < s->h; ++r) {
        int yy = y0 + r;
        if (yy < 0 || yy >= WIN_H) continue;
        for (int c = 0; c < s->w; ++c) {
            u8 v = s->pixels[r * s->w + c];
            if (v == 0xFF) continue;
            if (v == ck) continue;
            int xx = x0 + c;
            if (xx < 0 || xx >= WIN_W) continue;
            render_framebuffer()[yy * WIN_W + xx] =
                sprite_is_cga() ? pal[v & 3] : pal[v & 15];
        }
    }
}

/* ─── HUD OPTIONS item bar ────────────────────────────────────────────── */
/* Рисует собранные предметы как ряд иконок (спрайты #39+id) в нижней части
 * HUD. Текущий выбор (inv_current) МИГАЕТ (рисуется только на кадрах, где
 * (frame>>2)&1) плюс подсвечивается рамкой. Иконки — приоритет; count>1
 * показывается маленькой цифрой рядом. */
#define HUD_ITEM_X0    4     /* левый край первой иконки */
#define HUD_ITEM_STEP  18    /* шаг между иконками */

static void draw_item_bar(u16 frame)
{
    int slot = 0;
    int cur = inv_current();
    for (int id = 0; id < ITEM_NUM; ++id) {
        if (inv_count(id) == 0) continue;
        const sprite_t *s = sprite_find((u16)(39 + id));
        int ih = s ? s->h : 12;
        int x0 = HUD_ITEM_X0 + slot * HUD_ITEM_STEP;
        int y0 = VIEW_H + HUD_H - ih - 1;   /* прижато к низу HUD */
        bool is_cur = (id == cur);
        /* Текущий предмет мигает: скрывается на «off»-кадрах. */
        bool blink_on = ((frame >> 2) & 1) != 0;
        if (!(is_cur && !blink_on)) {
            blit_sprite_abs(s, x0, y0);
        }
        if (is_cur) {
            /* Рамка вокруг выбранного (жёлтая). */
            int bw = (s ? s->w : 16) + 2, bh = (s ? s->h : 12) + 2;
            render_fill_rect(x0 - 1,       y0 - 1,      bw, 1,  0xFFFFFF00u);
            render_fill_rect(x0 - 1,       y0 - 1 + bh, bw, 1,  0xFFFFFF00u);
            render_fill_rect(x0 - 1,       y0 - 1,      1,  bh, 0xFFFFFF00u);
            render_fill_rect(x0 - 1 + bw,  y0 - 1,      1,  bh, 0xFFFFFF00u);
        }
        if (inv_count(id) > 1) {
            char cnt[8];
            snprintf(cnt, sizeof(cnt), "%d", inv_count(id));
            render_draw_text(x0 + (s ? s->w : 16) - 4, y0 + (s ? s->h : 12) - 6,
                             cnt, 0xFFFFFFFFu);
        }
        ++slot;
    }
}

/* Символы жизней — белый череп-спрайт #0x32, ряд справа-налево в нижнем
 * ряду HUD. Показывает s_lives запасных жизней. */
static void draw_lives(void)
{
    const sprite_t *icon = sprite_find(LIFE_ICON_SPRITE);
    if (!icon) return;
    int step = icon->w + 1;
    int y = VIEW_H + HUD_H - icon->h - 1;
    int n = (s_lives > KARNOV_MAX_LIVES) ? KARNOV_MAX_LIVES : s_lives;
    for (int i = 0; i < n; ++i) {
        blit_sprite_abs(icon, WIN_W - 4 - (i + 1) * step, y);
    }
}

static void draw_hud(u16 frame, int karnov_x, u8 input_state)
{
    /* Yellow text on black background bar. */
    render_fill_rect(0, VIEW_H, WIN_W, HUD_H, 0xFF000000u);
    char line[96];
    snprintf(line, sizeof(line),
             "SCORE:%-7u F:%-4u X:%-4d Y:%-3d HP:%d TIME:%03d",
             ai_score(), frame, karnov_x, physics_get_y(), physics_hp(), s_level_timer);
    render_draw_text(4, VIEW_H + 2, line, 0xFFFFFF55u);
    /* Мигающий индикатор «время на исходе» — оригинал (file 0x612A) рисует
     * glyph 0x3D мигая по биту 0x10 [0xCDEA] рядом с цифрами таймера. */
    if (s_level_timer_hurry && ((frame >> 4) & 1) != 0) {
        render_draw_text(4, VIEW_H + 2 + 9, "TIME!", 0xFFFF3030u);
    }
    /* OPTIONS item bar — иконки собранных предметов с подсветкой выбора. */
    draw_item_bar(frame);
    draw_lives();   /* символы жизней справа в HUD */
    (void)input_state;
}

/* ─── Level physics setup ──────────────────────────────────────────── */

typedef struct {
    int spawn_x, spawn_y;
    int ground_y, ceiling_y;
    int min_x, max_x;
} level_phys_t;

static level_phys_t level_phys_for(u16 level_num)
{
    if (level_num == 0) {
        /* LEVEL0 splash: 320×120 экран, без скролла.
         * spawn_x=137 — байт-верифицированный из LEVEL0.MOV demo
         * (Karnov walk -22*4 net = ladder placed at x=49 = DOSBox match).
         * ceiling=10 — чтобы прыжок мог дотянуться до верха букв KARNOV. */
        return (level_phys_t){
            .spawn_x = 137, .spawn_y = 108,
            .ground_y = 115, .ceiling_y = 10,
            .min_x = 8, .max_x = 312
        };
    }
    /* LEVEL1+: широкий мир со скроллом.
     * Стартовая позиция Karnov'а — из region[0].respawn_x/y (KARNOV.EXE
     * read_object_regions tail, file 0x51F0 копирует их в шаблон игрока
     * DS:0x480). X в 2px-единицах → ×2. L1: (40,205)→(80,205);
     * L2: (36,280)→(72,280); L3: (32,360)→(64,360); и т.д.
     * ground_y вычисляется per-level сканом карты (level_compute_ground_y). */
    int level_w_px = level_num_map_cols * TILE_W;
    int ground_y = level_compute_ground_y();
    int sx = 80, sy = 300;   /* fallback = шаблон игрока DS:0x480 (40,300) */
    if (level_num_objects > 0) {
        sx = (int)level_objects[0].respawn_x * 2;
        sy = (int)level_objects[0].respawn_y;
    }
    return (level_phys_t){
        .spawn_x = sx, .spawn_y = sy,
        .ground_y = ground_y, .ceiling_y = 10,
        .min_x = 8, .max_x = (level_w_px > 16 ? level_w_px - 8 : 312)
    };
}

/* ─── prepare_level ────────────────────────────────────────────────── */

static int prepare_level(u16 level_num, bool load_demo)
{
    int rc = level_load(level_num);
    if (rc != 0) {
        fprintf(stderr, "prepare_level: level_load(%u) failed rc=%d\n", level_num, rc);
        return rc;
    }
    printf("LEVEL%u: %u объектов, %u врагов, %u tile_graphics, карта %ux%u\n",
           level_num, level_num_objects, level_num_enemies,
           level_num_tile_graphics, level_num_map_rows, level_num_map_cols);

    level_phys_t ph = level_phys_for(level_num);
    palette_set_level(level_num);
    physics_set_world_bounds(ph.min_x, ph.max_x, ph.ground_y, ph.ceiling_y);
    /* DEBUG: KARNOV_START_X env var override (для headless screenshot-теста). */
    const char *dbg_start_x = getenv("KARNOV_START_X");
    int sx = dbg_start_x ? atoi(dbg_start_x) : ph.spawn_x;
    physics_reset(sx, ph.spawn_y);

    /* Таймер уровня — сброс на каждый вход (level_num < 10 всегда для
     * реальных уровней; file 0x1021E, см. комментарий у level_timer_tick). */
    s_level_timer       = LEVEL_TIMER_INIT[level_num % 10];
    s_level_timer_hurry = false;

    ai_init_from_level(level_num);
    /* КАЖДЫЙ вход в уровень (не только респавн после смерти!) в оригинале
     * перевзводит one-shot флаги спауна — см. file 0x6966 (вызывается и на
     * "level complete", и на смерть игрока, cs:0x6C30/0x6C5A), который
     * зовёт cs:0x667F → cs:0xE62:0x1F9 (file 0x10019). Та функция сначала
     * ставит игрока в region.respawn, ЗАТЕМ безусловно гоняет тот же
     * rearm-цикл file 0x1015A (flags = (cur_x+range_x < player_x) ? 1:0).
     * Преднабитые в LEVEL1.BIN flags=1 у орлов/стражей — просто «мусорные»
     * дефолтные данные на диске: они перезаписываются ДО первого
     * CHECKTRIGGERS на любом входе в уровень, а не только после смерти.
     * Раньше порт вызывал rearm только из ai_respawn_rearm() при респавне,
     * поэтому предустановленные flags=1 переживали самый первый заход на
     * уровень и орлы/стражи у старта L1 не появлялись до первой смерти. */
    ai_respawn_rearm(sx);
    s_max_progress_units = sx / 2;
    /* Первый вызов CHECKTRIGGERS — записи с широким range вокруг старта
     * (метатель LEVEL0, стартовые враги LEVEL1) срабатывают сразу. */
    int init_view_left = sx - VIEW_W/2;
    if (init_view_left < 0) init_view_left = 0;
    ai_check_triggers(physics_get_left_x(), physics_get_top_y(), init_view_left, 0);

    if (load_demo) {
        int dr = demo_load(level_num);
        if (dr == 0) {
            printf("demo: загружено %u событий\n", demo_record_count());
        } else {
            printf("demo: LEVEL%u.MOV не найден (rc=%d) — без attract\n", level_num, dr);
            demo_clear();
        }
    } else {
        demo_clear();
        printf("demo: отключено (интерактив с клавиатуры)\n");
    }
    return 0;
}

/* ─── splash_run main loop ─────────────────────────────────────────── */

/* ─── Загрузочный экран ────────────────────────────────────────────────
 *
 * Копирайт-экран + loader-music (track 6). Оригинал показывает его при
 * КАЖДОМ чтении LEVEL%d.BIN с диска — и перед первым уровнем, и между
 * уровнями, — поэтому та же функция вызывается на переходах. Держим
 * экран до конца одного полного цикла loader-музыки (или LOADER_MIN_MS,
 * если звук выключен), затем возвращаем in-game track 0.
 * Возвращает true, если игрок закрыл окно (QUIT/ESC). */
#define LOADER_MIN_MS 700

static bool run_loader_screen(bool audio_running)
{
    const char *line1 = "Karnov (C) 1987 Data East USA Inc.";
    const char *line2 = "Adaptation by Quicksilver Software Inc";
    int x1 = (WIN_W - (int)strlen(line1) * 6) / 2;
    int x2 = (WIN_W - (int)strlen(line2) * 6) / 2;
    render_clear(0xFF000000u);
    render_draw_text(x1, 130, line1, 0xFFFFFFFFu);
    render_draw_text(x2, 140, line2, 0xFFFFFFFFu);
    render_present();

    if (audio_running) audio_play_music(6, /*loop=*/true);

    bool quit = false, skip = false;
    u32  start    = SDL_GetTicks();
    u32  deadline = start + 30000;      /* safety cap 30 s */
    while (!quit && !skip && SDL_GetTicks() < deadline) {
        if (audio_running ? (audio_cycle_count() >= 1)
                          : (SDL_GetTicks() - start >= LOADER_MIN_MS))
            break;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
                quit = true;
                break;
            }
            /* Любая другая клавиша — прервать загрузку и начать игру. */
            if (ev.type == SDL_KEYDOWN) { skip = true; break; }
        }
        SDL_Delay(20);
    }
    if (!quit && audio_running) {
        audio_play_music(0, /*loop=*/true);
        printf("audio: in-game music (track 0) запущена%s\n",
               skip ? " (загрузка прервана клавишей)" : "");
    }
    return quit;
}

int splash_run(const splash_options_t *opt)
{
    bool mode_cga = (opt->video_mode == VIDEO_CGA);
    bool headless = (opt->ppm_path != NULL);

    u16 current_level = opt->level_num;

    /* Sprite cache — ДО загрузки уровня: prepare_level делает первый
     * проход CHECKTRIGGERS, а постановка врагов на землю считает высоту
     * их спрайта. Пока кэш не загружен, sprite_find возвращает NULL, все
     * получают заглушечные 16 px и встают НИЖЕ пола (типы 15 и 38 из-за
     * этого потом проваливались сквозь карту). */
    const char *sprite_file = mode_cga ? "karnov_sprites_cga.bin" : "karnov_sprites_ega.bin";
    if (try_load_sprites(sprite_file) != 0) {
        fprintf(stderr, "sprite_cache_load failed for %s\n", sprite_file);
        return 1;
    }
    printf("sprites: загружен data/%s (%d спрайтов)\n", sprite_file, sprite_cache_count());

    int rc = prepare_level(current_level,
                            opt->with_demo || current_level == 0);
    if (rc != 0) return rc;

    /* Audio: loader-music (track 6) — играет во время загрузки/инициализации.
     * Переключаем на in-game (track 0) после minimum-один-цикл loader-а ниже. */
    bool audio_running = false;
    if (opt->with_audio) {
        if (audio_init() == 0) {
            audio_play_music(6, true);
            audio_running = true;
            printf("audio: loader-music (track 6) запущена\n");
        }
    }

    /* Renderer */
    render_init(opt->scale, headless);

    /* Headless PPM mode: tick до целевого frame, рендер один кадр, сохранение. */
    if (headless) {
        u16 frame = 0;
        while (frame < opt->ppm_frame) {
            demo_tick(frame);
            u8 inp = demo_input_state();
            /* KARNOV_DBG_INPUT=<hex> — подмешать биты ввода в headless-прогоне
             * (для тестов анимаций/физики без клавиатуры). */
            { const char *di = getenv("KARNOV_DBG_INPUT");
              if (di) inp |= (u8)strtol(di, NULL, 16); }
            ai_tick(frame, inp);
            physics_set_current_frame(frame);
            bool was_dying = physics_is_dying();
            physics_tick(inp);
            level_timer_tick(frame, audio_running);
            if (physics_just_died()) {
                int checkpoint_units = physics_get_x() / 2 + 0xBB;
                const object_region_t *rg = level_region_for_respawn(
                    checkpoint_units, physics_get_y(), s_max_progress_units);
                if (rg) physics_set_spawn_point((int)rg->respawn_x * 2,
                                                (int)rg->respawn_y);
            }
            if (was_dying && !physics_is_dying()) {
                ai_respawn_rearm(physics_get_x());
                /* Респавн — тот же file 0x10019, что и вход в уровень:
                 * таймер получает свежее значение (см. level_timer_tick). */
                s_level_timer       = LEVEL_TIMER_INIT[current_level % 10];
                s_level_timer_hurry = false;
            }
            ai_try_pickup(physics_get_x(), physics_get_y());
            /* Pair-loop: касание врага ранит Карнова (file 0xFB7A). */
            ai_check_player_hit(physics_get_x(), physics_get_y(), 16, 26);
            update_scroll(physics_get_x(), physics_get_y());
            if (physics_get_x() / 2 > s_max_progress_units)
                s_max_progress_units = physics_get_x() / 2;
            ai_check_triggers(physics_get_left_x(), physics_get_top_y(), g_scroll_x, frame);
            ++frame;
        }
        /* Initial scroll для frame=0 (когда цикл не выполнился). */
        update_scroll(physics_get_x(), physics_get_y());
        u8 inp = demo_input_state();
        render_clear(0xFF000000u);
        draw_level(mode_cga);
        if (current_level == 1) draw_level1_dragon(g_scroll_x, g_scroll_y);
        draw_ladder((u16)opt->ppm_frame, g_scroll_x, g_scroll_y);
        ai_render(mode_cga, g_scroll_x, g_scroll_y);
        physics_render(inp, (u16)opt->ppm_frame, mode_cga, g_scroll_x, g_scroll_y);
        ai_render_overlay(mode_cga, g_scroll_x, g_scroll_y);
        draw_hud((u16)opt->ppm_frame, physics_get_x(), inp);
        render_present();
        render_save_ppm(opt->ppm_path);
        printf("PPM записан в %s (frame=%d)\n", opt->ppm_path, opt->ppm_frame);
        render_shutdown();
        if (opt->with_audio) audio_close();
        return 0;
    }

    /* Заставка загрузки перед первым уровнем. */
    bool quit = run_loader_screen(audio_running);

    /* Interactive main loop. Точный период оригинала — 36.367 мс (27.4977 Гц),
     * байт-подтверждено заново 2026-09-14: PIT-канал 0 перепрограммирован на
     * divisor 0x1530 (file 0xE3FF, cs:0xCB9:0x4C) → ISR 1193182/5424 =
     * 219.9819 Гц; главный цикл (file 0x69E9-0x69FF) busy-wait'ит ровно 8
     * тиков этого ISR перед следующим кадром: 219.98/8 = 27.4977 Гц. Целых
     * 36мс (используется ниже) короче примерно на 1% — не годится ровно, но
     * фиксировать дробный тик не стали: это НЕ основная причина ощущения
     * «слишком быстро» (высота прыжка не зависит от FPS вовсе, см. правку
     * от того же числа). Важнее другое: 27.5 Гц в оригинале — это ПОТОЛОК
     * busy-wait'а, не гарантированная частота; на реальном XT (4.77 МГц)
     * AI/отрисовка вполне могли не укладываться в 8 тиков и код УХОДИЛ
     * МЕДЛЕННЕЕ потолка — порт же на современном железе всегда попадает
     * точно в потолок, отсюда стабильно «быстрее», чем помнится по XT.
     * Честно смоделировать это без cycle-accurate тайминга 8088-кода
     * нельзя — не делаем (см. план code-review-floating-moth.md). */
    u16 frame = 0;
    u16 s_demo_done_frame = 0;
    bool prev_start = false;
    u32 last_tick = SDL_GetTicks();
    const u32 tick_ms = 36;
    SDL_Event evt;

    printf("game loop: tick_ms=%u (%.1f FPS)\n", tick_ms, 1000.0 / tick_ms);

    while (!quit) {
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT) quit = true;
            if (evt.type == SDL_KEYDOWN) {
                if (evt.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (!evt.key.repeat) {
                    /* Чит ZEEK: последовательность Ctrl+Z, Ctrl+E, Ctrl+E,
                     * Ctrl+K (file 0x3F5E). Любая другая клавиша её
                     * сбрасывает — поэтому вызываем на каждое нажатие. */
                    bool ctrl = (evt.key.keysym.mod & KMOD_CTRL) != 0;
                    input_cheat_key(evt.key.keysym.scancode, ctrl);
                    /* Level select (L + цифра) доступен только с читом,
                     * как в оригинале (file 0x3DB0). */
                    if (input_god_mode() && s_level_select_armed &&
                        evt.key.keysym.sym >= SDLK_0 && evt.key.keysym.sym <= SDLK_9) {
                        u16 lv = (u16)(evt.key.keysym.sym - SDLK_0);
                        if (prepare_level(lv, false) == 0) {
                            current_level = lv;
                            g_scroll_x = g_scroll_y = 0;
                            frame = 0;
                            s_demo_done_frame = 0;
                            s_level_end = false;
                            physics_set_invuln(false);
                            physics_set_sit_scroll(false);
                            printf("level select: LEVEL%u\n", lv);
                        }
                        s_level_select_armed = false;
                    } else if (input_god_mode() && evt.key.keysym.sym == SDLK_l) {
                        s_level_select_armed = true;
                    } else {
                        s_level_select_armed = false;
                    }
                }
            }
        }
        u32 now = SDL_GetTicks();
        if (now - last_tick >= tick_ms) {
            /* Никогда не "досогонять" несколько тиков подряд без задержки
             * между ними — иначе после любой просадки кадра (рендер
             * притормозил, окно свернули/растянули, ОС отвлеклась) движок
             * какое-то время симулирует игру БЫСТРЕЕ реального времени
             * (следующая проверка `now - last_tick >= tick_ms` сразу же
             * снова истинна, и очередной тик выполняется без SDL_Delay).
             * Оригинал на реальном железе физически не мог обогнать
             * таймер PIT — он либо успевал 27.5 кадров в секунду, либо
             * тормозил, но никогда не ускорялся. Поэтому здесь режем
             * накопившееся отставание до одного tick_ms перед тем как
             * снова его прибавлять — симуляция может отставать от
             * реального времени, но никогда не обгоняет его. */
            if (now - last_tick > tick_ms) last_tick = now - tick_ms;
            last_tick += tick_ms;
            demo_tick(frame);
            u8 demo_inp = demo_input_state();
            u8 kbd_only = 0;
            input_poll(&kbd_only);
            /* Во время демо-показа (аттракт-режим либо between-level демка) игра
             * должна управляться ИСКЛЮЧИТЕЛЬНО записью .MOV — клавиатура не должна
             * подмешиваться в inp. `kbd_only` при этом всё равно опрашивается и
             * доступен ниже как есть (ENTER/START-переход в интерактив, ESC, чит
             * ZEEK — всё это читает kbd_only/сырые SDL-события напрямую, не inp,
             * поэтому этим фиксом не затрагивается). */
            bool in_attract = demo_record_count() > 0;
            u8 inp = (kbd_only != 0 && !in_attract) ? kbd_only : demo_inp;

            /* Во время финальной секвенции (взят свиток) игрок не управляет:
             * Karnov сидит (INP_D → crouch-поза), пока играет level-end jingle. */
            if (s_level_end) {
                inp = INP_D; kbd_only = 0;              /* INP_D блокирует движение (сидит на месте) */
                physics_set_sit_scroll(true);           /* поза «сидит со свитком» #0x60-0x62 */
            } else if (current_level >= 1 && ai_scroll_spawned() && !ai_scroll_picked()) {
                /* Авто-секвенция начинается с ПОЯВЛЕНИЯ свитка, а не со смерти
                 * босса. В оригинале обработчик игрока подменяется на
                 * cs:0xE62:0x0CF6 внутри самого свитка (ai_type07), когда тот
                 * проявляется; до этого игрок управляется как обычно.
                 *
                 * Гейт по ai_boss_dead() приводил к дедлоку: настоящий свиток
                 * (запись типа 7) проявляется, только если игрок ближе 0x50
                 * единиц (160 px), а управление отбиралось уже в момент гибели
                 * босса — Карнов не мог подойти, свиток не выпадал и уровень
                 * было не закончить (LEVEL1: свиток на x=3408, бой идёт на
                 * 3242…3282, порог появления — правее 3248). */
                physics_set_invuln(true);
                kbd_only = 0;
                /* Идём к свитку ТОЛЬКО когда он упал в видимую область экрана. */
                if (ai_scroll_onscreen(g_scroll_x, g_scroll_y, VIEW_W, VIEW_H)) {
                    int tx = ai_scroll_target_x();
                    int kx2 = physics_get_x();
                    inp = (kx2 < tx - 4) ? INP_R : (kx2 > tx + 4) ? INP_L : 0;  /* ходьба к свитку */
                } else {
                    inp = 0;  /* ждём пока свиток появится/упадёт в кадр */
                }
            }

            bool demo_done = in_attract &&
                             demo_current_index() >= demo_record_count();
            if (demo_done) {
                if (s_demo_done_frame == 0) s_demo_done_frame = frame;
            }

            /* ENTER → interactive LEVEL1 (skip attract chain). */
            if ((kbd_only & INP_START) && !prev_start &&
                (current_level == 0 || in_attract)) {
                printf("ENTER — interactive LEVEL1\n");
                if (prepare_level(1, /*load_demo=*/false) == 0) {
                    current_level    = 1;
                    g_scroll_x = g_scroll_y = 0;
                    frame      = 0;
                    inp        = 0;
                    s_demo_done_frame = 0;
                    prev_start = true;
                    if (run_loader_screen(audio_running)) { quit = true; break; }
                    last_tick = SDL_GetTicks();
                }
            } else if (demo_done && (frame - s_demo_done_frame) >= 60) {
                u16 next = (current_level == 0) ? 1 : 0;
                printf("attract: LEVEL%u demo done → LEVEL%u demo\n",
                       current_level, next);
                if (prepare_level(next, /*load_demo=*/true) == 0) {
                    current_level = next;
                    g_scroll_x = g_scroll_y = 0;
                    frame = 0;
                    s_demo_done_frame = 0;
                }
            }
            prev_start = (kbd_only & INP_START) != 0;

            ai_tick(frame, inp);
            physics_set_current_frame(frame);
            bool was_dying = physics_is_dying();
            physics_tick(inp);
            level_timer_tick(frame, audio_running);
            /* Karnov только что вошёл в death-state (контакт с водой):
             * запускаем death-music (track 5, не зацикленную). По окончании
             * анимации physics возвращает is_dying=false, и мы возобновляем
             * in-game music (track 0). */
            if (physics_just_died()) {
                /* KARNOV.EXE: hit once → минус жизнь (dec [0xCF4E]);
                 * при исчерпании — game over (в оригинале строка "Game Over"
                 * DS:0xCCB и возврат в attract). */
                if (s_lives > 0) --s_lives;
                else             s_game_over = true;
            }
            if (physics_just_died()) {
                /* Checkpoint (file 0x10026): точка гибели + 0xBB (187 ед.),
                 * затем поиск региона → его respawn_x/y — точка возрождения. */
                int checkpoint_units = physics_get_x() / 2 + 0xBB;
                const object_region_t *rg = level_region_for_respawn(
                    checkpoint_units, physics_get_y(), s_max_progress_units);
                if (rg) physics_set_spawn_point((int)rg->respawn_x * 2,
                                                (int)rg->respawn_y);
            }
            if (was_dying && !physics_is_dying()) {
                /* Karnov возродился: rearm-цикл записей спауна (file 0x1015A) —
                 * всё, что не полностью позади точки возрождения, оживает. */
                ai_respawn_rearm(physics_get_x());
                /* Респавн — тот же file 0x10019, что и вход в уровень:
                 * таймер получает свежее значение (см. level_timer_tick). */
                s_level_timer       = LEVEL_TIMER_INIT[current_level % 10];
                s_level_timer_hurry = false;
            }
            /* Трек 5 «смерть Karnov» запускает сама physics (INIT обработчика
             * гибели, KARNOV.EXE 0x1838). Здесь — только возврат к фоновой
             * музыке после респауна: оригинальный post-death (cs:0x4E4:0x18D)
             * гасит канал A вызовом play_music(0xFFFF, …). */
            if (audio_running && was_dying && !physics_is_dying())
                audio_play_music(0, /*loop=*/true);
            ai_try_pickup(physics_get_x(), physics_get_y());
            /* Pair-loop: касание врага ранит Карнова (file 0xFB7A). */
            ai_check_player_hit(physics_get_x(), physics_get_y(), 16, 26);

            /* Boss death → взят свиток → level-end секвенция (KARNOV.EXE
             * type56): jingle + Karnov садится, затем переход на след. уровень. */
            if (ai_scroll_picked() && current_level >= 1 && !s_level_end) {
                s_level_end = true;
                s_level_end_start = frame;
                if (s_lives < KARNOV_MAX_LIVES) ++s_lives;  /* level-end: выпадает символ жизни (+1) */
                if (audio_running) audio_play_music(2, /*loop=*/false);  /* level-completion jingle */
                printf("LEVEL%u: свиток подобран → level-end секвенция\n", current_level);
            }
            if (s_level_end) {
                u16  elapsed    = (u16)(frame - s_level_end_start);
                bool min_done   = elapsed >= LEVEL_END_MIN_TICKS;
                bool max_done   = elapsed >= LEVEL_END_MAX_TICKS;   /* защита от зависания */
                bool jingle_end = !audio_running || audio_music_finished();
                if ((min_done && jingle_end) || max_done) {
                    u16 next = (current_level < 9) ? (u16)(current_level + 1) : 0;
                    printf("LEVEL%u: секвенция завершена → LEVEL%u\n", current_level, next);
                    if (prepare_level(next, /*load_demo=*/false) == 0) {
                        current_level = next;
                        g_scroll_x = g_scroll_y = 0;
                        frame = 0;
                        s_demo_done_frame = 0;
                        s_level_end = false;
                        physics_set_invuln(false);   /* invuln не течёт в след. уровень */
                        physics_set_sit_scroll(false); /* сбросить позу свитка */
                        /* Между уровнями — тот же экран загрузки с loader-
                         * музыкой, что и перед первым (он же возвращает
                         * in-game track 0). */
                        if (run_loader_screen(audio_running)) { quit = true; break; }
                        last_tick = SDL_GetTicks();
                        continue;
                    }
                }
            }

            /* Game over: жизни кончились — возврат к заставке/аттракту. */
            if (s_game_over && !physics_is_dying()) {
                printf("GAME OVER → LEVEL0 (attract)\n");
                s_game_over = false;
                s_lives = KARNOV_START_LIVES;
                if (prepare_level(0, /*load_demo=*/true) == 0) {
                    current_level = 0;
                    g_scroll_x = g_scroll_y = 0;
                    frame = 0;
                    s_demo_done_frame = 0;
                    s_level_end = false;
                    physics_set_invuln(false);
                    physics_set_sit_scroll(false);
                    if (audio_running) audio_play_music(0, true);
                    continue;
                }
            }
            update_scroll(physics_get_x(), physics_get_y());
            if (physics_get_x() / 2 > s_max_progress_units)
                s_max_progress_units = physics_get_x() / 2;
            ai_check_triggers(physics_get_left_x(), physics_get_top_y(), g_scroll_x, frame);
            render_clear(0xFF000000u);
            draw_level(mode_cga);
            if (current_level == 1) draw_level1_dragon(g_scroll_x, g_scroll_y);
            draw_ladder(frame, g_scroll_x, g_scroll_y);
            ai_render(mode_cga, g_scroll_x, g_scroll_y);
            physics_render(inp, frame, mode_cga, g_scroll_x, g_scroll_y);
            ai_render_overlay(mode_cga, g_scroll_x, g_scroll_y);
            draw_hud(frame, physics_get_x(), inp);
            render_present();

            ++frame;
        } else {
            SDL_Delay(1);
        }
    }

    render_shutdown();
    if (opt->with_audio) audio_close();
    return 0;
}
