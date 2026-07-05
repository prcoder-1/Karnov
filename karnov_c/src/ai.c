/* ai.c — function-pointer table AI dispatcher.
 *
 * AI_HANDLERS[type] вызывается для каждого живого объекта.
 * Реализованы: type 13 (Y-tracker), type 6 (bird), type 512 (stone man).
 * Остальные = ai_stub (no-op — враг остаётся на стартовой позиции,
 * рендерится плейсхолдер).
 */
#include "ai.h"
#include "level.h"
#include "physics.h"
#include "sprite.h"
#include "palette.h"
#include "render.h"
#include "inventory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MAX_AI bumped 32→64. LEVEL1 имеет 78 cumulative-spawn записей; даже
 * без recycling это слишком мало. 64 даёт запас под десятки одновременно
 * активных + projectiles + items. */
#define MAX_AI 64

/* Splash-only psevdo-типы (выше 1000, не пересекаются с KARNOV.EXE).
 * Реальные типы [1..57] — это enemy types из ai_scripts.cpp. */
#define TYPE_STONE_THROWER     1001  /* зелёный качок справа splash, кидает глыбы */
#define TYPE_ROCK_PROJECTILE   1002  /* катящаяся красная глыба */
#define TYPE_PLAYER_FIREBALL   1003  /* снаряд Karnov-а (FIRE press) */
#define TYPE_FLYING_BIRD       1004  /* LEVEL1-start «два орла» (sky-spawn, sprites
                                      * #147-148). Отдельный псевдо-тип, чтобы не
                                      * путать с реальным type 14 = Селёдка. */

/* Sprite IDs для player-fireball — 2-кадровый цикл из user-verified
 * mapping (2026-05-27): #19, #20 — анимация выстрелов Karnov.
 * Конкретный кадр выбирается по o->anim_phase (см. sprite_for_type). */
#define SPRITE_PLAYER_FIREBALL_A  19
#define SPRITE_PLAYER_FIREBALL_B  20

/* 1-hit kill: список AI-типов, которых fireball уничтожает.
 * Декорации (type 512 stone-man) и собственно players/items (type 6) не
 * затрагиваются. */
static bool is_damageable_enemy(u16 type)
{
    switch (type) {
        case   5:                       /* падающий бонус — выбить из воздуха */
        case   8:                       /* sky-spawn враг */
        case   9:                       /* 2-bbox boss-like */
        case  10:                       /* маятник */
        case  11:                       /* наземный патрульный */
        case  13:                       /* Y-tracker */
        case  14:                       /* летающая птица */
        case  15:                       /* каменный голем */
        case  16:                       /* горизонтальный снаряд */
        case  17:                       /* страус/птица */
        case  18:                       /* walking minion */
        case  19:                       /* медленный враг */
        case TYPE_STONE_THROWER:        /* зелёный качок */
        case TYPE_ROCK_PROJECTILE:      /* катящийся камень */
            return true;
        default:
            return false;
    }
}

typedef struct ai_obj {
    bool   alive;
    bool   invisible;     /* OPTIONS items (type 6) — на splash скрыты */
    bool   is_boss;       /* type 14 в конце уровня (Селёдка) */
    u16    type;
    int    x, y;
    int    vx, vy;        /* пикс/кадр для простых types */
    int    facing;        /* +1 / -1 */
    int    hp;            /* health (для boss'а >1) */
    int    level_idx;     /* индекс записи в level_enemies[], или -1 */
    /* Movement bounds (из EnemySpawn.range_x/range_y, half-amplitudes от
     * home (spawn_x, spawn_y)). AI handler'ы должны clamp'ить вне bounds. */
    int    home_x, home_y;
    int    half_range_x, half_range_y;
    u16    anim_phase;
    u16    spawn_x, spawn_y;
    int    custom;        /* type-specific scratch (для thrower'а — throw timer) */
    u16    spawn_frame;   /* кадр на котором обьект появился */
    /* Projectile Q16.16 sub-pixel state (TYPE_ROCK_PROJECTILE).
     * Соответствуют полям object'а +0x0E/+0x10 (vx) и +0x12/+0x14 (vy)
     * из KARNOV.EXE handler cs:0x11D6:0x08B3 (sprites #21-23).
     * grav_q16 эмулирует то, что движок прибавляет к vy каждый кадр
     * в UPDATELOCS (значение 0xAAAA взято из obj+0x1A в той же init-секции). */
    int    pos_x_q16;
    int    pos_y_q16;
    int    vx_q16;
    int    vy_q16;
    int    grav_q16;
    int    bounces;
} ai_obj_t;

static ai_obj_t g_obj[MAX_AI];
static int      g_num = 0;

/* Per-entry state машинка для bidirectional scroll spawn:
 *   g_alive_idx[i]  — индекс активного g_obj-слота для level_enemies[i],
 *                     или -1 если не активен сейчас (despawned, но может
 *                     re-spawn при возврате камеры).
 *   g_killed[i]     — true если игрок убил этого врага; повторно не спавним.
 *   g_one_shot[i]   — для пикапов / projectiles: однократный спавн (после
 *                     активации больше не возвращаемся).
 * Размер LEVEL_MAX_ENEMIES (256). Сбрасывается в ai_init_from_level. */
static s16  g_alive_idx[LEVEL_MAX_ENEMIES];
static bool g_killed   [LEVEL_MAX_ENEMIES];
static bool g_one_shot [LEVEL_MAX_ENEMIES];
static bool g_spawn_triggered[LEVEL_MAX_ENEMIES];   /* spawn_x порог однажды пройден */
/* Наибольший right edge viewport'а, достигнутый камерой. Инициализирован
 * VIEW_W в `ai_init_from_level` — все spawn_x ≤ VIEW_W считаются «уже
 * пройденными» (Image #8 user-spec: чистый старт уровня). */
static int g_max_right_edge = 0;

/* Boss-конец: когда type 14 boss убит — g_boss_dead=true и через 30 ticks
 * спавним свиток (scroll item) сверху. После пикапа Karnov-ом инициируем
 * level-end transition. */
static bool g_boss_dead = false;
static u16  g_boss_dead_frame = 0;
static bool g_scroll_picked = false;

/* Запас в пикселях справа от viewport, на котором мы заранее активируем
 * врага (чтобы он успел появиться до того, как игрок увидит). 32 px
 * соответствует двум 16-px тайлам — типичная safe-зона.
 * Также применяется СЛЕВА — если scroll вернулся назад, мы не ре-спаунит
 * (это monotone cursor), но новых не активируем тоже. */
#define SPAWN_MARGIN 32


static void spawn_rock(int x, int y, int vx, u16 frame);
static int alloc_obj_slot(void);

/* ─── Handlers ──────────────────────────────────────────────────────── */

/* Type 6 — скрытый OPTIONS item (powerup). На splash невидим;
 * становится видимым только когда у Karnov надеты «Glasses»
 * (см. ENEMY_GUIDE_CORRECTIONS.md §«Glasses: see hidden Options»).
 * Логики тика нет, флаг invisible выставлен в ai_init_from_level. */
static void ai_options_item(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)o; (void)frame; (void)karnov_input;
}

/* Type 13 — Комарик (mosquito Y-tracker, спрайты #91..94).
 * Каждые ~64 кадра делает шаг по X/Y в сторону Karnov.
 * 4-кадровая анимация полёта со страйдом ~8 ticks/кадр. */
static void ai_y_tracker(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 3) & 3;
    if ((frame & 0x3F) != 0) return;
    int kx = physics_get_x(), ky = physics_get_y();
    if (ky < o->y - 2)      o->y -= 1;
    else if (ky > o->y + 2) o->y += 1;
    if (kx > o->x + 40)     { o->x += 1; o->facing =  1; }
    else if (kx < o->x - 40){ o->x -= 1; o->facing = -1; }

    /* Movement bounds: home ± half_range (из EnemySpawn.range_x/range_y).
     * Если range нулевой (LEVEL0 splash — нет данных), fallback на
     * исходные splash-границы 20..300 / 90..115. */
    if (o->half_range_x > 0) {
        int xmin = o->home_x - o->half_range_x;
        int xmax = o->home_x + o->half_range_x;
        if (o->x < xmin) o->x = xmin;
        if (o->x > xmax) o->x = xmax;
    } else {
        if (o->x < 20) o->x = 20;
        if (o->x > 300) o->x = 300;
    }
    if (o->half_range_y > 0) {
        int ymin = o->home_y - o->half_range_y;
        int ymax = o->home_y + o->half_range_y;
        if (o->y < ymin) o->y = ymin;
        if (o->y > ymax) o->y = ymax;
    } else {
        if (o->y < 90) o->y = 90;
        if (o->y > 115) o->y = 115;
    }
}

/* Type 512 — Stone Man (статичная декорация с лёгкой анимацией).
 * Стрида ≈ 16 game-ticks (медленный четырёхкадровый цикл — без байт-верификации
 * для типа 512, выбрано «на глаз» под темп DOSBox-референса). */
