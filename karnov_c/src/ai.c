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
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Пул объектов оригинала = 128 (init-циклы `cmp si,0x80` @ file 0x1F30/
 * 0x1F87; две parity-очереди по 64). При переполнении spawn просто не
 * удаётся — запись остаётся невзведённой и ретраится следующим кадром. */
#define MAX_AI 128

/* Splash-only psevdo-типы (выше 1000, не пересекаются с KARNOV.EXE).
 * Реальные типы [1..57] — это enemy types из ai_scripts.cpp. */
#define TYPE_STONE_THROWER     1001  /* зелёный качок справа splash, кидает глыбы */
#define TYPE_ROCK_PROJECTILE   1002  /* катящаяся красная глыба */
#define TYPE_PLAYER_FIREBALL   1003  /* снаряд Karnov-а (FIRE press) */
#define TYPE_FLYING_BIRD       1004  /* LEVEL1-start «два орла» (sky-spawn, sprites
                                      * #147-148). Отдельный псевдо-тип, чтобы не
                                      * путать с реальным type 14 = Селёдка. */
#define TYPE_BOMB_BLAST        1005  /* анимированная вспышка Bomb-предмета
                                      * (#25→#26→#27→#28, ~50 кадров). */

/* Sprite IDs для player-fireball — мелкие круглые «шарики» (7x7).
 * #0x0A/#0x0B — маленькие round energy-ball'ы (4-кадровый набор 0x0A-0x0D).
 * Прежние варианты: #19/#20 (кресты), #0xB9/#0xBA (16px fireball — слишком
 * большой). Конкретный кадр выбирается по o->anim_phase. */
#define SPRITE_PLAYER_FIREBALL_A  0x0A
#define SPRITE_PLAYER_FIREBALL_B  0x0B

/* Уязвимость для выстрела Karnov'а — см. is_damageable_obj ниже
 * (после typedef ai_obj_t): решается по subtype (obj_type +0xE5), как
 * в pair-loop THINKCYCLE оригинала, а не по списку primary-типов. */
struct ai_obj;
static bool is_damageable_obj(const struct ai_obj *o);

/* Многосегментные враги (типы 20, 21, 30, 33, 34, 36, 39, 50, 51):
 * до 8 частей-«bbox» как в оригинальном ActiveObject (+0x1E num_bboxes,
 * +0x20.. массив SpriteBox). Часть = спрайт + смещение от (x, y). */
#define MAX_PARTS 8
typedef struct ai_part {
    u16 sprite;
    s16 dx, dy;
} ai_part_t;

typedef struct ai_obj {
    bool   alive;
    bool   invisible;     /* OPTIONS items (type 6) — на splash скрыты */
    bool   is_boss;       /* type 14 в конце уровня (Селёдка) */
    u16    type;          /* PRIMARY type = индекс AI-обработчика (spawn[0]) */
    u16    subtype;       /* EnemySpawn.subtype → obj_type (+0xE5): категория
                           * pair-loop: 0=Karnov-маркер, 1/7=враг, 4=спец,
                           * 5=предмет, 9=multi-bbox/триггер. */
    u16    init_arg;      /* младший байт trigger → init-аргумент обработчика
                           * (оригинал: spawn[0x10] → sub_timer). */
    int    num_parts;     /* 0 = обычный односпрайтовый рендер через
                           * sprite_for_type; >0 = массив parts[]. */
    ai_part_t parts[MAX_PARTS];
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
    u16    item_sprite;   /* type 5 (падающий предмет): sprite_id = 0x27+item_id (#39..49) */
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

/* Текущий кадр — записывается в начале ai_tick, читается функциями вне
 * тик-цикла (ai_bomb_explode) для spawn_frame анимации вспышки. */
static u16      g_ai_frame = 0;

/* Номер текущего уровня (для level-зависимых обработчиков: type 49
 * активен только на LEVEL9). Не static: читается extern-объявлением. */
u16 g_ai_level_num = 0;

/* Состояние спауна живёт в самих записях level_enemies[]: поле flags —
 * one-shot защёлка (0 = взведена, 1 = отработала), как в оригинале
 * (check_triggers @ file 0x5011 пропускает flags≠0; spawn_active_object
 * @ file 0x4FFE ставит flags=1 только при успешном alloc). Перевзводит
 * записи ТОЛЬКО rearm-цикл после смерти игрока (file 0x1015A). Никакого
 * центрального деспавна/респавна-при-возврате-камеры в оригинале нет. */

/* Boss-конец: когда goal-объект убит — g_boss_dead=true и через 30 ticks
 * спавним свиток (scroll item) сверху. После пикапа Karnov-ом инициируем
 * level-end transition. Goal-объект определяется данными (check_triggers
 * file 0x517F..0x518D): на уровне 9 — запись type 56, на остальных —
 * последняя (по sort cur_x) запись таблицы спаунов. */
static bool g_boss_dead = false;
static u16  g_boss_dead_frame = 0;
static bool g_scroll_picked = false;
static int  g_boss_x = 0;   /* X boss'а на момент смерти — фикс. точка падения свитка */

/* Sky-spawner (драйвер file 0x12DB5) — работает ВНЕ объектной системы.
 * Запись type 8 cur=(0,0) range=(0,0) в BIN никогда не спавнится сама
 * (range=0 при строгом «<») — она лишь маркер «уровень имеет sky-спаун».
 * Состояние: последняя точка прогресса игрока (DS:0xD8A0) и счётчик
 * простоя (DS:0xD8A4). */
static bool s_sky_enabled = false;
static int  s_sky_last_x_units = 0;
static int  s_sky_idle = 0;


static void spawn_rock(int x, int y, int vx, u16 frame);
static int alloc_obj_slot(void);

/* ─── PRNG ──────────────────────────────────────────────────────────────
 * Общий генератор для всех PRNG-типов (6, 8, 25, 44, 45, 47) — Turbo-C
 * rand() LCG (cs:0x1CE7:0x17): seed = seed*0x015A4E35 + 1; старшие 15 бит.
 * Сидируется в ai_init_from_level → детерминизм демо-воспроизведения. */
static u32 g_rng_seed = 1;

static u16 rng_next(void)
{
    g_rng_seed = g_rng_seed * 0x015A4E35u + 1u;
    return (u16)((g_rng_seed >> 16) & 0x7FFF);
}

/* Урон/подбор: категория по subtype (obj_type +0xE5), как в pair-loop
 * THINKCYCLE (think.cpp, jump-table cs:0xF56 по b.obj_type-1):
 *   subtype 5 → предмет (неуязвим для выстрела, подбирается касанием);
 *   subtype 0 → Karnov-маркер;
 *   type 7 (триггер), 8 (спаунер), 56 (level-end), 57 (декор) — не цели.
 * Псевдотипы порта — по своему списку. */
static bool obj_is_item(const ai_obj_t *o)
{
    return o->type == 4 || o->type == 5 || o->type == 6;
}

static bool is_damageable_obj(const struct ai_obj *o)
{
    /* Псевдотипы порта. */
    switch (o->type) {
        case TYPE_STONE_THROWER:
        case TYPE_ROCK_PROJECTILE:
        case TYPE_FLYING_BIRD:
            return true;
        case TYPE_PLAYER_FIREBALL:
        case TYPE_BOMB_BLAST:
            return false;
        default: break;
    }
    if (o->type == 0 || o->type > 57) return false;      /* маркеры/декор 512+ */
    if (o->type <= 3) return false;                       /* Karnov (типы 1-3) */
    if (obj_is_item(o)) return false;                     /* предметы: subtype 5,
                                                           * default-ветка jump-table
                                                           * cs:0xFCEC → нет урона */
    if (o->type == 7 || o->type == 8) return false;       /* триггер / спаунер */
    if (o->type == 56 || o->type == 57) return false;     /* level-end / декор */
    if (o->subtype == 5 || o->subtype == 0) return false;
    /* type 9 в LEVEL1 — декоративный дракон-платформа (hp=0, subtype=4):
     * не рисуется (draw_level1_dragon) и не должен убиваться. */
    if (o->type == 9 && o->hp <= 1) return false;
    return true;
}

/* ─── Handlers ──────────────────────────────────────────────────────── */

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
        /* Звук отскока камня — первые 3 удара (как +0xE9<3 в ai_type12,
         * KARNOV.EXE @ file 0x13F89, play_sound(8)). */
        if (o->bounces < 3) audio_play_sfx(8);
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
        if (!is_damageable_obj(e) && !e->is_boss) continue;
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

/* TYPE_BOMB_BLAST — анимированная вспышка Bomb-предмета. Растёт через
 * спрайты #25→#26→#27→#28 (16×6, 32×9, 32×15, 32×19 — уже в кэше) за ~50
 * кадров, затем исчезает. Врагов уже убил ai_bomb_explode() при спавне. */
static void ai_bomb_blast(ai_obj_t *o, u16 frame, u8 input)
{
    (void)input;
    u16 age = (u16)(frame - o->spawn_frame);
    o->anim_phase = age / 13;
    if (age >= 50) o->alive = false;
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

    /* Phase-based movement: walk if far (abs_dx > 10), stop if close.
     * 2 px/frame — под ×2-мир враги двигались втрое медленнее орлов (было
     * 0.5 px/frame); ускорено до нормального шага (user 2026-07-06). */
    if (abs_dx > 10) {
        o->x += 2 * o->facing;
    }
    /* НЕТ позиционного clamp'а home±range_x: в оригинале ai_type11 гонится за
     * игроком без границы, останавливается по ДИСТАНЦИИ (phase toggle на
     * abs_dx≈10 выше). range_x в файле = маленькая подсказка патруля (5-15px),
     * а НЕ граница преследования — раньше клампился сюда и враг застревал
     * в ~10px от точки спавна, не доходя до Karnov (user 2026-07-07). */
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
    int dx = kx - o->x;
    int abs_dx = dx < 0 ? -dx : dx;
    o->facing = (dx >= 0) ? 1 : -1;
    /* 8-кадровый walk-cycle (sub_timer_max=3 × stride ≈ 3 = 9 ticks/кадр). */
    o->anim_phase = (frame >> 2) & 7;

    /* Идёт к игроку по X — 2 px/frame. Останавливается вплотную (abs_dx≤8),
     * чтобы не дёргаться поверх Karnov'а. НЕТ позиционного clamp'а home±range:
     * оригинал ai_type18 преследует игрока, range_x/y в файле — маленькие
     * подсказки (10px), а не границы. Раньше клампилось и демон застревал
     * у точки спавна (user 2026-07-07). */
    if (abs_dx > 8) o->x += 2 * o->facing;
    /* Y-snap — медленное подтягивание к Y игрока (1 px каждые 8 кадров), без
     * clamp'а, чтобы демон-на-облаке снижался к Karnov'у. */
    if ((frame & 7) == 0) {
        if (ky < o->y - 1)      o->y -= 1;
        else if (ky > o->y + 1) o->y += 1;
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
    /* Комарик — 4-фазный взмах крылышек (#91..94), стрида ≈ 4 ticks/кадр. */
    o->anim_phase = (frame >> 2) & 3;
    /* 3 px/frame — летящий снаряд/птица, скорость как у орлов (было 0.75,
     * слишком медленно в ×2-мире; user 2026-07-06). */
    o->x += 3 * o->facing;
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
    bool descending = false;
    if (fixed_left) {
        /* Sky-spawn (type 8): facing задан при спавне (в сторону игрока)
         * и не меняется. Птица рождается на ~100 px ВЫШЕ игрока и сначала
         * «падает в видимый кадр» (ai_type08, state_list 0x171C) БЕЗ
         * горизонтального движения; затем летит по прямой. */
        int target = physics_get_y() - 40;
        if (o->home_y < target) {
            o->home_y += 2;
            descending = true;
        }
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
    if (!descending) o->x += 3 * o->facing;

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
    o->anim_phase = (frame >> 2) & 7;   /* 8-фазная анимация чёртика */
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
    int phase = (frame >> 1) & 7;   /* быстрее качание (было >>2) — 2026-07-06 */
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

/* ═══ Обработчики типов 4..57 (порт по псевдокоду ai_scripts.cpp) ═══════
 *
 * У декомпиляции байт-верны INIT-константы (sprite_id, sub_timer_max,
 * скорости, число сегментов); TICK-логика многих типов в KARNOV.EXE не
 * декомпилирована — там поведение реализовано «по семантике» (chaser /
 * patrol / flyer / projectile) и помечено TODO. */

/* Табличное описание «простых» типов: базовый спрайт (индекс каталога
 * SPRITES.EGA = ENEMY_GUIDE), число кадров, стрида анимации (сдвиг),
 * скорость (px/frame, мир ×2), вид поведения. */
typedef struct simple_ai {
    u16 base;       /* базовый sprite id (0 = нет спрайта → placeholder) */
    u8  nframes;
    u8  shift;      /* anim stride = frame >> shift */
    u8  speed;      /* px/frame по X */
    u8  kind;       /* SK_* */
} simple_ai_t;

enum { SK_NONE = 0, SK_CHASER, SK_PATROL, SK_FLYER };

/* index = primary type (только типы, покрываемые generic-обработчиком).
 * sub_timer_max из INIT (ai_scripts.cpp) переведён в shift стриды. */
static const simple_ai_t SIMPLE_AI[58] = {
    /* type12: наземный патруль, vx=±3 file (INIT 0x13E3C), HP=20.
     * Спрайты в EGA-каталоге не помечены (категория 0) — временно
     * визуал type 11 (зелёный стражник). TODO: настоящий спрайт. */
    [12] = { 76, 10, 2, 2, SK_CHASER },
    [22] = { 181, 4, 3, 2, SK_PATROL },   /* tile-aware (0xB5..B8) */
    [25] = { 189, 4, 3, 0, SK_NONE   },   /* PRNG-lurker — спец-tick ниже */
    [26] = { 193, 8, 1, 3, SK_CHASER },   /* быстрый воин, sub_timer_max=2 */
    [28] = { 207, 4, 2, 2, SK_CHASER },   /* FX-spawn (0xCF..D2) */
    [29] = { 211, 7, 3, 1, SK_CHASER },   /* multi-phase, sub_timer_max=8 */
    [31] = { 219, 3, 2, 2, SK_CHASER },   /* spawn 0xDD */
    [32] = { 222, 8, 2, 2, SK_CHASER },   /* spawn 0xDE (близнец 31) */
    [35] = { 239, 6, 2, 1, SK_PATROL },   /* dual-mode: агрессия по init_arg>10 */
    [37] = { 251, 3, 3, 1, SK_CHASER },   /* slow boss, sub_timer_max=7 */
    [38] = { 254, 5, 3, 2, SK_CHASER },   /* delayed 75 кадров (state_ext=75) */
    [40] = { 263, 4, 1, 2, SK_CHASER },   /* self-init, sub_timer_max=2 */
    [43] = { 275, 9, 2, 2, SK_CHASER },   /* flash enemy (9 кадров) */
    [46] = { 292, 4, 3, 1, SK_CHASER },   /* extended 0x124, sub_timer_max=9 */
    [48] = { 298, 6, 2, 2, SK_CHASER },   /* extended 0x12B */
    [52] = { 309, 9, 2, 1, SK_PATROL },   /* Y-align 10 */
    [53] = { 318, 2, 2, 1, SK_CHASER },   /* простой шаблон */
    [54] = { 320, 2, 2, 2, SK_CHASER },   /* extended 0x140 */
    [55] = { 322, 7, 2, 2, SK_CHASER },   /* хелпер финального босса 0x142 */
};

/* Generic chaser/patrol/flyer. */
static void ai_simple(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    const simple_ai_t *s = &SIMPLE_AI[o->type <= 57 ? o->type : 0];
    if (s->nframes == 0) return;

    /* Type 38 — delayed appearance: invisible первые 75 кадров
     * (INIT: flag_2E=0, state_ext=75). */
    if (o->type == 38) {
        u16 age = (u16)(frame - o->spawn_frame);
        o->invisible = (age < 75);
        if (o->invisible) return;
    }

    o->anim_phase = (u16)((frame >> s->shift) % s->nframes);

    u8 kind = s->kind;
    /* Type 35 — dual-mode: init_arg > 10 → агрессивный chaser
     * (ai_type35: behavior_phase = sub_timer_start > 10). */
    if (o->type == 35 && o->init_arg > 10) kind = SK_CHASER;

    int kx = physics_get_x();
    int dx = kx - o->x;
    int abs_dx = dx < 0 ? -dx : dx;

    switch (kind) {
    case SK_CHASER:
        o->facing = (dx >= 0) ? 1 : -1;
        if (abs_dx > 10) o->x += s->speed * o->facing;
        break;
    case SK_PATROL: {
        /* Патруль home ± range_x; type 22 — tile-aware: разворот на краю
         * платформы (проверка тайла под ногами впереди). */
        if (o->custom == 0) o->custom = (o->facing < 0) ? -1 : 1;
        int nx = o->x + s->speed * o->custom;
        bool turn = false;
        if (o->half_range_x > 0 &&
            (nx < o->home_x - o->half_range_x || nx > o->home_x + o->half_range_x))
            turn = true;
        if (o->type == 22 && !level_solid_at(nx + 4 * o->custom, o->y + 1))
            turn = true;
        if (turn) o->custom = -o->custom;
        else      o->x = nx;
        o->facing = o->custom;
        break;
    }
    case SK_FLYER:
        o->facing = (dx >= 0) ? 1 : -1;
        o->x += s->speed * o->facing;
        if ((frame & 3) == 0) {
            int ky = physics_get_y();
            if (ky < o->y - 2)      o->y -= 1;
            else if (ky > o->y + 2) o->y += 1;
        }
        break;
    default:
        break;
    }
}

/* ─── Sky-spawner (тип 8) ──────────────────────────────────────────────
 * Драйвер cs:0xFFA:0x1615 (file 0x12DB5), работает ВНЕ объектной системы:
 *   — если игрок продвинулся на ≥ 0x32 единиц (= 100 px) с последней
 *     отметки (DS:0xD8A0): убить цепь летунов, сбросить отметку и счётчик;
 *   — иначе счётчик простоя (DS:0xD8A4) растёт; при > 0x186 (390 кадров)
 *     и пустой цепи — спаун (rand&1)+1 летунов.
 * Цепь убивается также при спауне любого scripted-врага subtype 1/7
 * (jump-table в check_triggers @ file 0x5155 → helper file 0x12CFE).
 * Точка рождения летуна — из creator (ai_type08, file 0x12B3F):
 *   x = player.x + (rand & 0x3F) − 10 (file-ед. → мир ×2),
 *   y = player.y − 100 − (rand & 0x3F). */
#define SKY_IDLE_FRAMES     390     /* 0x186 */
#define SKY_PROGRESS_UNITS  50      /* 0x32 (= 100 px) */

static void ai_kill_sky_chain(void)
{
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].type == TYPE_FLYING_BIRD)
            g_obj[i].alive = false;
}

static bool sky_chain_alive(void)
{
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].type == TYPE_FLYING_BIRD) return true;
    return false;
}

static void spawn_sky_bird(u16 frame, int idx)
{
    int kx = physics_get_x(), ky = physics_get_y();
    int bx = kx + 2 * ((int)(rng_next() & 0x3F) - 10);
    int by = ky - 100 - (int)(rng_next() & 0x3F);
    if (by < 16) by = 16;
    int slot = alloc_obj_slot();
    if (slot < 0) return;
    if (getenv("KARNOV_DBG_SPAWN"))
        fprintf(stderr, "sky-spawn: bird slot=%d at (%d,%d) player=(%d,%d)\n",
                slot, bx, by, kx, ky);
    g_obj[slot] = (ai_obj_t){
        .alive       = true,
        .type        = TYPE_FLYING_BIRD,
        .subtype     = 1,
        .x           = bx, .y = by,
        .facing      = (bx > kx) ? -1 : 1,
        .hp          = 10,
        .level_idx   = -1,
        .home_x      = bx, .home_y = by,
        .custom      = idx,               /* противофаза для пары */
        .spawn_x     = (u16)bx, .spawn_y = (u16)by,
        .spawn_frame = frame,
    };
}

static void ai_sky_tick(u16 frame)
{
    if (!s_sky_enabled) return;
    int px = physics_get_x() / 2;
    if (px - s_sky_last_x_units >= SKY_PROGRESS_UNITS) {
        ai_kill_sky_chain();
        s_sky_last_x_units = px;
        s_sky_idle = 0;
        return;
    }
    ++s_sky_idle;
    if (s_sky_idle > SKY_IDLE_FRAMES && !sky_chain_alive()) {
        int n = (int)(rng_next() & 1) + 1;
        for (int i = 0; i < n; ++i) spawn_sky_bird(frame, i);
        s_sky_idle = 0;
    }
}

/* Types 4 и 6 — предмет, лежащий в мире (ai_type04/ai_type06).
 * Type 6: init_arg < 11 → видимая иконка (sprite 39+arg); arg ≥ 11 —
 * СКРЫТЫЙ предмет (misc_state≥11, num_bboxes=0), виден только с Glasses.
 * Type 4 — «трансформирующийся» предмет: тик по (frame & 8) — мерцает.
 * Подбор — касанием (ai_try_pickup). */
static void ai_world_item(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->type == 4) {
        /* frame-gated мерцание раз в 16 кадров (ai_type04:
         * `if ((current_frame & 8) == 0) return`). */
        o->invisible = ((frame >> 3) & 1) != 0;
    }
}