static void ai_stone_man(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 4) & 3;
}

/* TYPE_STONE_THROWER — splash-only «зелёный качок», стоит на правом краю
 * заставки и периодически кидает влево катящуюся глыбу (TYPE_ROCK_PROJECTILE).
 *
 * Байт-верифицированно по handler cs:0x11D6:0x06AA (file 0x13C0A):
 *   state 1 INIT (file 0x13C67):
 *     obj.sub_timer_max = 4       (collision_cnt reload — AI thinks every 4 ticks)
 *     obj.state_list_off = 0x18CC
 *     obj.E9 = 0 (idle phase), obj.EB = 25 (idle timer ticks)
 *   state 2 TICK (file 0x13CA8):
 *     sub-dispatch by obj.E9:
 *       E9=0 (idle): if EB<0 → E9=4 (throw); else decrement EB
 *       E9=4 (throw): when sub_timer==1, call queue_state(new_obj, 0x11D6:0x08B3)
 *                    → spawns new object with rolling-rock handler
 *
 * AnimSegment refs (DS:0x18CC = state_list, byte-dumped from KARNOV.EXE):
 *   state 0: AnimSeg DS:0x1888 = [stride=2, #86, #87]            (idle)
 *   state 4: AnimSeg DS:0x1896 = [stride=3, #88, #89, #90]      (throw)
 *
 * Реальная анимационная скорость (think.cpp think_cycle):
 *   --collision_cnt каждый game-tick;
 *   при collision_cnt<=0 → collision_cnt=sub_timer_max, ai_dispatch.
 *   В dispatch: ++sub_timer; если sub_timer<*state_ptr — без смены кадра.
 * Итого frames_per_anim_change = sub_timer_max × *state_ptr game-ticks.
 *   idle  : 4 × 2 = 8 ticks/frame ≈ 290 ms
 *   throw : 4 × 3 = 12 ticks/frame ≈ 432 ms
 *
 * Слоты anim_phase: 0=#86, 1=#87, 2=#88, 3=#89, 4=#90. */
#define IDLE_FLIP_STRIDE     8     /* 4 × *state_ptr(2) = 8 ticks per idle frame */
#define WINDUP_LEN           5     /* фаза #88 (windup) — ускорено с 10 */
#define THROW1_LEN           5     /* фаза #89 (момент броска) — ускорено с 10 */
#define THROW2_LEN           5     /* фаза #90 (follow-through) — ускорено с 10 */
/* Idle между бросками = 80 ticks (≈2.9s @ 27.5 FPS). Реже, чем было (40 ticks),
 * но всё ещё короче декомпилированного оригинала (100 ticks), чтобы LEVEL1
 * thrower'ы успевали бросить ДО despawn'а при scroll'е игрока мимо них. */
#define IDLE_LEN            80
#define THROW_PERIOD       (IDLE_LEN + WINDUP_LEN + THROW1_LEN + THROW2_LEN)

static void ai_stone_thrower(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    u16 phase = (u16)((frame - o->spawn_frame) % THROW_PERIOD);
    u16 idle_len = IDLE_LEN;

    if (phase < idle_len) {
        /* Type 17 (LEVEL1 gameplay enemy) — переориентируемся на Karnov-а
         * во время idle. TYPE_STONE_THROWER (splash-декорация) сохраняет
         * статичный facing=-1 из спавна. Facing «замораживается» на старте
         * windup, чтобы спрайт не дёргался во время броска. */
        if (o->type == 17) {
            int kx = physics_get_x();
            o->facing = (kx >= o->x) ? 1 : -1;
        }
        o->anim_phase = (phase / IDLE_FLIP_STRIDE) & 1;  /* IDLE #86/#87 */
    } else if (phase < idle_len + WINDUP_LEN) {
        o->anim_phase = 2;             /* #88 — windup, рок в руке */
    } else if (phase < idle_len + WINDUP_LEN + THROW1_LEN) {
        o->anim_phase = 3;             /* #89 — момент броска */
    } else {
        o->anim_phase = 4;             /* #90 — follow-through */
    }

    /* Spawn rock на переходе #88 → #89. */
    if (phase == idle_len + WINDUP_LEN && frame != o->spawn_frame) {
        int rock_x  = o->x + (o->facing < 0 ? -10 : 10);
        int rock_y  = o->y - 18;
        int rock_vx = (o->facing < 0 ? -3 : 3);
        spawn_rock(rock_x, rock_y, rock_vx, frame);
    }
}

/* TYPE_ROCK_PROJECTILE — катящаяся каменная глыба, побайтовый порт AI handler
 * cs:0x11D6:0x08B3 (file 0x13E13) для sprites #21-23.
 * Init: vx=±3, vy=-1.5 (UP launch), gravity=0xAAAA/65536 (≈0.666 px/frame²).
 * TICK: при ударе об пол (tile_attr & 1) bounce с уменьшением скорости,
 * gravity→0x5555.
 *
 * Despawn по WORLD coordinates (раньше было по screen — баг для LEVEL1+). */
#define ROCK_FLOOR_Y_SPLASH  115         /* пиксельная «земля» на splash */
#define ROCK_GRAV_INITIAL  0xAAAA        /* obj+0x1A init = 0xAAAA */
#define ROCK_GRAV_AFTER    0x5555        /* obj+0x1A post-bounce = 0x5555 */
#define ROCK_VY_INITIAL  (-0x18000)      /* -1.5 px/frame (Q16.16) */

static void ai_rock_projectile(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;

    o->vy_q16 += o->grav_q16;           /* gravity per frame */
    o->pos_x_q16 += o->vx_q16;
    o->pos_y_q16 += o->vy_q16;
    o->x = o->pos_x_q16 >> 16;
    o->y = o->pos_y_q16 >> 16;

    /* Bounce: для LEVEL0 splash используем фиксированный FLOOR_Y (там нет
     * solid-тайлов в map), для LEVEL1+ — solid-тайл ИЛИ default ground_y
     * (если sand-floor тайл не имеет attr&1, как в LEVEL1). До 2026-05-28
     * рок проваливался через sand-floor т.к. attr&1=0 для песчаных тайлов. */
    int level1_floor = physics_get_ground_y();
    int floor_hit;
    int floor_y_snap;
    if (level_num_map_rows <= 12) {
        floor_hit = (o->y >= ROCK_FLOOR_Y_SPLASH && o->vy_q16 > 0);
        floor_y_snap = ROCK_FLOOR_Y_SPLASH;
    } else if (o->vy_q16 > 0 && level_solid_at(o->x, o->y + 1)) {
        floor_hit = 1;
        floor_y_snap = o->y;
    } else if (o->vy_q16 > 0 && o->y >= level1_floor) {
        floor_hit = 1;
        floor_y_snap = level1_floor;
    } else {
        floor_hit = 0;
        floor_y_snap = o->y;
    }
    if (floor_hit) {
        o->y = floor_y_snap;
        o->pos_y_q16 = (int)((unsigned)floor_y_snap << 16);
        o->vy_q16 = -(o->vy_q16 >> 1);  /* отскок с потерей половины амплитуды */
        o->grav_q16 = ROCK_GRAV_AFTER;
        ++o->bounces;
        /* После 4 отскоков камень «стихает» — despawn. */
        if (o->bounces >= 4) { o->alive = false; return; }
    }

    /* Animation: AnimSeg DS:0x18EC = [stride=3, #21, #22, #23] (rolling rock). */
    u16 since_spawn = frame - o->spawn_frame;
    o->anim_phase = (since_spawn / 3) % 3;

    /* Despawn по WORLD-границам. Для splash (LEVEL0) world_w = 320, для
     * LEVEL1+ — много шире (3600+ px). Используем level_num_map_cols×TILE_W. */
    int world_w = (int)level_num_map_cols * TILE_W;
    if (o->x < -16 || o->x > world_w + 16) o->alive = false;
}

/* TYPE_PLAYER_FIREBALL — снаряд Karnov-а. Прямолинейное движение, без
 * гравитации, 1-hit kill при пересечении bbox-а живого damageable-врага.
 * Despawn при выходе за края экрана.
 *
 * Скорость 6 px/frame в направлении facing — заметно быстрее WALK_SPEED(4)
 * и ROCK_VX(3), чтобы снаряд мог догнать убегающего врага. Конкретное
 * значение пока НЕ байт-верифицировано из KARNOV.EXE (handler Karnov-fire
 * не декомпилирован). */
#define PLAYER_FIREBALL_SPEED  6

/* Урон снаряда Karnov-а по врагу. В оригинале (level.cpp pair-loop,
 * cs:0xFC2C) урон = hit_points САМОГО снаряда: `b->hit_points -= a->hit_points`,
 * где a — атакующий объект игрока. HP врага (spawn.trigger>>8) — это ПУЛ:
 * все значения в LEVEL%d.BIN кратны/сравнимы с 10 (обычные враги=10 → 1 удар,
 * type18=20 → 2, type13/15=30 → 3, боссы 95..250 → 10..25). Отсюда damage=10.
 * НЕ байт-верифицировано (handler Karnov-fire не декомпилирован), но это
 * единственное значение, при котором обычные враги гибнут с одного попадания
 * И все HP-пулы дают целое число попаданий. */
#define PLAYER_FIREBALL_DAMAGE 10

static void ai_player_fireball(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)frame; (void)karnov_input;

    /* Движение. */
    o->x += o->vx;
    o->anim_phase = (o->anim_phase + 1) & 1;

    /* Despawn off-screen — теперь world-coords, не screen. */
    int world_w = (int)level_num_map_cols * TILE_W;
    if (o->x < -8 || o->x > world_w + 8) {
        o->alive = false;
        return;
    }

    /* Столкновение с живыми damageable-врагами. bbox fireball'а — 8×6.
     * bbox противника — приближённо 16×24 от его (x, y) = центр ног.
     * Простое AABB-пересечение. */
    int fx0 = o->x - 4, fx1 = o->x + 4;
    int fy0 = o->y - 3, fy1 = o->y + 3;
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *e = &g_obj[i];
        if (!e->alive) continue;
        if (e == o) continue;
        if (!is_damageable_enemy(e->type) && !e->is_boss) continue;
        int ex0 = e->x - 8, ex1 = e->x + 8;
        int ey0 = e->y - 24, ey1 = e->y;
        if (fx1 < ex0 || fx0 > ex1) continue;
        if (fy1 < ey0 || fy0 > ey1) continue;
        /* Hit. */
        ai_notify_enemy_hit(i, frame);
        o->alive = false;
        return;
    }
}

/* ─── LEVEL1 enemies (упрощённые реализации) ──────────────────────────
 *
 * Поведение каждого типа — это «достаточно близко к оригиналу» (визуально
 * различимое движение + анимация), а не байт-точный порт. Sprite-IDs и
 * скорости подобраны из ENEMY_GUIDE.md + ENEMY_GUIDE_CORRECTIONS.md +
 * karnov_cpp/src/ai_scripts.cpp комментариев. Где точно неизвестно —
 * placeholder rect (см. ai_render).
 *
 * Базовая семантика:
 *   facing — обновляется по направлению к Karnov-у при init.
 *   anim_phase — индекс в массиве sprite-id'ов для рендера.
 *   custom — type-specific scratch (timer, HP, sub-state).
 *
 * Все AI ниже — damageable (см. is_damageable_enemy). Karnov-fireball
 * убивает с одного попадания, что упрощает геймплей. */

/* Type 11 — наземный патрульный (декомпиляция cs:0x11D6:0x09F4 → ai_type11).
 *
 * Pre-dispatch: pending_state_id = (player_x > obj_x) ? 1 : 0 — ВСЕГДА
 * лицом к игроку (выбор анимации L/R).
 *
 * INIT (case 1):  obj.sub_timer_max = 2;  state_list_off = 0x1856;
 * TICK (case 2):
 *   phase 0 (далеко): if (abs_dx >= 10) return  // остаёмся в phase 0
 *                     else: phase = 1; pending_state_id += 2; emit ai_result|2
 *   phase 1 (близко): if (abs_dx <= 10) return  // остаёмся в phase 1
 *                     else: phase = 0;
 *
 * Семантика: phase 0 = «walk toward player», phase 1 = «stopped (близко)».
 * Реальная скорость задаётся state_list animation (не moveменно мы вычисляем).
 * Приблизительно 0.5 px/frame walk (sub_timer_max=2). */
static void ai_ground_walker(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    int kx = physics_get_x();
    int dx = kx - o->x;
    int abs_dx = dx < 0 ? -dx : dx;

    /* Always face player (pending_state_id = dir). */
    o->facing = (dx >= 0) ? 1 : -1;

    /* 10-кадровая walk-анимация «Зелёного стражника с мечом» (#76..85).
     * sub_timer_max=2 × stride ≈ 4 game-ticks/кадр → ~146 ms per frame
     * (при 27.5 FPS, цикл ≈ 1.46 s). */
    o->anim_phase = (frame >> 2) % 10;

    /* Phase-based movement: walk if far (abs_dx > 10), stop if close. */
    if (abs_dx > 10 && (frame & 1) == 0) {
        o->x += o->facing;
    }
    /* Movement bounds: home ± half_range_x. */
    if (o->half_range_x > 0) {
        int xmin = o->home_x - o->half_range_x;
        int xmax = o->home_x + o->half_range_x;
        if (o->x < xmin) o->x = xmin;
        if (o->x > xmax) o->x = xmax;
    }
}

/* Type 17 в LEVEL1 — это «Зелёный каменный человек» (метатель), а не
 * страус (user-verified 2026-05-28). Sprites #86..90 — 5-кадровая
 * анимация: idle #86/#87 → windup #88 → release #89 → follow-through #90.
 * Логика идентична splash-TYPE_STONE_THROWER: периодически спавнит
 * катящийся камень (TYPE_ROCK_PROJECTILE, sprites #21..23) влево по facing.
 *
 * Используется существующий ai_stone_thrower (см. ниже) — он закрывает
 * как splash-декорацию, так и обычных LEVEL1 врагов type 17. Разница только
 * в способе спавна (LEVEL0 inline upfront vs LEVEL1 scroll-gated). */
/* (Скелет на страусе #161..163 — это другой враг, в LEVEL1 не встречается;
 * sprite-диапазон оставлен для будущих уровней.) */

/* Type 18 — Чёртик (walking minion Y-snap, спрайты #153..160).
 * Идёт к Karnov-у по X, его Y постепенно подтягивается к Y игрока. */
static void ai_walking_minion(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    int kx = physics_get_x(), ky = physics_get_y();
    o->facing = (kx > o->x) ? 1 : -1;
    /* 8-кадровый walk-cycle (sub_timer_max=3 × stride ≈ 3 = 9 ticks/кадр). */
    o->anim_phase = (frame >> 2) & 7;

    if ((frame & 1) == 0) {
        /* Движение по X — 0.5 px/frame в сторону игрока. */
        o->x += o->facing;
    }
    /* Y-snap — медленное подтягивание (1 px каждые 8 кадров). */
    if ((frame & 7) == 0) {
        if (ky < o->y - 1)      o->y -= 1;
        else if (ky > o->y + 1) o->y += 1;
    }
    /* Movement bounds от EnemySpawn.range_x/y. */
    if (o->half_range_x > 0) {
        int xmin = o->home_x - o->half_range_x;
        int xmax = o->home_x + o->half_range_x;
        if (o->x < xmin) o->x = xmin;
        if (o->x > xmax) o->x = xmax;
    }
    if (o->half_range_y > 0) {
        int ymin = o->home_y - o->half_range_y;
        int ymax = o->home_y + o->half_range_y;
        if (o->y < ymin) o->y = ymin;
        if (o->y > ymax) o->y = ymax;
    }
}

/* Type 16 — горизонтальный снаряд (декомпиляция cs:0x11D6:?? → ai_type16).
 *
 * Pre-dispatch: pending_state_id = (player_x > obj_x) ? 1 : 0 — facing к
 * player'у. INIT: obj.sub_timer_max=3; vx = obj+0x10:0x0E = 0xFFFE:0x8000
 * (Q16.16 = -1.5 px/frame); state_list_off = 0x1984.
 *
 * Оригинал стреляет В СТОРОНУ ИГРОКА в момент спавна: если игрок справа —
 * снаряд летит вправо; если слева — влево. Скорость 1.5 px/frame в выбранную
 * сторону, направление не меняется на лету.
 *
 * Используем `custom` как флаг «init done», чтобы facing вычислялся только
 * однократно на первом тике. */
static void ai_horizontal_projectile(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->custom == 0) {                /* init on first tick */
        int kx = physics_get_x();
        o->facing = (kx >= o->x) ? 1 : -1;
        o->custom = 1;
    }
    /* «Селёдка» — 4-фазная анимация плавания (#149..152), стрида ≈ 4 ticks/кадр. */
    o->anim_phase = (frame >> 2) & 3;
    /* -1.5 px/frame: 1 px каждые 2 кадра + 1 px каждые 4 = 3 px / 4 кадра. */
    if ((frame & 1) == 0) o->x += o->facing;
    if ((frame & 3) == 0) o->x += o->facing;
    /* Despawn off-world. */
    if (o->x < -16 || o->x > 32000) o->alive = false;
}

/* Type 5 — падающий бонус (KARNOV.EXE: ai_type05 cs:0xFFA:0x12... ).
 * INIT: vy=+2 px/frame (gravity). TICK: tile_at_xy(x, y+10) — если
 * (attr & 1), bonus застыл → меняем custom-state на «pickable». До тех пор
 * — падает.
 *
 * Реализация: используем custom как «is_landed». Если 0 — падаем со
 * скоростью 2 px/frame. Если 1 — стоим, ждём подбора. */