/* Type 20 — КОЛОННА ГОЛОВ (LEVEL2). INIT дизассемблирован байт-в-байт
 * (ai_type20, file 0x1535D..0x15383):
 *
 *     obj.obj_type   = 9
 *     obj.num_bboxes = obj.sub_timer          ; = init_arg (в LEVEL2 = 5/6)
 *     obj.x_world    = (x_world & 0xFFF8) + 2 ; снап к 8-ед. сетке
 *     for (si = 0; si < num_bboxes; ++si) {
 *         box[si].flag     = 1
 *         box[si].x_offset = 0                ; ← ВЕРТИКАЛЬНО, не в ряд
 *         box[si].y_offset = si * 0x14        ; ← шаг 20 строк вниз
 *         box[si].sprite   = si ? 0xAF(175) : 0xAD(173)   ; голова + тела
 *     }
 *     obj.hit_points = 0xFA (250)
 *
 * Т.е. это неподвижный СТОЛБ из голов, растущий ВНИЗ от y_world до пола:
 * LEVEL2 записи (spawn_y=210, init_arg=5) дают 210 + 5*20 = 310 = ровно
 * уровень земли. Прежняя реализация раскладывала сегменты ГОРИЗОНТАЛЬНО
 * (dx = −16) и заставляла их ползти за игроком — отсюда «головы висят
 * в небе» вместо колонн. Реконструкция «змеи» была ошибкой: в
 * декомпиляции TICK (state 2) — пустой `return`, объект статичен. */