static void ai_falling_bonus(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    /* anim_phase индексирует sprite cycle (39-50 — все опции, перебираем
     * для отладки. Реальный оригинал хранит конкретный sprite_id в misc_state). */
    o->anim_phase = (frame >> 4) & 7;
    if (o->custom == 1) return;  /* приземлился */
    /* Падаем 2 px/frame. */
    o->y += 2;
    /* Tile collision — feet are at o->y (по нашей конвенции — bottom of sprite). */
    if (level_solid_at(o->x, o->y + 1)) {
        /* Snap к верху найденного solid-row'а. */
        o->y = ((o->y) / 10) * 10;
        o->custom = 1;
    }
    if (o->y >= 300) {            /* fallback: дефолт ground LEVEL1 */
        o->y = 290;
        o->custom = 1;
    }
}

/* Type 14 — Летящая зелёная птица (спрайты #147..148, 2 фазы).
 * Sinusoidal Y-движение + горизонтальный дрейф к игроку. */
static void ai_flying_bird(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    /* «Два орла» в начале LEVEL1 — synthetic spawn (level_idx<0): летят
     * строго влево без разворота к игроку, Y-осцилляция вдвое быстрее.
     * Остальные type-14 птицы (из level_enemies[]) — стандартное
     * tracking-поведение с разворотом к игроку. */
    bool fixed_left = (o->level_idx < 0);
    if (fixed_left) {
        o->facing = -1;
    } else {
        int kx = physics_get_x();
        o->facing = (kx > o->x) ? 1 : -1;
    }
    /* 2-кадровый цикл — взмах крыльями ≈ 4 ticks/кадр.
     * Анимация из KARNOV.EXE state_list @ DS:0x17E4: count=6, sprites
     * [0x93, 0x93, 0x93, 0x94, 0x94, 0x94] = [147, 147, 147, 148, 148, 148].
     * 6-frame cycle с 3 повторами каждого спрайта = медленный взмах.
     *
     * Скорость: оригинал KARNOV.EXE eagle handler @ cs:0xFFA:0x07CC INIT
     * устанавливает `obj+0x14 = 2` (vy_hi=Q16.16 = +2.0 px/frame базовый
     * spawn-fall). После lshift_long×2 (как в Karnov walk) = vy=4 px/frame.
     * Для горизонтального движения структура DS:0x1800 содержит [5,0,5,0,5]
     * — это X-движение per tick. Используем 3 px/frame (компромисс между
     * "fall in" и "attack" фазами; для horizontal-tracking быстрее чем
     * базовый Karnov walk=4, чтобы орлы догоняли игрока). */
    o->anim_phase = (frame >> 2) & 1;
    o->x += 3 * o->facing;

    /* Counterphase Y oscillation (user-spec 2026-05-29): два орла в
     * LEVEL1 начале должны менять высоту в противофазе. `custom` поле =
     * phase offset: 0 = верхний (старт max -20 px), 1 = нижний (старт max
     * +20 px, 180° сдвиг). Таблица начинается с MAX чтобы орлы видимо
     * разделялись с frame=0. Период для «двух орлов» ускорен (frame>>2,
     * ~64 ticks ≈ 2.3s @ 27.5 FPS) против ~128 ticks у остальных. */
    static const s8 sin16[16] = {
        -20, -18, -14,  -8,   0,  +8, +14, +18,
        +20, +18, +14,  +8,   0,  -8, -14, -18,
    };
    int phase = ((frame >> (fixed_left ? 2 : 3)) + (o->custom ? 8 : 0)) & 15;
    o->y = o->home_y + sin16[phase];

    /* Movement bounds. */
    if (o->half_range_x > 0) {
        int xmin = o->home_x - o->half_range_x;
        int xmax = o->home_x + o->half_range_x;
        if (o->x < xmin) o->x = xmin;
        if (o->x > xmax) o->x = xmax;
    }
    if (o->half_range_y > 0) {
        int ymin = o->home_y - o->half_range_y;
        int ymax = o->home_y + o->half_range_y;
        if (o->y < ymin) o->y = ymin;
        if (o->y > ymax) o->y = ymax;
    }
}

/* Type 15 — каменный голем (anchor minion, sprite 0xB2=178 per
 * ai_scripts.cpp:1844 zone, ENEMY_GUIDE_CORRECTIONS).
 * Стоит на месте, периодически «стреляет» вращением — для нашего порта
 * просто статичная декорация с лёгкой анимацией. */
static void ai_stone_golem(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 4) & 1;
    /* Facing к игроку, но не двигаемся. */
    int kx = physics_get_x();
    o->facing = (kx > o->x) ? 1 : -1;
}

/* Type 19 — медленный враг (ENEMY_GUIDE: sprite 0xAC=172, sub_timer_max=1000
 * — почти статичный). Использует helper 0x4B36, общий с type 9.
 * Реализация: декорация с очень медленной анимацией. */
static void ai_slow_helper(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 6) & 1;       /* очень медленная флипа */
}

/* Type 10 — ГОРИЗОНТАЛЬНЫЙ маятник (декомпиляция cs:0x11D6:0x0413
 * → ai_type10). Документация: «Движется горизонтально по фиксированной
 * траектории, при достижении предельной точки flip'ит направление
 * (через NEG двойной точности)». vx_lo = 0x1000 (≈ slow), target = 0x6000
 * или 0xA000 (зависит от obj.sub_timer ≥ 0x0A).
 *
 * Упрощение: ±range_x px swing вокруг spawn_x (range_x в LEVEL1 = 5),
 * период ~32 кадра. ОРИГИНАЛ ВЕРТИКАЛЬНО НЕ ДВИГАЕТСЯ — поправлена ошибка
 * предыдущей реализации (был vertical). */
static void ai_pendulum(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    /* Амплитуда swing'а = half_range_x (LEVEL1 enemy[4]: range_x=5 → ±2).
     * Если range нулевой, fallback на ±5. */
    int amp = (o->half_range_x > 0) ? o->half_range_x : 5;
    int phase = (frame >> 2) & 7;
    static const s8 sin8[8] = { 0, +1, +2, +2, 0, -1, -2, -2 };
    o->x = o->home_x + (sin8[phase] * amp + 1) / 2;
    /* «Демон на облаке» — 8 фаз (#164..171), стрида ≈ 8 ticks/кадр. */
    o->anim_phase = (frame >> 3) & 7;
}

/* Default — no-op. */
static void ai_stub(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)o; (void)frame; (void)karnov_input;
}

typedef void (*ai_handler_fn)(ai_obj_t *, u16, u8);

/* Полный набор AI-типов из KARNOV.EXE (57 хендлеров, см. ENEMY_GUIDE.md
 * и karnov_cpp/src/ai_scripts.cpp). Здесь реализованы 3 типа, остальные
 * = ai_stub (враг рендерится но не двигается). Расширение — будущая
 * работа: каждый case → function-pointer на конкретный обработчик. */
static ai_handler_fn pick_handler(u16 type)
{
    switch (type) {
        case   1: return ai_stub;            /* Karnov idle/walk — physics.c */
        case   2: return ai_stub;            /* Karnov spawn-marker — physics.c */
        case   5: return ai_falling_bonus;   /* падающий бонус */
        case   6: return ai_options_item;    /* скрытый OPTIONS powerup */
        case   8: return ai_stub;            /* sky-spawner — спавн отключён */
        case   9: return ai_walking_minion;  /* 2-bbox boss — упрощённо как walker */
        case  10: return ai_pendulum;        /* маятник */
        case  11: return ai_ground_walker;   /* наземный патрульный */
        case  13: return ai_stone_thrower;   /* зелёный каменный метатель (из вспышки) */
        case  14: return ai_flying_bird;     /* Селёдка (boss) — движение как у летуна */
        case TYPE_FLYING_BIRD: return ai_flying_bird; /* LEVEL1-start орлы */
        case  15: return ai_stone_golem;     /* каменный голем (anchor) */
        case  16: return ai_horizontal_projectile; /* horiz projectile */
        case  17: return ai_ground_walker;   /* скелет на страусе — бегает, НЕ кидает камни */
        case  18: return ai_walking_minion;  /* walking minion (Y-snap) */
        case  19: return ai_slow_helper;     /* slow helper */
        case 512: return ai_stone_man;       /* каменный человек (декор) */
        case 513: return ai_stub;            /* Karnov in-game state — physics.c */
        case TYPE_STONE_THROWER:   return ai_stone_thrower;
        case TYPE_ROCK_PROJECTILE: return ai_rock_projectile;
        case TYPE_PLAYER_FIREBALL: return ai_player_fireball;
        /* Type 7 invisible trigger, type 12, 20+: stub. */
        default:  return ai_stub;
    }
}

/* ─── API ──────────────────────────────────────────────────────────── */

/* spawn_rock — соответствует state 0 ai handler'а cs:0x11D6:0x08B3
 * (rolling rock с sprites #21-23). vx=±3 — байт-верифицированная скорость
 * из state 1 init блока (obj+0x10 = 0xFFFD = -3). */