static void ai_head_column(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->num_parts == 0) {
        int n = (int)o->init_arg;
        if (n < 1) n = 5;
        if (n > MAX_PARTS) n = MAX_PARTS;
        o->num_parts = n;
        o->x = ((o->x / 2) & ~7) * 2 + 4;             /* file: (x & 0xFFF8) + 2 */
        o->facing = 1;                                /* без flip: dx = 0 */
        for (int i = 0; i < n; ++i) {
            o->parts[i].sprite = (u16)(i == 0 ? 173 : 175);
            o->parts[i].dx = 0;
            o->parts[i].dy = (s16)(i * 20);           /* 0x14 строк вниз */
        }
    }
    o->anim_phase = (frame >> 3) & 1;
}

/* Types 21/33/34/39/50 — многосегментные цепи/черви (фикс. длина).
 * INIT-константы из ai_scripts.cpp; TICK: медленное преследование. */
static void worm_init(ai_obj_t *o, int len, u16 sprite0, u16 sprite_body,
                      int step_dx, int step_dy)
{
    if (len > MAX_PARTS) len = MAX_PARTS;
    o->num_parts = len;
    for (int i = 0; i < len; ++i) {
        o->parts[i].sprite = (i == 0) ? sprite0 : sprite_body;
        o->parts[i].dx = (s16)(step_dx * i);
        o->parts[i].dy = (s16)(step_dy * i);
    }
}

static void ai_worm(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->num_parts == 0) {
        switch (o->type) {
        case 21: worm_init(o, 7, 178, 178, 2, -5); break;  /* bbox0 (2,-3), далее y −30..+5 */
        case 33: worm_init(o, 7, 230, 231, -12, 0); break;
        case 34: worm_init(o, 6, 233, 234, -14, 0); break;
        case 39: worm_init(o, 6, 259, 259, -12, 0); break;
        case 50: worm_init(o, 7, 306, 306, -12, 0); break; /* bbox0 x=-4 file */
        default: worm_init(o, 4, 178, 178, -12, 0); break;
        }
    }
    int kx = physics_get_x();
    o->facing = (kx >= o->x) ? 1 : -1;
    if (o->type != 21 && (frame & 3) == 0) o->x += o->facing;
    /* Лёгкая волна по сегментам. */
    for (int i = 1; i < o->num_parts; ++i) {
        int ph = ((frame >> 3) + i) & 3;
        o->parts[i].dy = (s16)(o->parts[i].dy / 2 * 2 + (ph < 2 ? 0 : (o->type == 21 ? 2 : -2)));
    }
    o->anim_phase = (frame >> 3) & 1;
}