static void spawn_rock(int x, int y, int vx, u16 frame)
{
    int slot = alloc_obj_slot();
    if (slot < 0) return;
    g_obj[slot] = (ai_obj_t){
        .alive       = true,
        .type        = TYPE_ROCK_PROJECTILE,
        .x           = x,
        .y           = y,
        .vx          = vx,
        .facing      = (vx < 0 ? -1 : 1),
        .spawn_frame = frame,
        .pos_x_q16   = x * 0x10000,
        .pos_y_q16   = y * 0x10000,
        .vx_q16      = vx * 0x10000,        /* ±3.0 px/frame (Q16.16) */
        .vy_q16      = ROCK_VY_INITIAL,     /* -1.5 (UP launch) */
        .grav_q16    = ROCK_GRAV_INITIAL,   /* ~0.666 px/frame² */
        .bounces     = 0,
    };
}

void ai_init_from_level(u16 level_num)
{
    g_num = 0;
    for (int i = 0; i < LEVEL_MAX_ENEMIES; ++i) g_alive_idx[i] = -1;
    memset(g_killed,         0, sizeof(g_killed));
    memset(g_one_shot,       0, sizeof(g_one_shot));
    memset(g_spawn_triggered, 0, sizeof(g_spawn_triggered));
    g_boss_dead = false;
    g_boss_dead_frame = 0;
    g_scroll_picked = false;
    /* Right edge инициализирован view_w — все spawn_x ≤ view_w считаются
     * «уже пройденными» (Karnov визуально видит их при старте, но эти
     * враги в LEVEL%d.BIN — будут спавниться только если scroll вернулся
     * назад и снова пошёл вправо, что обычно не случается). */
    /* Стартовое значение = −SPAWN_MARGIN, чтобы инициальный вызов
     * ai_spawn_pending(0, VIEW_W, 0) активировал ВСЕХ врагов с
     * spawn_x в [0, VIEW_W] (видимых на старте уровня). Раньше было
     * VIEW_W → инициальные враги никогда не спавнились. */
    g_max_right_edge = -SPAWN_MARGIN;

    if (level_num == 0) {
        /* LEVEL0 splash: всё спаунится upfront — узкий 320×120 экран без
         * прокрутки. Позиция врага — из spawn_x/spawn_y (декомпиляция
         * spawn_active_object cs:0x4F30: obj.x_world = spawn[0xC],
         * obj.y_world = spawn[0xE]). */
        for (u16 i = 0; i < level_num_enemies && g_num < MAX_AI; ++i) {
            const enemy_spawn_t *e = &level_enemies[i];
            g_one_shot[i] = true;             /* LEVEL0 → spawn один раз */
            if (e->type == 0) continue;
            if (e->type == 2) continue;
            if (e->type == 13) continue;      /* mosquito не виден на splash */
            if (e->spawn_x >= 320 || e->spawn_y >= 120) continue;
            g_obj[g_num] = (ai_obj_t){
                .alive     = true,
                .invisible = (e->type == 6),
                .type      = e->type,
                .x         = (int)e->spawn_x,
                .y         = (int)e->spawn_y,
                .facing    = -1,
                .spawn_x   = e->spawn_x,
                .spawn_y   = e->spawn_y,
                .level_idx = i,
            };
            g_alive_idx[i] = (s16)g_num;
            ++g_num;
        }
    } else {
        /* LEVEL1+: scroll-gated spawn в ai_spawn_pending. */
        if (getenv("KARNOV_DBG_L1_ENEMIES")) {
            for (u16 i = 0; i < level_num_enemies; ++i) {
                const enemy_spawn_t *e = &level_enemies[i];
                fprintf(stderr, "L%u en[%u]: type=%u sub=%u cur=(%u,%u) rng=(%u,%u) spawn=(%u,%u) trg=%u (hp=%u init=%u) flg=%04x\n",
                        level_num, i, e->type, e->subtype, e->cur_x, e->cur_y,
                        e->range_x, e->range_y, e->spawn_x, e->spawn_y,
                        e->trigger, (e->trigger >> 8), (e->trigger & 0xFF), e->flags);
            }
        }

        /* LEVEL1 start: «два орла» — sky-spawn навстречу Karnov-у. В оригинале
         * KARNOV.EXE генерируется через type 8 sky-spawn handler с PRNG,
         * AnimSeg @ DS:0x17E4 = [0x93, 0x93, 0x93, 0x94, 0x94, 0x94] =
         * sprites #147..148 повторённые по 3 раза (медленный взмах крыльев).
         *
         * Поведение из user-spec: два орла меняют высоту в ПРОТИВОФАЗЕ
         * друг к другу. `custom` поле = phase offset (0/1) — обработчик
         * ai_flying_bird осциллирует Y вокруг home_y c sin16[] таблицей,
         * нижний орёл сдвинут на 8 шагов (= 180°). */
        if (level_num == 1 && g_num + 2 <= MAX_AI) {
            g_obj[g_num] = (ai_obj_t){
                .alive       = true,
                .type        = TYPE_FLYING_BIRD, /* орёл, sprites #147-148 */
                .x           = 300,            /* world x — справа от Karnov-а */
                .y           = 250,            /* shared home_y, начинает вверху */
                .facing      = -1,
                .level_idx   = -1,
                .home_x      = 300, .home_y = 250,
                .half_range_x = 0, .half_range_y = 0,
                .custom      = 0,              /* phase=0 — верхний орёл */
                .spawn_x     = 300, .spawn_y = 250,
                .spawn_frame = 0,
            };
            ++g_num;
            g_obj[g_num] = (ai_obj_t){
                .alive       = true,
                .type        = TYPE_FLYING_BIRD,
                .x           = 300,
                .y           = 250,            /* same home_y */
                .facing      = -1,
                .level_idx   = -1,
                .home_x      = 300, .home_y = 250,
                .half_range_x = 0, .half_range_y = 0,
                .custom      = 1,              /* phase=π — нижний орёл, counterphase */
                .spawn_x     = 300, .spawn_y = 250,
                .spawn_frame = 0,
            };
            ++g_num;
        }
    }

    /* LEVEL0 SPLASH showcase items — hardcoded positions matching original
     * DOSBox observation (см. user screenshot 2026-05-26).
     * Sprite render anchor: y = sprite-bottom (y0 = obj.y - h),
     *                       x = body-center (x0 = obj.x - body_cx),
     * для sprite #39/#46 (16x12, body_cx=8) — обычный geom-center.
     *
     * Координаты подобраны эмпирически из render-pixels:
     *   - Karnov feet (red bbox y_max) = 107, stone-thrower feet = 111
     *   - Brown sand strip = y 112..114
     *   - KARNOV letter centers (yellow-density per col, y=50..75):
     *       K@63  A@95  R@126  N@159  O@192  V@225
     *   - R letter x-range 114..139, top edge ≈ y=40.
     *
     * Ladder icon: y=112 → sprite bottom at y=111 = непосредственно над верхом
     *              brown-sand-strip (y=112..114), т.е. иконка СИДИТ на земле.
     *              (Karnov в нашем рендере "висит" над sand'ом на 5 px из-за
     *              транспарентных строк низа sprite-а, но земля визуально —
     *              brown strip, и иконка должна быть на нём, как в DOSBox.)
     * Wings icon:  x=126 (center R), y=44 → sprite bottom at y=43 → overlap
     *              верха R на 3 px («немного заходит на букву R»). */
    if (level_num == 0 && g_num + 2 <= MAX_AI) {
        g_obj[g_num++] = (ai_obj_t){
            .alive = true, .type = 6,
            .x = 16, .y = 112,             /* ladder showcase — на земле (sand-strip top) */
            .facing = 1, .custom = 39,     /* sprite #39 = Ladder icon */
        };
        g_obj[g_num++] = (ai_obj_t){
            .alive = true, .type = 6,
            .x = 126, .y = 44,             /* wings showcase — над R, overlap top 3 px */
            .facing = 1, .custom = 45,     /* sprite #45 = Wings icon (user-verified 2026-05-29) */
        };
    }

    /* Splash-only декорации для LEVEL0: справа стоит зелёный stone-thrower
     * (мускулистый качок), периодически кидающий красные глыбы влево.
     * См. оригинальный скриншот splash KARNOV.EXE. В level_enemies этого
     * нет — это hardcoded splash-композиция (как `draw_stone_man(60,113)`
     * в karnov_cpp/src/splash.cpp:1306, но в другой позиции). */
    if (level_num == 0 && g_num < MAX_AI) {
        g_obj[g_num] = (ai_obj_t){
            .alive       = true,
            .type        = TYPE_STONE_THROWER,
            .x           = 254,    /* DOSBox-verified: green cluster center=254 */
            .y           = 113,    /* DOSBox-verified: green feet at y=113 */
            .facing      = -1,           /* смотрит влево, к Karnov-у */
            .spawn_frame = 0,
        };
        ++g_num;
    }
}