/* Type 23 — самонаводящийся снаряд (ai_type23, file 0x14C87): при init
 * vx = ±3 file (=±6 мир) в сторону игрока, sprite 0xB9 (185), прямой
 * полёт до столкновения с тайлом. */
static void ai_homing_shot(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->custom == 0) {
        int kx = physics_get_x();
        o->facing = (kx >= o->x) ? 1 : -1;
        o->custom = 1;
    }
    o->anim_phase = (frame >> 1) & 1;
    o->x += 6 * o->facing;
    if (level_solid_at(o->x, o->y - 4)) { o->alive = false; return; }
    int world_w = (int)level_num_map_cols * TILE_W;
    if (o->x < -16 || o->x > world_w + 16) o->alive = false;
}

/* Type 24 — спаунер стаи (ai_type24, file 0x15029): INIT рождает РОВНО 7
 * детей (spawn_child cs:0x1348:0xF57) в формации; спрайты 0xBB/0xBC. */
static void ai_flock_spawner(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->custom == 0) {
        o->custom = 1;
        o->invisible = true;
        for (int i = 0; i < 7; ++i) {
            int slot = alloc_obj_slot();
            if (slot < 0) break;
            g_obj[slot] = (ai_obj_t){
                .alive       = true,
                .type        = TYPE_FLYING_BIRD,
                .subtype     = 1,
                .x           = o->x + i * 14,
                .y           = o->y - (i % 3) * 8,
                .facing      = -1,
                .hp          = 10,
                .level_idx   = -1,
                .home_x      = o->x + i * 14,
                .home_y      = o->y - (i % 3) * 8,
                .custom      = i & 1,
                .spawn_frame = frame,
            };
        }
    }
    /* Спаунер отработал — освобождаем слот (одноразовый). */
    o->alive = false;
}

/* Type 25 — PRNG-lurker (ai_type25): случайная пауза 30..45 тиков
 * (rand & 0x0F + 30), затем бросок к игроку. */
static void ai_prng_lurker(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 3) & 3;
    if (o->custom == 0) o->custom = (int)((rng_next() & 0x0F) + 30);
    if (o->custom > 1) { --o->custom; return; }
    /* Бросок: шаг 8 px к игроку, потом новая пауза. */
    int kx = physics_get_x();
    o->facing = (kx >= o->x) ? 1 : -1;
    o->x += 8 * o->facing;
    o->custom = (int)((rng_next() & 0x0F) + 30);
}

/* Type 27 — top-drop (ai_type27): state 0 ставит y = player.y − 70,
 * затем падает вниз до пола и ходит. Спрайты 0xC9..0xCE (201..206). */
static void ai_top_drop(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 2) % 6;
    if (o->custom == 0) {                 /* падение */
        o->y += 3;
        if (level_solid_at(o->x, o->y + 1) || o->y >= physics_get_ground_y()) {
            o->custom = 1;
        }
        return;
    }
    int kx = physics_get_x();
    int dx = kx - o->x;
    o->facing = (dx >= 0) ? 1 : -1;
    if (dx > 10 || dx < -10) o->x += 2 * o->facing;
}

/* Type 30 — растущая цепь (ai_type30): num_bboxes начинается с 0 и растёт
 * во времени; спрайты чередуются 0x05 / 0x98 (152). */
static void ai_grow_chain(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    int target = 6;
    if (o->num_parts < target && (frame & 7) == 0) {
        int i = o->num_parts;
        o->parts[i].sprite = (i & 1) ? 152 : 5;
        o->parts[i].dx = (s16)(-i * 12);
        o->parts[i].dy = 0;
        ++o->num_parts;
    }
    o->anim_phase = (frame >> 3) & 1;
}

/* Type 36 — ★ Рю, финальный босс (ai_type36, file 0x16FBB): 2 части —
 * голова 0xF5 (245) + хвост 0xF9 (249), x выровнен по 8 px file-сетки.
 * TICK не декомпилирован: стоит, периодически пускает type-23 шары в
 * игрока; state 5 (hurt) — мигание. */
static void ai_ryu(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->num_parts == 0) {
        o->x = ((o->x / 2) & ~7) * 2;
        o->num_parts = 2;
        o->parts[0].sprite = 245; o->parts[0].dx = 4;  o->parts[0].dy = 0;
        o->parts[1].sprite = 249; o->parts[1].dx = -20; o->parts[1].dy = 0;
    }
    int kx = physics_get_x();
    o->facing = (kx >= o->x) ? 1 : -1;
    /* Анимация головы: 245..248 (idle → дыхание огнём). */
    u16 ph = (frame >> 4) & 3;
    o->parts[0].sprite = (u16)(245 + ph);
    /* Огненный шар в фазе «дыхания». */
    if (ph == 2 && (frame & 15) == 0) {
        int slot = alloc_obj_slot();
        if (slot >= 0) {
            g_obj[slot] = (ai_obj_t){
                .alive = true, .type = 23, .subtype = 1,
                .x = o->x, .y = o->y - 16,
                .facing = o->facing, .hp = 1,
                .level_idx = -1, .spawn_frame = frame,
            };
        }
    }
}

/* Types 44/45 — PRNG-снаряды (ai_type44/45): случайная vy
 *   type 44: vy = −((rand & 0x1FFF) + 0x2000)/65536  (быстрее вверх), vx=0x3000
 *   type 45: vy = −((rand & 0x1FFF) + 0x8000)/65536? — медленный вариант,
 *            vx=0x2000, sub_timer_max=7.
 * Q16.16 как у rock-projectile. Направление по игроку. */
static void ai_prng_spark(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->custom == 0) {
        o->custom = 1;
        int kx = physics_get_x();
        o->facing = (kx >= o->x) ? 1 : -1;
        int r = (int)(rng_next() & 0x1FFF);
        o->pos_x_q16 = o->x << 16;
        o->pos_y_q16 = o->y << 16;
        if (o->type == 44) {
            o->vy_q16 = -(r + 0x2000) * 2;         /* ~-0.25..-0.5 px/f ×2 */
            o->vx_q16 = o->facing * 0x3000 * 2;
        } else {
            o->vy_q16 = -(r + 0x8000);             /* медленный подъём */
            o->vx_q16 = o->facing * 0x2000 * 2;
        }
        o->grav_q16 = 0x1000;
    }
    o->vy_q16 += o->grav_q16;
    o->pos_x_q16 += o->vx_q16;
    o->pos_y_q16 += o->vy_q16;
    o->x = o->pos_x_q16 >> 16;
    o->y = o->pos_y_q16 >> 16;
    o->anim_phase = (frame >> 2) & 3;
    int world_w = (int)level_num_map_cols * TILE_W;
    if (o->y > physics_get_ground_y() + 32 || o->x < -16 || o->x > world_w + 16)
        o->alive = false;
}

/* Type 42 — частица/искра (ai_type42, UNIQUE 3-state): короткоживущий
 * объект; behavior_phase = init_arg & 1 (если 1 — y −= 20); живёт
 * ~init_arg*10 тиков. */
static void ai_particle42(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->custom == 0) {
        o->custom = (int)(o->init_arg * 10);
        if (o->custom < 30) o->custom = 30;
        if (o->init_arg & 1) o->y -= 20;
    }
    o->anim_phase = (frame >> 2) & 7;
    if (--o->custom <= 1) o->alive = false;
}

/* Type 47 — Y-snap + PRNG (ai_type47): очень медленный (sub_timer_max=10),
 * случайные вертикальные подскоки. */
static void ai_hopper47(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 3) & 1;
    if ((frame % 10) != 0) return;
    if ((rng_next() & 3) == 0) o->y -= 4;
    else if (o->y < o->home_y) o->y += 2;
    int kx = physics_get_x();
    o->facing = (kx >= o->x) ? 1 : -1;
    o->x += o->facing;
}

/* Type 49 — хелпер Рю (ai_type49): агрессивен ТОЛЬКО на LEVEL9 при
 * x_world > 0x44C (file) = 2200 (мир); иначе пассивен. */
static void ai_ryu_helper(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 3) & 1;
    extern u16 g_ai_level_num;         /* ниже, ai_init_from_level */
    bool arena = (g_ai_level_num == 9) && (o->x > 0x44C * 2);
    if (!arena) return;
    int kx = physics_get_x(), ky = physics_get_y();
    o->facing = (kx >= o->x) ? 1 : -1;
    if ((frame & 1) == 0) {
        o->x += 2 * o->facing;
        if (ky < o->y - 2)      o->y -= 1;
        else if (ky > o->y + 2) o->y += 1;
    }
}

/* Type 51 — статический элемент арены (ai_type51): 8 частей, sprite 0x135
 * (309), y выровнен по 10, sub_timer_max=0xFF (вечный). */
static void ai_arena_static(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)frame; (void)karnov_input;
    if (o->num_parts == 0) {
        o->y -= o->y % 10;
        o->num_parts = MAX_PARTS;
        for (int i = 0; i < MAX_PARTS; ++i) {
            o->parts[i].sprite = 309;
            o->parts[i].dx = (s16)((i % 4) * 16);
            o->parts[i].dy = (s16)((i / 4) * -12);
        }
    }
}

typedef void (*ai_handler_fn)(ai_obj_t *, u16, u8);

/* Таблица AI-обработчиков по PRIMARY type — аналог ai_handler_table[58]
 * @ DS:0x10FE в KARNOV.EXE (spawn_active_object: bx = spawn[0]*4;
 * handler = DS:[bx+0x10FE]). Типы 0-3 — Karnov/маркеры (physics.c). */
static const ai_handler_fn AI_HANDLERS[58] = {
    [0]  = ai_stub,
    [1]  = ai_stub,            /* Karnov idle — physics.c */
    [2]  = ai_stub,            /* Karnov spawn-marker — physics.c */
    [3]  = ai_stub,            /* Karnov swim/fly — physics.c */
    [4]  = ai_world_item,      /* трансформирующийся предмет (мерцает) */
    [5]  = ai_falling_bonus,   /* падающий бонус */
    [6]  = ai_world_item,      /* предмет в мире (init_arg≥11 → скрыт) */
    [7]  = ai_stub,            /* невидимый триггер */
    [8]  = ai_stub,            /* sky-spawner — драйвер ai_sky_tick вне объектов;
                                * запись type 8 (range=0) не спавнится вовсе */
    [9]  = ai_walking_minion,  /* 2-bbox boss-like (L1: декор-дракон) */
    [10] = ai_pendulum,
    [11] = ai_ground_walker,
    [12] = ai_simple,          /* наземный патруль (HP=20) */
    [13] = ai_stone_thrower,   /* Y-snap метатель */
    [14] = ai_flying_bird,     /* летун / Селёдка-босс */
    [15] = ai_stone_golem,
    [16] = ai_horizontal_projectile,
    [17] = ai_ground_walker,   /* скелет на страусе */
    [18] = ai_walking_minion,
    [19] = ai_slow_helper,
    [20] = ai_head_column,     /* колонна голов LEVEL2 (multi-bbox, статична) */
    [21] = ai_worm,            /* 7-сегментная цепь */
    [22] = ai_simple,          /* tile-aware патруль */
    [23] = ai_homing_shot,     /* самонаводящийся шар */
    [24] = ai_flock_spawner,   /* стая из 7 */
    [25] = ai_prng_lurker,
    [26] = ai_simple,          /* быстрый воин */
    [27] = ai_top_drop,
    [28] = ai_simple,
    [29] = ai_simple,          /* multi-phase хелпер */
    [30] = ai_grow_chain,
    [31] = ai_simple,
    [32] = ai_simple,
    [33] = ai_worm,
    [34] = ai_worm,            /* червь с HP-индикатором */
    [35] = ai_simple,          /* dual-mode */
    [36] = ai_ryu,             /* ★ Рю */
    [37] = ai_simple,          /* slow boss */
    [38] = ai_simple,          /* delayed 75 кадров */
    [39] = ai_worm,
    [40] = ai_simple,
    [41] = ai_stub,            /* mid-boss HP=100 — в данных уровней не встречается */
    [42] = ai_particle42,
    [43] = ai_simple,
    [44] = ai_prng_spark,
    [45] = ai_prng_spark,
    [46] = ai_simple,
    [47] = ai_hopper47,
    [48] = ai_simple,
    [49] = ai_ryu_helper,
    [50] = ai_worm,
    [51] = ai_arena_static,
    [52] = ai_simple,
    [53] = ai_simple,
    [54] = ai_simple,
    [55] = ai_simple,
    [56] = ai_stub,            /* level-end триггер — секвенция в game.c */
    [57] = ai_stub,            /* статическая декорация */
};

static ai_handler_fn pick_handler(u16 type)
{
    if (type <= 57) {
        ai_handler_fn fn = AI_HANDLERS[type];
        return fn ? fn : ai_stub;
    }
    switch (type) {
        case 512: return ai_stone_man;       /* каменный человек (декор) */
        case 513: return ai_stub;            /* Karnov in-game state — physics.c */
        case TYPE_FLYING_BIRD:     return ai_flying_bird;
        case TYPE_STONE_THROWER:   return ai_stone_thrower;
        case TYPE_ROCK_PROJECTILE: return ai_rock_projectile;
        case TYPE_PLAYER_FIREBALL: return ai_player_fireball;
        case TYPE_BOMB_BLAST:      return ai_bomb_blast;
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
    g_boss_dead = false;
    g_boss_dead_frame = 0;
    g_scroll_picked = false;

    g_rng_seed = 0x4B41u + level_num;   /* детерминизм демо: сид от уровня */
    g_ai_level_num = level_num;

    /* Sky-spawner активен только на уровнях, где есть type-8 запись
     * (её range=(0,0) — она никогда не спавнится сама, это маркер). */
    s_sky_enabled = false;
    for (u16 i = 0; i < level_num_enemies; ++i)
        if (level_enemies[i].type == 8) { s_sky_enabled = true; break; }
    s_sky_last_x_units = 0;
    s_sky_idle = 0;

    if (getenv("KARNOV_DBG_L1_ENEMIES")) {
        for (u16 i = 0; i < level_num_enemies; ++i) {
            const enemy_spawn_t *e = &level_enemies[i];
            fprintf(stderr, "L%u en[%u]: type=%u sub=%u cur=(%u,%u) rng=(%u,%u) spawn=(%u,%u) trg=%u (hp=%u init=%u) flg=%04x\n",
                    level_num, i, e->type, e->subtype, e->cur_x, e->cur_y,
                    e->range_x, e->range_y, e->spawn_x, e->spawn_y,
                    e->trigger, (e->trigger >> 8), (e->trigger & 0xFF), e->flags);
        }
    }

    /* Спаунов «upfront» нет ни на одном уровне (включая LEVEL0 splash):
     * оригинал зовёт check_triggers каждый кадр, и записи с широкими
     * range срабатывают сразу же в первый кадр по близости игрока.
     * Скрытый предмет LEVEL0 rec[3] (range 30×30) появляется только
     * когда Karnov подходит — это и есть поведение оригинала. */
}

/* ─── Спаун одной записи (spawn_active_object, file 0x4F3D) ────────────
 * obj_type = subtype; hp = trigger>>8; x/y = spawn_x/spawn_y (unscaled в
 * единицах движка: X ×2 в пикселях порта); init_arg = trigger&0xFF;
 * flags=1 ставит ВЫЗЫВАЮЩИЙ — только при успехе. Возврат: slot или -1. */
static int ai_spawn_from_record(u16 rec_idx, u16 frame)
{
    const enemy_spawn_t *e = &level_enemies[rec_idx];
    int slot = alloc_obj_slot();
    if (slot < 0) return -1;

    int hp  = (int)(e->trigger >> 8);
    u16 arg = (u16)(e->trigger & 0xFF);
    if (hp == 0) hp = 1;
    /* Type 12: оригинальный INIT ставит hit_points=20 (file 0x13E3C). */
    if (e->type == 12 && hp <= 1) hp = 20;

    int sx = (int)e->spawn_x * 2;   /* единицы движка (2px) → пиксели */
    int sy = (int)e->spawn_y;

    /* Предметы: item_id = (init_arg == 99) ? RND%11 : init_arg;
     * sprite_id = item_id + 0x27 (= #39..49) — байт-порт ai_type05/06.
     * arg ≥ 11 → СКРЫТЫЙ предмет (misc_state≥11), виден только с Glasses. */
    u16 item_sprite = 0;
    bool hidden_item = false;
    if (e->type == 4 || e->type == 5 || e->type == 6) {
        u16 item_id;
        if (arg == 99)      item_id = (u16)(rng_next() % 11u);
        else if (arg >= 11) { item_id = (u16)(arg % 11u); hidden_item = true; }
        else                item_id = arg;
        item_sprite = (u16)(39 + item_id);
    }

    u16 type = e->type;
    if (g_ai_level_num == 0 && type == 13) {
        /* Заставка в оригинале собирается кодом splash.cpp, а не записями
         * LEVEL0.BIN: метатель там стоит ногами на y=113 (DOSBox-verified),
         * тогда как запись даёт y=110. Держим верифицированную позицию,
         * переведя её в новую (верхнюю) привязку: top = feet − высота. */
        const sprite_t *base = sprite_find(86);
        sy = 113 - (base ? (int)base->h : 27);
        type = TYPE_STONE_THROWER;  /* на splash — статичная декорация */
    }

    /* Goal-объект (его смерть = конец уровня) — из данных (file 0x517F):
     * level 9 → запись type 56; иначе — последняя запись sorted-таблицы. */
    bool goal = (g_ai_level_num == 9)
                    ? (e->type == 56)
                    : (rec_idx == (u16)(level_num_enemies - 1));

    g_obj[slot] = (ai_obj_t){
        .alive        = true,
        .invisible    = hidden_item,
        .is_boss      = goal,
        .type         = type,
        .subtype      = e->subtype,           /* → obj_type (+0xE5) */
        .init_arg     = arg,
        .x            = sx,
        .y            = sy,
        .facing       = -1,
        .hp           = hp,
        .level_idx    = (int)rec_idx,
        .home_x       = sx,
        .home_y       = sy,
        /* range — окно СПАУНА, в объект оригинал его не копирует. Порт
         * пока использует его как амплитуду патруля/маятника (port-
         * invention до декомпиляции TICK-хэндлеров). */
        .half_range_x = (int)e->range_x,
        .half_range_y = (int)e->range_y / 2,
        .spawn_x      = (u16)sx,
        .spawn_y      = (u16)sy,
        .item_sprite  = item_sprite,
        .spawn_frame  = frame,
    };
    return slot;
}

/* ─── CHECKTRIGGERS (cs:0x365:0x1C1, file 0x5011) — дословный порт ─────
 * Каждый кадр: два бинпоиска по sorted-таблице (окно [camera_left,
 * player_x + 160 единиц]), затем линейный скан с one-shot flags и
 * строгими range-тестами по близости ИГРОКА (не камеры!). */
/* KARNOV_SPAWN_LOG=path — машинно-читаемый лог для сверки с оракулом
 * (tools/spawn_oracle.py): «P frame px py left» на каждый вызов и
 * «S frame rec type sub x y hp goal» на каждый успешный спаун. */
static FILE *spawn_log(void)
{
    static FILE *fp = NULL;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char *p = getenv("KARNOV_SPAWN_LOG");
        if (p) fp = fopen(p, "w");
    }
    return fp;
}