void ai_tick(u16 frame, u8 karnov_input)
{
    /* Прежде здесь был static-auto-init, вызывавший
     * ai_init_from_level(0). При запуске напрямую с --level 1 он сбрасывал
     * g_pending_idx уже после prepare_level → каждый тик re-spawn'ил тех
     * же врагов, индексы дублировались, активные слоты росли необратимо.
     * Удалено: prepare_level() в game.c — единственная точка инициализации. */
    if (getenv("KARNOV_DBG_THROWERS_COUNT") && (frame % 50) == 0) {
        int n = 0;
        for (int i = 0; i < g_num; ++i)
            if (g_obj[i].alive && (g_obj[i].type == 17 || g_obj[i].type == TYPE_STONE_THROWER))
                ++n;
        fprintf(stderr, "f=%u throwers=%d g_num=%d\n", frame, n, g_num);
    }
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) continue;
        pick_handler(g_obj[i].type)(&g_obj[i], frame, karnov_input);
    }

    /* После убийства boss'а через ~30 ticks спавним свиток (scroll item).
     * custom=999 — sentinel marker (НЕ реальный sprite ID #47 = акваланг).
     * Реальный sprite свитка не верифицирован, рендер пока fallback. */
    if (g_boss_dead && !g_scroll_picked) {
        bool scroll_exists = false;
        for (int i = 0; i < g_num; ++i) {
            if (g_obj[i].alive && g_obj[i].type == 5 && g_obj[i].custom == 999) {
                scroll_exists = true; break;
            }
        }
        if (!scroll_exists && (frame - g_boss_dead_frame) >= 30) {
            int slot = alloc_obj_slot();
            if (slot >= 0) {
                int kx = physics_get_x();
                g_obj[slot] = (ai_obj_t){
                    .alive       = true,
                    .type        = 5,             /* падающий бонус */
                    .x           = kx,            /* над Karnov-ом */
                    .y           = 50,            /* высоко над землёй */
                    .vy          = 1,
                    .facing      = 1,
                    .custom      = 999,           /* sentinel: boss-reward scroll */
                    .level_idx   = -1,
                    .spawn_frame = frame,
                };
            }
        }
    }
}

/* Карта типа → sprite-id для рендера.
 *
 * User-verified sprite ranges (2026-05-28):
 *   #76..85   — Зелёный стражник с мечом (10 фаз)
 *   #86..90   — Зелёный каменный человек (5 фаз) → splash stone-thrower
 *   #91..94   — Комарик (4 фазы) → Y-tracker (тип 13)
 *   #147..148 — Летящая зелёная птица (2 фазы) → TYPE_FLYING_BIRD (LEVEL1 орлы)
 *                и type 16 (горизонтальный летун)
 *   #149..152 — Селёдка (4 фазы) → тип 14 (boss в конце LEVEL1; decomp
 *                ai_type14 spawn sprite 0x95=149)
 *   #153..160 — Чёртик (8 фаз) → walking minion (тип 18)
 *   #161..163 — Скелет на страусе (3 фазы) → ostrich (тип 17)
 *   #164..171 — Демон на облаке (8 фаз) → pendulum (тип 10)
 *
 * Type 6 (OPTIONS) — намеренно НЕТ записи: invisible-флаг отсекает рендер
 * раньше; даже если просочится, sprite_id 0xFFFF / NULL даст fallback-плейсхолдер. */
static u16 sprite_for_type(u16 type, u16 anim_phase)
{
    switch (type) {
        case   5: {                                /* падающий бонус — option icon */
            /* User-verified 2026-05-29 (project_inventory_items.md):
             *   #39 ladder, #40 boots, #41 bomb, #42 fireballs(30),
             *   #43 boomerang, #44 mask, #45 wings, #47 aqualung,
             *   #48 treasure, #49 multi-shot.
             * #46 не указан — gap. Anim_phase крутит 10 опций. */
            static const u16 opts[10] = { 39, 40, 41, 42, 43, 44, 45, 47, 48, 49 };
            return opts[anim_phase % 10];
        }
        case  10: {                                /* Демон на облаке — 8 фаз */
            return 164 + (anim_phase & 7);
        }
        case  11: {                                /* Зелёный стражник с мечом — 10 фаз */
            return 76 + (anim_phase % 10);
        }
        case  13: {                                /* Зелёный каменный МЕТАТЕЛЬ — 5 фаз (#86-90).
                                                    * type13 INIT = "Y-snap + spawn FX" (вспышка),
                                                    * т.е. метатель, появляющийся из вспышки
                                                    * (user 2026-07-04, images 4/6/9). Раньше
                                                    * ошибочно рисовался комариком #91-94. */
            return 86 + (anim_phase % 5);
        }
        case  14: {                                /* Селёдка (boss / flying minion) — 4 фазы.
                                                    * Декомпиляция ai_type14 (file_io.h:184):
                                                    * spawn(obj, sprite=0x95=149). */
            return 149 + (anim_phase & 3);
        }
        case TYPE_FLYING_BIRD: {                    /* Летящая зелёная птица — 2 фазы (#147-148).
                                                    * type8 sky-spawn AnimSeg 0x93/0x94. */
            return 147 + (anim_phase & 1);
        }
        case  15: {                                /* каменный голем (anchor) — оставляем 178/179 пока неуточнено */
            static const u16 golem[2] = { 178, 179 };
            return golem[anim_phase & 1];
        }
        case  16: {                                /* Горизонтальный летящий враг (type16,
                                                    * vx≈-1.5) — рендерим как птицу #147-148.
                                                    * (Раньше ошибочно = Селёдка #149-152,
                                                    * из-за чего было несколько «селёдок».) */
            return 147 + (anim_phase & 1);
        }
        case  17: {                                /* Скелет на страусе — 3 фазы (#161-163).
                                                    * Декомпиляция ai_type17 (file_io.h:198):
                                                    * spawn(sprite=0xA1=161). Раньше ошибочно
                                                    * = зелёный каменный человек #86-90. */
            return 161 + (anim_phase % 3);
        }
        case  18: {                                /* Чёртик (walking minion) — 8 фаз */
            return 153 + (anim_phase & 7);
        }
        case  19: {                                /* slow helper — оставляем 172/173 пока неуточнено */
            static const u16 slow[2] = { 172, 173 };
            return slow[anim_phase & 1];
        }
        case 512: {                                /* stone man декорация (splash) — Зелёный каменный человек */
            static const u16 stone[5] = { 86, 87, 88, 89, 90 };
            return stone[anim_phase % 5];
        }
        case TYPE_STONE_THROWER: {
            /* Зелёный каменный человек: 5-кадровая анимация: idle (#86/#87) →
             * windup (#88) → release (#89) → follow-through (#90). Rock spawns
             * на переходе #88→#89. */
            static const u16 frames[5] = { 86, 87, 88, 89, 90 };
            return frames[anim_phase < 5 ? anim_phase : 0];
        }
        case TYPE_PLAYER_FIREBALL:
            return (anim_phase & 1) ? SPRITE_PLAYER_FIREBALL_B : SPRITE_PLAYER_FIREBALL_A;
        case TYPE_ROCK_PROJECTILE: {
            /* Rolling rock — байт-точный цикл AnimSeg DS:0x18EC: #21 → #22 → #23
             * (3 фазы; см. ai_rock_projectile). */
            static const u16 rock[3] = { 21, 22, 23 };
            return rock[anim_phase % 3];
        }
        default:  return 0xFFFF;                   /* нет спрайта — placeholder rect */
    }
}

/* Мелкие летуны рисуются в фоновом проходе, чтобы не закрывать наземных
 * врагов. В LEVEL1 комарик (type13, Y-tracker) местами стоит в одной точке
 * с зелёным каменным человеком (type17) — записи в LEVEL1.BIN совпадают по
 * координатам (напр. idx65/idx66 — одинаковые cur И spawn). Без разделения
 * слоёв мошка рисовалась поверх и выглядела «вместо» человека. */
static bool ai_is_background_flyer(u16 type)
{
    return type == 16 || type == TYPE_FLYING_BIRD;
}