void ai_check_triggers(int player_x_px, int player_y_px, int camera_left_px,
                       u16 frame)
{
    bool dbg = getenv("KARNOV_DBG_SPAWN") != NULL;
    int n = (int)level_num_enemies;
    if (n == 0) return;
    if (spawn_log())
        fprintf(spawn_log(), "P %u %d %d %d\n",
                frame, player_x_px, player_y_px, camera_left_px);

    int px   = player_x_px / 2;     /* X движка — в 2px-единицах */
    int py   = player_y_px;
    int left = camera_left_px / 2;

    /* Бинпоиск 1 (file 0x5041): lo=0, hi=n-1, порог = camera_left. */
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if ((int)level_enemies[mid].cur_x < left) lo = mid; else hi = mid;
    }
    int start = lo;
    /* Бинпоиск 2 (file 0x507D): порог = player_x + 0xA0 (160 ед. = 320 px). */
    lo = 0; hi = n - 1;
    int ahead = px + 0xA0;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if ((int)level_enemies[mid].cur_x < ahead) lo = mid; else hi = mid;
    }
    int end = hi;   /* скан ВКЛЮЧИТЕЛЬНО (loop tail file 0x5148) */

    for (int si = start; si <= end; ++si) {
        enemy_spawn_t *r = &level_enemies[si];
        if (r->flags != 0) continue;               /* one-shot защёлка */
        int dx = px - (int)r->cur_x; if (dx < 0) dx = -dx;
        if (dx >= (int)r->range_x) continue;       /* строгое «<»; range 0 ⇒ никогда */
        int dy = py - (int)r->cur_y; if (dy < 0) dy = -dy;
        if (dy >= (int)r->range_y) continue;
        /* Спаун scripted-врага subtype 1/7 убивает sky-цепь (file 0x5155). */
        if (r->subtype == 1 || r->subtype == 7) ai_kill_sky_chain();
        int slot = ai_spawn_from_record((u16)si, frame);
        if (slot < 0) continue;   /* alloc fail: flags остаётся 0 — ретрай */
        r->flags = 1;             /* file 0x4FFE: только при успехе */
        if (spawn_log())
            fprintf(spawn_log(), "S %u %d %u %u %d %d %d %d\n",
                    frame, si, r->type, r->subtype,
                    g_obj[slot].x, g_obj[slot].y, g_obj[slot].hp,
                    g_obj[slot].is_boss ? 1 : 0);
        if (dbg)
            fprintf(stderr, "spawn[%d]: f=%u type=%u sub=%u at=(%d,%d) hp=%d%s\n",
                    si, frame, r->type, r->subtype,
                    g_obj[slot].x, g_obj[slot].y, g_obj[slot].hp,
                    g_obj[slot].is_boss ? " GOAL" : "");
    }
}

/* ─── Смерть игрока: rearm-цикл (file 0x1015A) ─────────────────────────
 * Все активные объекты снимаются, затем каждая запись перевзводится по
 * правилу: flags = (cur_x + range_x < player_x) ? 1 : 0 — всё, что не
 * полностью позади точки возрождения, оживает вновь (включая убитых и
 * босса/goal). player_x_px — позиция ПОСЛЕ респауна. */
void ai_respawn_rearm(int player_x_px)
{
    int px = player_x_px / 2;
    for (int i = 0; i < g_num; ++i) g_obj[i].alive = false;
    g_num = 0;
    for (u16 i = 0; i < level_num_enemies; ++i) {
        enemy_spawn_t *r = &level_enemies[i];
        r->flags = ((int)r->cur_x + (int)r->range_x < px) ? 1 : 0;
    }
    /* Goal/босс мог быть убит до смерти игрока — он тоже возвращается
     * (оригинальное правило rearm не делает исключений). */
    g_boss_dead = false;
    g_boss_dead_frame = 0;
}

void ai_tick(u16 frame, u8 karnov_input)
{
    g_ai_frame = frame;
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

    /* Sky-spawner — драйвер вне объектной системы (file 0x12DB5). */
    ai_sky_tick(frame);

    /* После убийства boss'а через ~30 ticks спавним свиток (scroll item).
     * custom=999 — sentinel маркер подбора (level-end). item_sprite=0x90 —
     * настоящий спрайт выпадающего свитка (user-verified 2026-07-10, скриншот:
     * белый объект на земле). Karnov автоматически подходит к нему (неуязвим),
     * берёт и садится со свитком. Прежняя заглушка #0x32 (Treasure) была
     * неверной — #329-333 из ai_type56 вообще не рисуются как обычные спрайты
     * (это extended-data, не блиттятся), а реальный свиток = обычный #0x90. */
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
                g_obj[slot] = (ai_obj_t){
                    .alive       = true,
                    .type        = 5,             /* падающий бонус */
                    .x           = g_boss_x,      /* фикс. X boss'а (не над Karnov-ом) */
                    .y           = 50,            /* высоко над землёй */
                    .vy          = 1,
                    .facing      = 1,
                    .custom      = 999,           /* sentinel: boss-reward scroll */
                    .item_sprite = 0x90,          /* настоящий спрайт свитка (#0x90) */
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
        case   9: {                                /* Стартовый ДРАКОН (декорация).
                                                    * Единственный type9 в LEVEL1 — idx3 @ world 22,
                                                    * т.е. дракон у начала уровня. Его спрайты
                                                    * 0x91=145 + 0x92=146 уже рисует хардкод
                                                    * draw_level1_dragon() (game.c). Поэтому сам
                                                    * type9 НЕ рендерится (иначе — второй, «половинный»
                                                    * дракон). Скрываем через `continue` в ai_render. */
            return 0xFFFF;
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
        case TYPE_FLYING_BIRD: {                    /* Летящая зелёная птица — 2 фазы (#147-148).
                                                    * type8 sky-spawn AnimSeg 0x93/0x94.
                                                    * Манифест: ai=522 (Karnov-категория). */
            return 147 + (anim_phase & 1);
        }
        case  16: {                                /* КОМАРИК — горизонтальный летун (type16,
                                                    * vx≈-1.5 px/frame). Дизассемблирование
                                                    * ai_type16 (cs:0x11D6:0x1033): INIT ставит
                                                    * state_list = DS:0x1984, который указывает в
                                                    * блок анимации МОШКИ #91-94 (0x5B-0x5E), две
                                                    * 2-кадровые группы [91,92]/[93,94]. Птица
                                                    * #147-148 — это ДРУГОЙ список (DS:0x17E4).
                                                    * Раньше ошибочно рисовался птицей, как в начале
                                                    * (user 2026-07-08). 4-фазный взмах крылышек. */
            return 91 + (anim_phase & 3);
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
        case TYPE_BOMB_BLAST: {                     /* растущая вспышка #25..#28 */
            u16 p = anim_phase > 3 ? 3 : anim_phase;
            return 25 + p;
        }
        case TYPE_ROCK_PROJECTILE: {
            /* Rolling rock — байт-точный цикл AnimSeg DS:0x18EC: #21 → #22 → #23
             * (3 фазы; см. ai_rock_projectile). */
            static const u16 rock[3] = { 21, 22, 23 };
            return rock[anim_phase % 3];
        }
        default: {
            /* Data-driven: манифест ai_type из директории SPRITES.EGA/CGA
             * (KS v4). Для каждого типа спрайты идут непрерывным диапазоном
             * base..base+count-1 — это и есть анимация. Покрывает типы
             * 14,15,17..50,52..56; ручные case'ы выше — только для типов
             * без манифест-записи (10/11/13/16 делят чужие спрайты) и
             * псевдотипов порта. */
            u16 mb, mc;
            if (type >= 4 && type <= 57 && sprite_manifest(type, &mb, &mc))
                return (u16)(mb + (anim_phase % mc));
            /* Fallback (кэш v3 без манифеста): таблица SIMPLE_AI. */
            if (type <= 57 && SIMPLE_AI[type].nframes > 0) {
                const simple_ai_t *s = &SIMPLE_AI[type];
                return (u16)(s->base + (anim_phase % s->nframes));
            }
            return 0xFFFF;                   /* нет спрайта — placeholder rect */
        }
    }
}

/* Блит одного спрайта в мировых координатах.
 *
 * Anchor по Y — ВЕРХ спрайта (декомпиляция update_bboxes, KARNOV.EXE
 * file 0x0247A): движок кладёт box.y0 = obj.y_world + box.dy и передаёт
 * это блиттеру как верхнюю строку; арт в спрайте выровнен по верху
 * канвы. Порт раньше якорил НИЗ (y - h), из-за чего все враги висели на
 * высоту своего спрайта (~2 тайла) над землёй. Доказательства:
 * helper «поставить на землю» (file 0x0EE30) считает
 * y_world = ground_y − sprite_h, а INIT type 36 (file 0x17066) кладёт
 * dy хвоста = высоте спрайта головы (второй спрайт ровно под первым).
 *
 * Anchor по X пока остаётся body_cx: в нашем кэше спрайты записаны
 * ЗЕРКАЛЬНО (экстрактор делает col = w-1-...), тогда как оригинал
 * хранит арт прижатым влево и кладёт левый край в x_world. Исправление
 * X требует переизвлечения кэша и пересборки L/R-слотов Karnov'а —
 * отдельная задача (см. KARNOV.md §9.2). */
static void blit_sprite_world(u16 sid, int wx, int wy, int facing,
                              int scroll_x, int scroll_y)
{
    const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
    if (!s) return;
    bool flip = facing < 0;
    int anchor = flip ? (s->w - 1 - s->body_cx) : s->body_cx;
    int x0 = wx - anchor - scroll_x;
    int y0 = wy - scroll_y;
    u8 ck = sprite_is_cga() ? 0xFF : 1;
    const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
    for (int r = 0; r < s->h; ++r) {
        int yy = y0 + r;
        if (yy < 0 || yy >= WIN_H) continue;
        for (int c = 0; c < s->w; ++c) {
            int sc = flip ? (s->w - 1 - c) : c;
            u8 v = s->pixels[r * s->w + sc];
            if (v == 0xFF || v == ck) continue;
            int xx = x0 + c;
            if (xx < 0 || xx >= WIN_W) continue;
            render_framebuffer()[yy * WIN_W + xx] =
                sprite_is_cga() ? pal[v & 3] : pal[v & 15];
        }
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
        if (g_obj[i].type == 7) continue;  /* невидимый триггер-«призрак» (num_bboxes=0) */
        if (g_obj[i].type == 9) continue;  /* стартовый дракон — рисуется draw_level1_dragon() */
        if (g_obj[i].type == TYPE_BOMB_BLAST) continue;  /* вспышка — поверх Karnov'а (ai_render_overlay) */
        /* Пасс 0 — только фоновые летуны; пасс 1 — все остальные поверх. */
        if (ai_is_background_flyer(g_obj[i].type) != (pass == 0)) continue;
        /* Karnov-выстрел — маленький белый шарик, рисуем процедурно (~4px):
         * подходящего мелкого белого спрайта в кэше нет (мелкие спрайты
         * серые, белые — крупнее). Круглые синие шарики #0x0A-0x0D — это
         * снаряд босса «селёдка», не выстрел Karnov'а. */
        if (g_obj[i].type == TYPE_PLAYER_FIREBALL) {
            int bx = g_obj[i].x - scroll_x;
            int by = g_obj[i].y - 2 - scroll_y;   /* центр (placeholder: y - ph/2) */
            const u32 W = 0xFFFFFFFFu;
            /* Крошечный белый шарик 3x3 (ромбик). */
            render_fill_rect(bx,     by - 1, 1, 1, W);  /* верх */
            render_fill_rect(bx - 1, by,     3, 1, W);  /* середина 3px */
            render_fill_rect(bx,     by + 1, 1, 1, W);  /* низ */
            continue;
        }
        /* Multi-part враги (змея, цепи, черви, Рю, арена): рисуем каждую
         * часть отдельным спрайтом со смещением от (x, y). */
        if (g_obj[i].num_parts > 0) {
            for (int p = g_obj[i].num_parts - 1; p >= 0; --p) {
                blit_sprite_world(g_obj[i].parts[p].sprite,
                                  g_obj[i].x + g_obj[i].parts[p].dx * (g_obj[i].facing < 0 ? -1 : 1),
                                  g_obj[i].y + g_obj[i].parts[p].dy,
                                  g_obj[i].facing, scroll_x, scroll_y);
            }
            continue;
        }
        u16 sid;
        if ((g_obj[i].type == 6 || g_obj[i].type == 4) && g_obj[i].item_sprite > 0) {
            /* Предмет в мире (LEVEL0 showcase / скрытый / трансформирующийся):
             * иконка OPTIONS #39..49 из item_sprite. */
            sid = g_obj[i].item_sprite;
        } else if (g_obj[i].type == 6 && g_obj[i].custom > 0) {
            /* Legacy splash showcase: custom = sprite_id для item icon. */
            sid = (u16)g_obj[i].custom;
        } else if (g_obj[i].type == 5 && g_obj[i].item_sprite > 0) {
            /* Падающий предмет-бонус — иконка OPTIONS (#39..49), НЕ квадратик. */
            sid = g_obj[i].item_sprite;
        } else {
            /* Тип→спрайт решается ТОЛЬКО по полю type (из LEVEL%d.BIN), без
             * оглядки на координаты/высоту. Единственный источник — sprite_for_type.
             * (type6/type5 выше — это не координатные ветки, а сохранённый при
             * спавне sprite_id/item_id из записи уровня.) */
            sid = sprite_for_type(g_obj[i].type, g_obj[i].anim_phase);
        }
        const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
        if (s) {
            bool flip = g_obj[i].facing < 0;
            /* Anchor: X — body_cx (см. blit_sprite_world), Y — ВЕРХ
             * спрайта (obj.y_world из записи уровня = верхняя строка). */
            int anchor = flip ? (s->w - 1 - s->body_cx) : s->body_cx;
            int x0 = g_obj[i].x - anchor - scroll_x;
            int y0 = g_obj[i].y - scroll_y;
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
                         g_obj[i].y - scroll_y, pw, ph, col);
    }
  }
  (void)mode_cga;
}

/* Оверлей поверх Karnov'а: анимированная вспышка Bomb-предмета (#25→#28).
 * Вызывается из game.c ПОСЛЕ physics_render, чтобы взрыв был на переднем плане. */
void ai_render_overlay(bool mode_cga, int scroll_x, int scroll_y)
{
    (void)mode_cga;
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive || g_obj[i].type != TYPE_BOMB_BLAST) continue;
        u16 sid = sprite_for_type(TYPE_BOMB_BLAST, g_obj[i].anim_phase);
        const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
        if (!s) continue;
        int x0 = g_obj[i].x - s->body_cx - scroll_x;
        int y0 = g_obj[i].y - s->h - scroll_y;
        u8 ck = sprite_is_cga() ? 0xFF : 1;
        const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
        for (int r = 0; r < s->h; ++r) {
            int yy = y0 + r;
            if (yy < 0 || yy >= WIN_H) continue;
            for (int c = 0; c < s->w; ++c) {
                u8 v = s->pixels[r * s->w + c];
                if (v == 0xFF || v == ck) continue;
                int xx = x0 + c;
                if (xx < 0 || xx >= WIN_W) continue;
                render_framebuffer()[yy * WIN_W + xx] =
                    sprite_is_cga() ? pal[v & 3] : pal[v & 15];
            }
        }
    }
}