void ai_render(bool mode_cga, int scroll_x, int scroll_y)
{
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) continue;
        if (g_obj[i].invisible) continue;  /* OPTIONS items на splash */
        /* Пасс 0 — только фоновые летуны; пасс 1 — все остальные поверх. */
        if (ai_is_background_flyer(g_obj[i].type) != (pass == 0)) continue;
        u16 sid;
        if (g_obj[i].type == 6 && g_obj[i].custom > 0) {
            /* LEVEL0 splash OPTIONS showcase: custom = sprite_id для item icon. */
            sid = (u16)g_obj[i].custom;
        } else if (g_obj[i].type == 13) {
            /* type13: наземные (home_y≥250, на тумбах) — зелёные каменные
             * МЕТАТЕЛИ #86-90; поднятые над деревьями (home_y≤240, idx47/48) —
             * ЧЁРТИКИ #153-160 (user 2026-07-05). */
            sid = (g_obj[i].home_y <= 240) ? (153 + (g_obj[i].anim_phase & 7))
                                           : (86  + (g_obj[i].anim_phase % 5));
        } else if (g_obj[i].type == 18) {
            /* type18 разделяется по range_y: большой вертикальный ход (rng_y=100,
             * half=50) — ЧЁРТИК на дереве (#153-160); малый (rng_y=20, half=10) —
             * ДЕМОН НА ОБЛАКЕ в замке (#164-171). См. скриншоты 2026-07-05. */
            u16 base = (g_obj[i].half_range_y >= 30) ? 153 : 164;
            sid = base + (g_obj[i].anim_phase & 7);
        } else {
            sid = sprite_for_type(g_obj[i].type, g_obj[i].anim_phase);
        }
        const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
        if (s) {
            bool flip = g_obj[i].facing < 0;
            /* Anchor: body_cx (precomputed body center). Обеспечивает
             * совпадение visible body center с g_obj[i].x в обоих facings. */
            int anchor = flip ? (s->w - 1 - s->body_cx) : s->body_cx;
            int x0 = g_obj[i].x - anchor - scroll_x;
            int y0 = g_obj[i].y - s->h - scroll_y;
            u8 ck = sprite_is_cga() ? 0xFF : 1;
            const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
            for (int r = 0; r < s->h; ++r) {
                int yy = y0 + r;
                if (yy < 0 || yy >= WIN_H) continue;
                for (int c = 0; c < s->w; ++c) {
                    int sc = flip ? (s->w - 1 - c) : c;
                    u8 v = s->pixels[r * s->w + sc];
                    if (v == 0xFF) continue;
                    if (v == ck) continue;
                    int xx = x0 + c;
                    if (xx < 0 || xx >= WIN_W) continue;
                    render_framebuffer()[yy * WIN_W + xx] =
                        sprite_is_cga() ? pal[v & 3] : pal[v & 15];
                }
            }
            continue;
        }
        /* Плейсхолдер: цветной прямоугольник по типу врага. Это важно для
         * визуального дебага scroll-спавна — большинство LEVEL1+ типов
         * ещё не имеют sprite-маппинга в sprite_for_type, но «коробочки»
         * показывают, что AI был активирован и движется. */
        u32 col;
        int pw = 12, ph = 12;
        switch (g_obj[i].type) {
            case 13:  col = 0xFF55FF55u; break;  /* зелёный Y-tracker */
            case 512: col = 0xFFAAAAAAu; break;  /* серый stone-man */
            case TYPE_PLAYER_FIREBALL: col = 0xFFFFFF55u; pw=8; ph=4; break;
            case TYPE_ROCK_PROJECTILE: col = 0xFFFF5555u; pw=10; ph=10; break;
            /* LEVEL1 types — отдельные цвета для отладки. */
            case  5: col = 0xFFFFAA00u; break;  /* падающий бонус */
            case  6: col = 0xFFFF66FFu; break;  /* OPTIONS-item */
            case  7: col = 0xFF66FFFFu; break;  /* мини-летун */
            case  8: col = 0xFF8888FFu; break;  /* sky-spawn */
            case  9: col = 0xFFFFFFFFu; break;  /* enemy 9 */
            case 10: col = 0xFFCC66CCu; break;  /* enemy 10 */
            case 11: col = 0xFFAA5500u; break;  /* «snake»/наземный */
            case 14: col = 0xFFAAAA00u; break;
            case 15: col = 0xFFFF00AAu; break;
            case 16: col = 0xFF00AAFFu; break;  /* gun turret */
            case 17: col = 0xFF55AA55u; break;  /* страус */
            case 18: col = 0xFFAA00AAu; break;
            case 19: col = 0xFF008888u; break;
            default: col = 0xFF5555FFu; break;
        }
        render_fill_rect(g_obj[i].x - pw/2 - scroll_x,
                         g_obj[i].y - ph - scroll_y, pw, ph, col);
    }
  }
  (void)mode_cga;
}

/* ─── Pickup detection ─────────────────────────────────────────────────
 * Karnov bbox ≈ 16×24 от (kx, ky) = центр-ног. Items (type 6, custom>0) —
 * это showcase-иконки 16×12 с anchor (cx=body_cx=8, y=bottom). Простое
 * AABB-пересечение. */
void ai_try_pickup(int kx, int ky)
{
    int kx0 = kx - 8, kx1 = kx + 8;
    int ky0 = ky - 24, ky1 = ky;
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *o = &g_obj[i];
        if (!o->alive) continue;
        /* Scroll-свиток (type=5 custom=999) — boss-награда, sentinel ID.
         * Реальный sprite ID свитка-конца-уровня НЕ верифицирован — пока
         * 999 = sentinel чтобы не путать с реальными items #39..#49. */
        if (o->type == 5 && o->custom == 999) {
            int ix0 = o->x - 8, ix1 = o->x + 8;
            int iy0 = o->y - 12, iy1 = o->y;
            if (kx1 < ix0 || kx0 > ix1) continue;
            if (ky1 < iy0 || ky0 > iy1) continue;
            o->alive = false;
            g_scroll_picked = true;
            continue;
        }
        if (o->type != 6) continue;
        if (o->custom <= 0) continue;          /* invisible items без showcase */
        /* Item bbox: y = bottom, body_cx=8 → (x-8..x+8, y-12..y). */
        int ix0 = o->x - 8, ix1 = o->x + 8;
        int iy0 = o->y - 12, iy1 = o->y;
        if (kx1 < ix0 || kx0 > ix1) continue;
        if (ky1 < iy0 || ky0 > iy1) continue;
        /* Подобрали. Mapping sprite_id → item kind.
         * User-verified 2026-05-29 (project_inventory_items.md):
         *   #39 ladder, #45 wings (НЕ #46!). Остальные TODO. */
        switch (o->custom) {
            case 39: inv_give_ladder(); break;
            case 45: inv_give_wings();  break;
            default:                    break;   /* TODO: boots/bomb/etc */
        }
        o->alive = false;   /* убираем из мира */
    }
}

/* ─── Scroll-gated spawn ───────────────────────────────────────────────
 *
 * Враги в `level_enemies[]` уже отсортированы по cur_x по конструкции
 * самого файла LEVEL%d.BIN (наблюдается на LEVEL1: cur_x растёт от 0 до
 * 1632 монотонно). Курсор g_pending_idx продвигается только вперёд: когда
 * cur_x пересекает (scroll_x + view_w + SPAWN_MARGIN), враг превращается
 * в активный g_obj и индекс инкрементируется. Возврат камеры назад врагов
 * не ре-спаунит — это поведение оригинала (одноразовая активация).
 *
 * Что отфильтровываем:
 *   - type == 0  — пустая запись/терминатор.
 *   - type == 2  — Karnov-spawn-marker.
 *   - cur_x >= ULTRA_HIGH (32000) — sentinel-маркер конца списка.
 *   - type == 8  с cur=(0,0) и spawn=(0,0) — sky-spawn template (sky-spawn
 *     enemies формируются динамически в ai_type08, не из этой записи).
 */
/* Найти свободный слот в g_obj[] (alive=false) или вернуть g_num (если все
 * заняты и есть запас в массиве). Возвращает -1 если массив переполнен. */
static int alloc_obj_slot(void)
{
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) return i;
    }
    if (g_num < MAX_AI) return g_num++;
    return -1;
}

/* User-spec (2026-05-29 — Image #8 reference): при старте уровня видны
 * ТОЛЬКО декорации (дракон, платформа, Karnov) — НИ ОДНОГО врага. Только
 * при дальнейшем scroll'е, когда триггер cur_x ПЕРЕСЕКАЕТ правый край
 * viewport'а из-вне-кадра-вправо в кадр, враг материализуется. */