/* ─── Pickup detection ─────────────────────────────────────────────────
 * Karnov bbox ≈ 16×24 от (kx, ky) = центр-ног. Items (type 6, custom>0) —
 * это showcase-иконки 16×12 с anchor (cx=body_cx=8, y=bottom). Простое
 * AABB-пересечение. */
/* Положить в инвентарь предмет по его sprite_id (#39..48 = OPTIONS-иконки).
 * В оригинале pickup вызывает cs:0x41d:0x9be(item_id) — «добавить в очередь
 * OPTIONS». Полная 10-предметная модель: item_id = sprite - 39. */
static void give_item_by_sprite(u16 sprite)
{
    inv_give_by_sprite(sprite);
}

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
            int iy0 = o->y, iy1 = o->y + 12;
            if (kx1 < ix0 || kx0 > ix1) continue;
            if (ky1 < iy0 || ky0 > iy1) continue;
            o->alive = false;
            g_scroll_picked = true;
            continue;
        }
        /* Падающий предмет-бонус (type 5, item_sprite #39..49). Подбирается
         * КАСАНИЕМ Karnov'а (в оригинале коллизия ставит item.state_byte=4,
         * и его handler добавляет предмет в инвентарь). Стрелять по нему
         * нельзя (см. is_damageable_enemy). */
        if (o->type == 5 && o->item_sprite > 0) {
            int ix0 = o->x - 8, ix1 = o->x + 8;
            int iy0 = o->y, iy1 = o->y + 12;
            if (kx1 < ix0 || kx0 > ix1) continue;
            if (ky1 < iy0 || ky0 > iy1) continue;
            give_item_by_sprite(o->item_sprite);
            audio_play_sfx(5);   /* подбор предмета — play_sound(5) */
            o->alive = false;    /* запись остаётся flags=1 — предмет одноразов */
            continue;
        }
        if (o->type != 6 && o->type != 4) continue;
        if (o->invisible) continue;            /* скрытые — только с Glasses */
        u16 sid = (o->item_sprite > 0) ? o->item_sprite
                : (o->custom > 0)      ? (u16)o->custom : 0;
        if (sid == 0) continue;
        /* Item bbox: y = bottom, body_cx=8 → (x-8..x+8, y-12..y). */
        int ix0 = o->x - 8, ix1 = o->x + 8;
        int iy0 = o->y, iy1 = o->y + 12;
        if (kx1 < ix0 || kx0 > ix1) continue;
        if (ky1 < iy0 || ky0 > iy1) continue;
        give_item_by_sprite(sid);
        audio_play_sfx(5);
        o->alive = false;   /* убираем из мира; запись остаётся flags=1 */
    }
}

/* Найти свободный слот в g_obj[] (alive=false) или вернуть g_num (если все
 * заняты и есть запас в массиве). Возвращает -1 если массив переполнен —
 * тогда check_triggers НЕ ставит flags=1 и запись ретраится (оригинальное
 * поведение при пустой free-list, file 0x4F3D). */
static int alloc_obj_slot(void)
{
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) return i;
    }
    if (g_num < MAX_AI) return g_num++;
    return -1;
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
    audio_play_sfx(1);   /* враг убит — play_sound(1), KARNOV.EXE @ 0xFC5A */
    /* Запись врага остаётся flags=1 (one-shot): убитый не возвращается,
     * пока rearm-цикл после смерти игрока не перевзведёт его. */
    if (o->is_boss) {
        g_boss_dead       = true;
        g_boss_dead_frame = frame;
        g_boss_x          = o->x;
    }
}

/* Уничтожить всех живых damageable-врагов на экране (Bomb / Clapper).
 * exclude_boss=true (Clapper) пропускает boss'а; false (Bomb) убивает всех.
 * Логика зеркалит ai_notify_enemy_hit: снимаем alive (записи остаются
 * flags=1 — one-shot), отмечаем boss_dead. */
void ai_clear_enemies(bool exclude_boss)
{
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *o = &g_obj[i];
        if (!o->alive) continue;
        if (!is_damageable_obj(o) && !o->is_boss) continue;
        if (exclude_boss && o->is_boss) continue;
        o->alive = false;
        if (o->is_boss) {
            g_boss_dead       = true;
            g_boss_dead_frame = 0;
            g_boss_x          = o->x;
        }
    }
    audio_play_sfx(1);   /* enemy-kill sfx */
}

/* Bomb-предмет: спавнит анимированную вспышку в позиции Karnov'а и убивает
 * всех врагов (включая boss'а) через ai_clear_enemies(false). */
void ai_bomb_explode(void)
{
    int slot = alloc_obj_slot();
    if (slot >= 0) {
        g_obj[slot] = (ai_obj_t){
            .alive       = true,
            .type        = TYPE_BOMB_BLAST,
            .x           = physics_get_x(),
            .y           = physics_get_y() - 6,   /* по центру тела Karnov'а, а не у ног */
            .spawn_frame = g_ai_frame,
        };
    }
    ai_clear_enemies(false);
}

bool ai_boss_dead(void)        { return g_boss_dead;    }
bool ai_scroll_picked(void)    { return g_scroll_picked; }
u16  ai_boss_dead_frame(void)  { return g_boss_dead_frame; }
int  ai_scroll_target_x(void)  { return g_boss_x;        }

/* Свиток (type5 custom=999) уже заспавнен и падает? Karnov должен начать
 * авто-подход к свитку ТОЛЬКО после того как свиток начал выпадать. */
bool ai_scroll_spawned(void)
{
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].type == 5 && g_obj[i].custom == 999)
            return true;
    return false;
}

/* Свиток уже попал в видимую область экрана? Свиток спавнится высоко
 * (y=50) и падает; Karnov должен начать подход ТОЛЬКО когда свиток виден на
 * экране (упал в viewport), а не раньше. cam_x/cam_y — scroll камеры,
 * view_w/view_h — размеры игрового окна. */
bool ai_scroll_onscreen(int cam_x, int cam_y, int view_w, int view_h)
{
    for (int i = 0; i < g_num; ++i) {
        if (g_obj[i].alive && g_obj[i].type == 5 && g_obj[i].custom == 999) {
            int sx = g_obj[i].x - cam_x;
            int sy = g_obj[i].y - cam_y;
            return sx >= 0 && sx <= view_w && sy >= 0 && sy <= view_h;
        }
    }
    return false;
}

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
    audio_play_sfx(7);   /* выстрел — play_sound(7), KARNOV.EXE @ 0x10515 (fireball spawn) */
}