void ai_spawn_pending(int scroll_x, int view_w, u16 current_frame)
{
    bool dbg = getenv("KARNOV_DBG_SPAWN") != NULL;
    int cur_right_edge = scroll_x + view_w;
    if (cur_right_edge > g_max_right_edge) g_max_right_edge = cur_right_edge;

    for (u16 i = 0; i < level_num_enemies; ++i) {
        const enemy_spawn_t *e = &level_enemies[i];

        if (e->type == 0) continue;             /* пустой */
        if (e->type == 2) continue;             /* sentinel / Karnov spawn marker */
        if (e->type == 8) continue;             /* sky-spawn template (динамический) */
        if (g_killed[i]) continue;              /* уже убит */
        if (g_alive_idx[i] >= 0) continue;      /* уже активен в g_obj */
        if (g_one_shot[i]) continue;            /* пикапы — однократно */
        if (g_spawn_triggered[i]) continue;     /* одноразовая активация:
                                                 * despawn'нутый враг НЕ
                                                 * ре-спаунится (как в оригинале
                                                 * — возврат камеры назад не
                                                 * воскрешает пройденных). */

        /* Разделение полей записи (верифицировано на LEVEL1/2/3, декомпиляция
         * spawn_active_object cs:0x4F30 + damage pair-loop level.cpp):
         *
         *   cur_x/cur_y  (offset 0x04/0x06) — ТРИГГЕР спавна: враг рождается,
         *                когда правый край viewport'а доходит до cur_x. НЕ
         *                позиция!
         *   spawn_x/spawn_y (0x0C/0x0E) — ПОЗИЦИЯ размещения (obj.x_world,
         *                obj.y_world).
         *
         * Раньше эти роли путались: spawn_x использовался и как позиция, и как
         * триггер, из-за чего координаты смотрелись сдвинутыми. */

        /* Wave-queue: подряд идущие записи ОДНОГО типа, размещённые вплотную
         * (|Δcur_x|≤32, |Δcur_y|≤16), — это ОЧЕРЕДЬ, а не одновременная группа.
         * Появляются по одному: следующий член — только когда предыдущий УЖЕ
         * появлялся И его сейчас нет на экране (убит или ушёл за край). Без
         * этого 5 «зелёных каменных» (LEVEL1 idx20-24, spawn_x с шагом 8) вылезли
         * бы кучей в 32 px и слились в зелёное пятно.
         *
         * Прогресс по «предыдущий вышел из игры» (а не только «убит»), чтобы
         * очередь не застревала, если игрок пробежал мимо не убив. Одноразовый
         * латч g_spawn_triggered гарантирует, что каждый член появится ровно раз
         * (без ре-спауна при возврате камеры). */
        /* Пикапы (type 5 — падающий бонус, 6 — OPTIONS) — это размещённые
         * предметы, а не очередь врагов: их не ставим в wave-queue. */
        if (i > 0 && e->type != 5 && e->type != 6) {
            const enemy_spawn_t *p = &level_enemies[i - 1];
            int dcx = (int)e->cur_x - (int)p->cur_x; if (dcx < 0) dcx = -dcx;
            int dcy = (int)e->cur_y - (int)p->cur_y; if (dcy < 0) dcy = -dcy;
            if (p->type == e->type && dcx <= 32 && dcy <= 16) {
                if (!g_spawn_triggered[i - 1]) continue; /* предыдущий ещё не появлялся */
                if (g_alive_idx[i - 1] >= 0)  continue;  /* предыдущий ещё на экране */
            }
        }

        /* Координаты врагов по X — ×2 (world = 2 × file). Так враги
         * распределяются по всей ширине уровня (3600), а босс (spawn_x=1730)
         * оказывается в конце (~3460). Без ×2 они ужаты в левую половину и
         * босса в конце нет (user 2026-07-05: «верни как было»). Y — 1:1. */
        int tx = (int)e->cur_x  * 2;            /* триггер по X (world) */
        int rx = (int)e->spawn_x * 2;           /* позиция X (world) */
        int ry = (int)e->spawn_y;               /* позиция Y */

        /* First-time activation: cur_x впервые попал в правый край viewport'а
         * (с margin). Инициальный g_max_right_edge=−SPAWN_MARGIN, поэтому
         * враги с cur_x ≤ VIEW_W материализуются на первых тиках. */
        if (tx > cur_right_edge + SPAWN_MARGIN) continue;   /* триггер ещё впереди */
        g_spawn_triggered[i] = true;

        int slot = alloc_obj_slot();
        if (slot < 0) {
            if (dbg) fprintf(stderr, "spawn_pending: g_obj full @ entry=%u\n", i);
            break;
        }
        if (dbg) {
            fprintf(stderr, "spawn[%u]: type=%u spawn=(%d,%d) slot=%d\n",
                    i, e->type, rx, ry, slot);
        }

        /* HP из старшего байта поля trigger (декомпиляция spawn_active_object:
         * obj.hit_points = spawn[0x11] — high byte of WORD at offset 0x10).
         * Младший байт — init_arg/sub_timer. Если 0 — fallback hp=1.
         * Boss = HP ≥ 50 (только Селёдка в конце LEVEL1: hp=100). */
        int hp = (int)(e->trigger >> 8);
        if (hp == 0) hp = 1;
        bool boss = (hp >= 50);
        /* Type 6 (OPTIONS) и type 5 (падающий бонус) — однократные. */
        if (e->type == 5 || e->type == 6) g_one_shot[i] = true;

        g_obj[slot] = (ai_obj_t){
            .alive        = true,
            .invisible    = (e->type == 6 && (int)level_num_map_rows > 12),
            .is_boss      = boss,
            .type         = e->type,
            .x            = rx,
            .y            = ry,
            .facing       = -1,
            .hp           = hp,
            .level_idx    = (int)i,
            .home_x       = rx,
            .home_y       = ry,
            .half_range_x = (int)e->range_x,        /* range_x×2 (world) / 2 */
            .half_range_y = (int)e->range_y / 2,
            .spawn_x      = (u16)rx,
            .spawn_y      = (u16)ry,
            .spawn_frame  = current_frame,
        };
        g_alive_idx[i] = (s16)slot;
    }
}

/* Despawn врагов, ушедших слишком далеко за левый край viewport.
 * Освобождённые слоты переиспользует ai_spawn_pending.
 * Вызывается каждый тик из game.c после update_scroll. */
void ai_despawn_offscreen(int scroll_x)
{
    /* Despawn zone: 128 px за левым краем И 128 px за правым краем viewport.
     * Despawn освобождает g_obj-слот и СБРАСЫВАЕТ g_alive_idx — при возврате
     * scroll'а enemy re-spawn'нется через ai_spawn_pending.
     * Killed-флаг (g_killed) этим НЕ затрагивается — убитые остаются мёртвыми. */
    /* despawn_right с запасом 256: враг размещается в spawn_x, который может
     * быть на ~112 px правее своего триггера cur_x. При узком правом margin
     * такой враг был бы срезан в тот же тик, когда родился, и (из-за
     * одноразовой активации) больше не появился бы. 256 > 112+SPAWN_MARGIN. */
    int despawn_left  = scroll_x - 128;
    int despawn_right = scroll_x + VIEW_W + 256;
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) continue;
        /* Не дезактивируем projectiles (player fireball) и items — они
         * сами завершаются. Только обычных врагов LEVEL1+. */
        u16 t = g_obj[i].type;
        if (t == TYPE_PLAYER_FIREBALL) continue;
        if (t == 6) continue;             /* OPTIONS items — не трогаем */
        if (g_obj[i].is_boss) continue;   /* boss остаётся пока не убит */
        if (g_obj[i].x < despawn_left || g_obj[i].x > despawn_right) {
            g_obj[i].alive = false;
            int li = g_obj[i].level_idx;
            if (li >= 0 && li < LEVEL_MAX_ENEMIES) g_alive_idx[li] = -1;
        }
    }
}

/* Player hit detection: вызывается из game.c когда player-fireball
 * пересекает врага. Помечает врага killed (если HP→0). При смерти boss'а
 * запускает scroll-drop sequence. */
void ai_notify_enemy_hit(int slot, u16 frame)
{
    if (slot < 0 || slot >= g_num) return;
    ai_obj_t *o = &g_obj[slot];
    if (!o->alive) return;
    /* Урон = PLAYER_FIREBALL_DAMAGE (пул HP врага уменьшается на урон
     * снаряда, как `b->hit_points -= a->hit_points` в оригинале). */
    o->hp -= PLAYER_FIREBALL_DAMAGE;
    if (o->hp > 0) return;
    o->alive = false;
    int li = o->level_idx;
    if (li >= 0 && li < LEVEL_MAX_ENEMIES) {
        g_killed[li]    = true;
        g_alive_idx[li] = -1;
    }
    if (o->is_boss) {
        g_boss_dead       = true;
        g_boss_dead_frame = frame;
    }
}

bool ai_boss_dead(void)        { return g_boss_dead;    }
bool ai_scroll_picked(void)    { return g_scroll_picked; }
u16  ai_boss_dead_frame(void)  { return g_boss_dead_frame; }

/* ─── Player fireball spawn ──────────────────────────────────────────── */
void ai_spawn_player_fireball(int x, int y, int facing, u16 frame)
{
    /* Через alloc_obj_slot() — переиспользует слоты dead enemies/fireballs,
     * иначе через ~30 выстрелов g_num достигает MAX_AI=64 и Karnov только
     * принимает позу стрельбы без вылета снарядов. */
    int slot = alloc_obj_slot();
    if (slot < 0) return;
    int vx = (facing < 0) ? -PLAYER_FIREBALL_SPEED : +PLAYER_FIREBALL_SPEED;
    g_obj[slot] = (ai_obj_t){
        .alive       = true,
        .type        = TYPE_PLAYER_FIREBALL,
        .x           = x,
        .y           = y,
        .vx          = vx,
        .facing      = (facing < 0) ? -1 : +1,
        .spawn_frame = frame,
    };
}
