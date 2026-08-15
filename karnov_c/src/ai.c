/* ai.c — таблица обработчиков AI (порт AI_HANDLERS из DS:0x10FE).
 *
 * Обработчик вызывается для каждого живого объекта раз в кадр.
 * Покрыты все типы 4..57 + псевдотипы порта (см. AI_HANDLERS ниже).
 * Степень достоверности разная: INIT-константы и раскладки спрайт-боксов
 * взяты из дизассемблирования, TICK-логика у большинства типов ещё
 * реконструирована «по смыслу» — такие места помечены в комментариях.
 *
 * Координатная конвенция: o->x — позиция по X в пикселях (= 2 × x_world
 * оригинала), o->y — ВЕРХНЯЯ строка спрайта (KARNOV.md §3.3).
 * «Ноги» объекта = obj_feet(o) = o->y + высота текущего спрайта.
 */
#include "ai.h"
#include "level.h"
#include "physics.h"
#include "sprite.h"
#include "anim_data.h"
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
#define TYPE_BOMB_BLAST        1005
                                      /* анимированная вспышка Bomb-предмета
                                       * (#25→#26→#27→#28, ~50 кадров). */
#define TYPE_ENEMY_SHOT        1006  /* снаряд врага (порт cs:0x11D6:0x2A3) */
#define TYPE_GOLD_BALL         1007
#define TYPE_TRAP_SEGMENT      1008  /* сегмент ловушки типа 42:
                                      * cs:0x161F:0x020A, спрайты
                                      * #271..274, obj_type = 3, hp = 100 */  /* шар «золотого человека» (type 24):
                                      * ребёнок cs:0x1348:0x00D7, спрайты
                                      * #185/#186, hp 10, очки 0x500. */

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
static int  player_shots_alive(void);
static void obj_snap_ground(struct ai_obj *o);

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
    bool   is_boss;       /* goal-объект уровня (его смерть = конец) */
    bool   is_scroll;     /* свиток-награда за goal-объект. ОТДЕЛЬНОЕ поле:
                           * раньше признаком был custom==999, но custom
                           * принадлежит обработчику type 5, который при
                           * приземлении пишет туда 1 — свиток «терялся»,
                           * level-end не наступал, а ai_tick бесконечно
                           * спавнил новые свитки, забивая пул объектов. */
    u16    type;          /* PRIMARY type = индекс AI-обработчика (spawn[0]) */
    u16    subtype;       /* EnemySpawn.subtype → obj_type (+0xE5): категория
                           * pair-loop: 0=Karnov-маркер, 1/7=враг, 4=спец,
                           * 5=предмет, 9=multi-bbox/триггер. */
    u16    init_arg;      /* младший байт trigger → init-аргумент обработчика
                           * (оригинал: spawn[0x10] → sub_timer). */
    int    num_parts;     /* 0 = обычный односпрайтовый рендер через
                           * sprite_for_type; >0 = массив parts[]. */
    ai_part_t parts[MAX_PARTS];
    int    x, y;          /* x — левый край / y — ВЕРХ спрайта (KARNOV.md §3.3) */
    int    cur_h;         /* высота текущего спрайта: «ноги» = y + cur_h */
    /* Подложка движения оригинала (UPDATELOCS, file 0x25B3): позиция,
     * скорость и ускорение — 32-битные Q16.16. Движок каждый кадр делает
     *     x += vx;  y += vy;  vx += ax;  vy += ay
     * и обработчики почти не двигают объект сами — они задают скорости.
     * X здесь в ЕДИНИЦАХ движка (1 ед. = 2 px), Y — в строках; поля
     * x/y выше пересчитываются из них после интегрирования.
     * Включается для типов, чья TICK-логика портирована (флаг phys). */
    bool   phys;
    /* Аним-машина оригинала (ai_dispatch, file 0x0F885): кадр берётся из
     * таблиц ANIM_LISTS (данные из KARNOV.EXE), а не из ручных циклов.
     * tick_period (+0xC9) — раз в столько кадров вызывается обработчик и
     * продвигается кадр; anim_state (+0xD9) — текущее состояние;
     * pending_state — что обработчик просит на этот тик (−1 = ничего). */
    bool   use_anim;
    /* Приватные поля обработчиков — те же, что у ActiveObject оригинала:
     * st_misc (+0xE9), st_aux (+0xEB), st_phase (+0xED), st_ctr (+0xC7 —
     * счётчик КАДРОВ, тикает каждый кадр, а не раз в AI-тик). */
    int    st_misc, st_aux, st_phase, st_ctr;
    u8     anim_state;
    u8     frame_index;
    u8     tick_period;
    int    tick_countdown;
    int    pending_state;
    s32    xq, yq;
    s32    vxq, vyq;
    s32    axq, ayq;
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
 * записи ТОЛЬКО rearm-цикл после смерти игрока (file 0x1015A).
 * Респавна-при-возврате-камеры в оригинале нет, но off-screen despawn
 * ЕСТЬ — в THINKCYCLE (file 0xFD6A): объект, отставший от игрока более
 * чем на 0x7D единиц (250 px) и не являющийся goal-объектом, получает
 * state_byte = 6. В порту пока не реализовано (TODO). */

/* Boss-конец: когда goal-объект убит — g_boss_dead=true и через 30 ticks
 * спавним свиток (scroll item) сверху. После пикапа Karnov-ом инициируем
 * level-end transition. Goal-объект определяется данными (check_triggers
 * file 0x517F..0x518D): на уровне 9 — запись type 56, на остальных —
 * последняя (по sort cur_x) запись таблицы спаунов. */
static bool g_boss_dead = false;
static u16  g_boss_dead_frame = 0;
static bool g_scroll_picked = false;
static int  g_boss_x = 0;   /* X boss'а на момент смерти — фикс. точка падения свитка */
static int  g_boss_y = 0;
/* Левый край камеры — сохраняется каждый кадр из ai_check_triggers;
 * нужен типам, которые живут «пока видны» (23). */
static int  g_camera_left = 0;   /* Y boss'а на момент смерти — с этой высоты падает свиток */

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
        case TYPE_TRAP_SEGMENT:
            return false;                  /* states 4/5/6 возвращают в THINK */
        case TYPE_GOLD_BALL:
            /* Флаг коллизии bbox (+0x2E) шар ставит только на разлёте:
             * висящий и возвращающийся шар неуязвим (file 0x14E93). */
            return o->st_phase < 2;
        default: break;
    }
    /* «Золотой человек» уязвим только собранным (флаг +0x2E ставится
     * при входе в фазу 6, снимается в остальных — file 0x1508D). */
    if (o->type == 24) return o->custom != 0;
    /* Босс LEVEL4 неуязвим, пока дремлет и падает: box0.hitflag = 1
     * ставится только при входе в боевую подфазу 6 (file 0x17E37). */
    if (o->type == 40) return o->custom != 0;
    /* Босс LEVEL3 так же неуязвим, пока стоит (file 0x17867). */
    if (o->type == 38) return o->custom != 0;
    switch (o->type) {
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

/* ─── Высота объекта ───────────────────────────────────────────────────
 * o->y — ВЕРХНЯЯ строка спрайта (конвенция оригинала, см. KARNOV.md §3.3
 * и update_bboxes @ file 0x0247A). Обработчикам же нужны «ноги» — для
 * контакта с полом и для слежения за Karnov'ом (его physics-Y = ноги).
 * cur_h обновляется раз в тик перед вызовом обработчика. */
static u16 sprite_for_type(u16 type, u16 anim_phase);

static int obj_height(const ai_obj_t *o)
{
    if (o->num_parts > 0) {
        int bottom = 0;
        for (int i = 0; i < o->num_parts; ++i) {
            const sprite_t *ps = sprite_find(o->parts[i].sprite);
            int b = (int)o->parts[i].dy + (ps ? (int)ps->h : 16);
            if (b > bottom) bottom = b;
        }
        if (bottom > 0) return bottom;
    }
    u16 sid = 0xFFFF;
    /* Приоритет — аним-таблицы САМОГО EXE: ручной sprite_for_type у
     * многих типов возвращал случайный кадр (у типа 15 — высотой 16 px),
     * из-за чего постановка на землю в ai_spawn_from_record сажала врага
     * НИЖЕ пола, и он потом проваливался сквозь карту. Предметы (4/5/6)
     * рисуются своей иконкой и сюда не попадают. */
    if (o->type != 4 && o->type != 5 && o->type != 6 && o->type < 58) {
        const anim_list_t *al = &ANIM_LISTS[o->type];
        if (al->count) {
            int st = (o->anim_state < al->count) ? o->anim_state : 0;
            const anim_state_t *as = &al->states[st];
            if (as->n) sid = as->spr[(o->frame_index < as->n) ? o->frame_index : 0];
        }
    }
    if (sid == 0xFFFF)
        sid = ((o->type == 4 || o->type == 5 || o->type == 6) && o->item_sprite > 0)
                  ? o->item_sprite
                  : sprite_for_type(o->type, o->anim_phase);
    const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
    return s ? (int)s->h : 16;
}

/* ─── UPDATELOCS: центральный интегратор (file 0x25B3) ─────────────────
 * Точный порт: x += vx; y += vy; vx += ax; vy += ay (всё Q16.16).
 * Вызывается ДО обработчиков, как фаза 1 кадрового цикла до THINKCYCLE. */
/* Q16.16-константы скоростей: hi — целая часть, lo — дробная. */
#define QV(hi, lo)  ((s32)(((u32)(u16)(hi) << 16) | (u16)(lo)))
#define Q_1_0    QV(1, 0)
#define Q_1_5    QV(1, 0x8000)
#define Q_2_0    QV(2, 0)
#define Q_2_5    QV(2, 0x8000)
#define Q_0_5    QV(0, 0x8000)

static void obj_integrate(ai_obj_t *o)
{
    o->xq  += o->vxq;
    o->yq  += o->vyq;
    o->vxq += o->axq;
    o->vyq += o->ayq;
    o->x = (int)(o->xq >> 16) * 2;   /* единицы движка → пиксели порта */
    o->y = (int)(o->yq >> 16);
}

/* Перевести объект на Q16.16-подложку, взяв текущие x/y как старт. */
static void obj_phys_begin(ai_obj_t *o)
{
    o->xq = (s32)(o->x / 2) << 16;
    o->yq = (s32)o->y << 16;
    o->phys = true;
}

/* ─── Аним-машина (порт ai_dispatch state 2 + биты 2/4 ai_result) ──────
 * anim_begin — то, что делает INIT типа: период тика и стартовое
 * состояние. anim_apply — то, что делает диспетчер после обработчика:
 * смена состояния (без рестарта, если состояние то же) и запись
 * спрайта/смещения в bbox[0]. */
static void anim_begin(ai_obj_t *o, int tick_period, int state)
{
    o->use_anim      = true;
    o->tick_period   = (u8)tick_period;
    o->tick_countdown = tick_period;
    o->anim_state    = (u8)state;
    o->frame_index   = 0;
    o->pending_state = -1;
}

static void anim_apply(ai_obj_t *o)
{
    const anim_list_t *al = (o->type < 58) ? &ANIM_LISTS[o->type] : NULL;
    if (!al || al->count == 0) return;
    if (o->pending_state >= 0 && o->pending_state < al->count &&
        o->pending_state != o->anim_state) {
        o->anim_state  = (u8)o->pending_state;
        o->frame_index = 0;                    /* рестарт только при СМЕНЕ */
    }
    if (o->anim_state >= al->count) o->anim_state = 0;
    const anim_state_t *st = &al->states[o->anim_state];
    if (o->frame_index >= st->n) o->frame_index = 0;
    o->num_parts       = 1;
    o->parts[0].sprite = st->spr[o->frame_index];
    o->parts[0].dx     = (s16)(st->off[o->frame_index * 2] * 2);  /* ед. → px */
    o->parts[0].dy     = st->off[o->frame_index * 2 + 1];
    o->facing          = 1;    /* направление уже закодировано состоянием */
}

/* Мировая Y «ног» объекта. */
static int obj_feet(const ai_obj_t *o)
{
    return o->y + (o->cur_h > 0 ? o->cur_h : obj_height(o));
}

/* Как INIT типа размещает объект по вертикали. Установлено сканом
 * far-call'ов по KARNOV.EXE с привязкой адресов к таблице DS:0x10FE:
 *
 *   PLACE_SNAP_LEFT   — хелпер cs:0xD4B:0x19F (file 0xEE4F): колонка по
 *     ЛЕВОМУ краю объекта, маска props[attr 0] = 0x0001. Типы 8, 11, 14,
 *     17, 24, 28, 29, 31, 32, 46, 48, 53, 54, 55 (8 — sky-спаунер, в
 *     порту не материализуется) + тип 26, который зовёт ядро 0x14C сам.
 *   PLACE_SNAP_CENTER — хелпер cs:0xD4B:0x1DE (file 0xEE8E): колонка по
 *     ЦЕНТРУ спрайта (x + sprite_w/2), маска 0x0089 (пол | стена |
 *     категория 7). Типы 15, 35, 36, 37, 38, 40.
 *   PLACE_LIFT16      — тип 47: INIT делает y_world -= [DS:0x10D4] = 16.
 *
 * Без этого враги перечисленных типов рисовались НИЖЕ, чем нужно: в их
 * записях spawn_y — линия земли, а не верх спрайта. */
typedef enum {
    PLACE_NONE = 0,
    PLACE_SNAP_LEFT,     /* хелпер 0x19F: колонка по левому краю, маска 0x0001 */
    PLACE_SNAP_CENTER,   /* хелпер 0x1DE: колонка по центру, маска 0x0089 */
    PLACE_LIFT,          /* y -= const (константа из DS, см. type_place_lift) */
    PLACE_ALIGN10,       /* y -= y % 10 — выравнивание по строке тайлов */
    PLACE_PLAYER_Y       /* y = player_y − 22 (тип 18 игнорирует spawn_y) */
} place_mode_t;

static place_mode_t type_place_mode(u16 type)
{
    switch (type) {
        case 11: case 14: case 17: case 24: case 26: case 28: case 29:
        case 31: case 32: case 46: case 48: case 53: case 54: case 55:
            return PLACE_SNAP_LEFT;
        case 15: case 35: case 36: case 37: case 38: case 40:
            return PLACE_SNAP_CENTER;
        case 12: case 13: case 47:
            return PLACE_LIFT;
        case 51: case 52:
            return PLACE_ALIGN10;
        case 18:
            return PLACE_PLAYER_Y;
        default:
            return PLACE_NONE;
    }
}

/* Константа подъёма в state 0 (значения байтов из DS-образа):
 *   тип 12 → DS:0x0FC1 = 16, тип 13 → DS:0x1002 = 26, тип 47 → DS:0x10D4 = 16. */
static int type_place_lift(u16 type)
{
    switch (type) {
        case 12: return 16;
        case 13: return 26;
        case 47: return 16;
        default: return 0;
    }
}

/* ─── Handlers ──────────────────────────────────────────────────────── */

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
            int kx = physics_get_left_x();
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
        int rock_y  = o->y + 6;        /* руки ≈ верх спрайта + 6 */
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
    } else if (o->vy_q16 > 0 && level_solid_at(o->x, obj_feet(o) + 1)) {
        floor_hit = 1;
        floor_y_snap = o->y;
    } else if (o->vy_q16 > 0 && obj_feet(o) >= level1_floor) {
        floor_hit = 1;
        floor_y_snap = level1_floor - (o->cur_h > 0 ? o->cur_h : obj_height(o));
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

/* TYPE_PLAYER_FIREBALL — снаряд Karnov-а. Порт трёх обработчиков оригинала
 * (cs:0xFFA:0x00C2 / 0x00FF / 0x0183) с общим INIT'ом cs:0xFFA:0x0009:
 *   tick_period = 2, +0xE5 = 2, +0xE7 = 10, ++[0x1424] (счётчик живых),
 *   vx = ±(6.0 или 7.0) единиц/кадр (= 12/14 px), state_list = DS:0x16B2,
 *   состояние анимации = (firepower > 1).
 * Скорости ТЕПЕРЬ байт-верифицированы (раньше здесь стояло 6 px/кадр —
 * вдвое медленнее оригинала). */

/* Урон снаряда Karnov-а по врагу. В оригинале (level.cpp pair-loop,
 * cs:0xFC2C) урон = hit_points САМОГО снаряда: `b->hit_points -= a->hit_points`,
 * где a — атакующий объект игрока. HP врага (spawn.trigger>>8) — это ПУЛ:
 * все значения в LEVEL%d.BIN кратны/сравнимы с 10 (обычные враги=10 → 1 удар,
 * type18=20 → 2, type13/15=30 → 3, боссы 95..250 → 10..25). Отсюда damage=10.
 * НЕ байт-верифицировано (handler Karnov-fire не декомпилирован), но это
 * единственное значение, при котором обычные враги гибнут с одного попадания
 * И все HP-пулы дают целое число попаданий. */
#define PLAYER_FIREBALL_DAMAGE 10

/* Варианты снаряда: три разных обработчика оригинала (см. spawn_player_shot,
 * file 0x12F4F — таблица переходов по firepower). */
enum { SHOT_STRAIGHT = 0,   /* cs:0xFFA:0x00C2 — vx = ±6.0, летит ровно      */
       SHOT_RISING   = 1,   /* cs:0xFFA:0x00FF — vx = ±7.0, vy = −5.0 на 16px */
       SHOT_GROUND   = 2 }; /* cs:0xFFA:0x0183 — vx = ±7.0, падает до земли   */

static void ai_player_fireball(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;

    /* Движение — Q16.16-интегратор, как у всех объектов движка. */
    obj_integrate(o);
    o->vx = (o->vxq > 0) ? 1 : -1;      /* знак для прочего кода порта */

    switch (o->st_phase) {
    case SHOT_RISING:
        /* Подъём ровно на 16 px от точки выстрела, затем горизонтально
         * (cs:0xFFA:0x0147: `if (y < target) { vy = 0; flag = 0 }`). */
        if (o->st_aux && o->y < o->st_misc) { o->vyq = 0; o->st_aux = 0; }
        break;
    case SHOT_GROUND: {
        /* Каждый тик щупается тайл ПОД снарядом: категории 2 и 9 (небо/
         * фон) — продолжаем падать со скоростью 5.0, иначе ложимся на
         * поверхность (vy = 0). Тайл В точке снаряда категории 3 или 4 —
         * снаряд гаснет (cs:0xFFA:0x01B3). */
        int below = level_tile_attr_at(o->x, o->y + TILE_H);
        o->vyq = (below == 2 || below == 9) ? QV(5, 0) : 0;
        int here = level_tile_attr_at(o->x, o->y);
        if (here == 3 || here == 4) { o->alive = false; return; }
        break;
    }
    default: break;
    }

    /* Анимация: усиленный выстрел (firepower > 1) — 4-кадровый цикл
     * #2,#2,#3,#3 при tick_period = 2; обычный — один кадр #1
     * (state_list DS:0x16B2). */
    if (o->custom) o->anim_phase = (u16)(((frame - o->spawn_frame) / 2) & 3);

    /* Despawn off-screen — теперь world-coords, не screen. */
    int world_w = (int)level_num_map_cols * TILE_W;
    if (o->x < -8 || o->x > world_w + 8) {
        o->alive = false;
        return;
    }

    /* Столкновение с живыми damageable-врагами. bbox fireball'а — 8×6.
     * bbox противника — приближённо 16×24 от его (x, y) = центр ног.
     * Простое AABB-пересечение. */
    /* bbox снаряда — от его (x, y) = левый-верх, размером со спрайт
     * (#1 = 4×4, усиленный #2/#3 — до 6×6). */
    int fx0 = o->x, fx1 = o->x + (o->custom ? 6 : 4);
    int fy0 = o->y, fy1 = o->y + (o->custom ? 6 : 4);
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *e = &g_obj[i];
        if (!e->alive) continue;
        if (e == o) continue;
        if (!is_damageable_obj(e) && !e->is_boss) continue;
        /* bbox врага в объектной конвенции: (x, y) — левый-верх,
         * размеры — из спрайта (в оригинале это box.x0..x1/y0..y1). */
        const sprite_t *es = sprite_find(sprite_for_type(e->type, e->anim_phase));
        int ew = es ? (int)es->art_w : 16;
        int ex0 = e->x, ex1 = e->x + ew;
        int ey0 = e->y, ey1 = e->y + (e->cur_h > 0 ? e->cur_h : 24);
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
 * Все AI ниже — damageable (см. is_damageable_obj). Karnov-fireball
 * убивает с одного попадания, что упрощает геймплей. */


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
    int kx = physics_get_left_x(), ky = physics_get_top_y();
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
        int feet = obj_feet(o);
        if (ky < feet - 1)      o->y -= 1;
        else if (ky > feet + 1) o->y += 1;
    }
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
    /* Контакт с полом считаем по НОГАМ (o->y — верх спрайта). */
    int h = o->cur_h > 0 ? o->cur_h : obj_height(o);
    int feet = o->y + h;
    if (level_solid_at(o->x, feet + 1)) {
        o->y = (feet / 10) * 10 - h;   /* ноги на верхнюю кромку строки */
        o->custom = 1;
    }
    /* Страховка ТОЛЬКО от вылета за нижний край карты. Раньше здесь
     * стояло «feet >= physics_get_ground_y() → положить на этот пол», и на
     * многоярусных уровнях предмет, летящий мимо площадок, ПОДТЯГИВАЛСЯ
     * ВВЕРХ к расчётному «основному» полу: на LEVEL4 это y=373, выше
     * Карнова (его ноги на 480), из-за чего свиток за боссом-«гусеницей»
     * оказывался недосягаем и уровень нельзя было закончить. */
    else if (o->y + h >= (int)level_num_map_rows * TILE_H) {
        o->y = (int)level_num_map_rows * TILE_H - h;
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
        int target = physics_get_top_y() - 40;
        if (o->home_y < target) {
            o->home_y += 2;
            descending = true;
        }
    } else {
        int kx = physics_get_left_x();
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
    /* Колебание только ВВЕРХ от точки спавна: для type 14 INIT оригинала
     * ставит объект ногами на пол (хелпер cs:0xD4B:0x19F), и симметричная
     * синусоида утапливала бы его в землю на половине периода. */
    o->y = o->home_y - (sin16[phase] < 0 ? -sin16[phase] : sin16[phase]);

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


/* Type 19 — медленный враг (ENEMY_GUIDE: sprite 0xAC=172, sub_timer_max=1000
 * — почти статичный). Использует helper 0x4B36, общий с type 9.
 * Реализация: декорация с очень медленной анимацией. */
static void ai_slow_helper(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    o->anim_phase = (frame >> 6) & 1;       /* очень медленная флипа */
}



/* ═══ Портированные TICK'и (декомпиляция KARNOV.EXE, 2026-08-14) ═══════
 *
 * Общая механика: обработчик вызывается раз в tick_period кадров
 * (THINKCYCLE, file 0x0FD88), скорость же интегрируется КАЖДЫЙ кадр
 * (UPDATELOCS). Кадр анимации берётся из таблиц ANIM_LISTS; обработчик
 * лишь просит состояние через o->pending_state.
 * Единицы: vxq — Q16.16 в единицах движка (1 ед. = 2 px), vyq — Q16.16
 * в пикселях. Константы ниже — как в оригинале.
 */

/* Позиция игрока в конвенции объектов движка: X — левый край в 2px-
 * единицах, Y — верх спрайта. Оригинал сравнивает координаты врагов
 * именно с player->x_world/+0x08 и y_world/+0x0C (кэш [DS:0xD8BC/0xD8BE],
 * file 0x10312). */
static int player_units(void) { return physics_get_left_x() / 2; }
static int obj_units(const ai_obj_t *o) { return o->x / 2; }

/* Снаряд врага: летит по прямой, гаснет за краем мира. Порт объекта,
 * который оригинал создаёт через spawn_projectile (cs:0x11D6:0x139) с
 * обработчиком cs:0x11D6:0x2A3. kind → набор кадров снаряда. */
static void spawn_enemy_shot(int x, int y, int dir, s32 speed_units, int kind)
{
    int slot = alloc_obj_slot();
    if (slot < 0) return;
    g_obj[slot] = (ai_obj_t){
        .alive = true, .type = TYPE_ENEMY_SHOT, .subtype = 3,
        .x = x, .y = y, .hp = 10, .level_idx = -1, .facing = dir ? 1 : -1,
        .custom = kind,
    };
    obj_phys_begin(&g_obj[slot]);
    g_obj[slot].vxq = dir ? speed_units : -speed_units;
}

static void ai_enemy_shot(ai_obj_t *o, u16 frame, u8 in)
{
    (void)in;
    /* Кадры снаряда из аним-скрипта оригинала (state_list DS:0x1784):
     * kind 1 → 1,2,3,2; kind 5 → 14,15 (влево); kind 6 → 16,17 (вправо). */
    static const u16 K1[4] = { 1, 2, 3, 2 };
    o->anim_phase = (u16)((frame >> 1) & 3);
    if (o->custom == 1) o->item_sprite = K1[o->anim_phase];
    else o->item_sprite = (u16)((o->vxq < 0 ? 14 : 16) + ((frame >> 2) & 1));
    int world_w = (int)level_num_map_cols * TILE_W;
    if (o->x < -16 || o->x > world_w + 16) o->alive = false;
}

/* ── Тип 11 — наземный мечник (handler cs:0x11D6:0x4CA, file 0x13A2A) ──
 * INIT: tick_period=2, state_list DS:0x1856, фаза 1 («стоит»).
 * TICK (file 0x13AC4): фаза 0 «идёт» → при |dx| < 10 ед. остановиться
 * (set_motion(0)) и переключиться на анимацию удара (dir+2); фаза 1
 * «стоит» → при |dx| > 10 ед. пойти со скоростью set_motion(±2.0)/2 =
 * ±1.0 ед./кадр. Анимация: 0/1 — шаг влево/вправо, 2/3 — удар. */
static void ai_type11(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    int sdx = player_units() - obj_units(o);
    int adx = sdx < 0 ? -sdx : sdx;
    int dir = sdx > 0;
    o->pending_state = dir;
    if (!o->use_anim) {                       /* state 1 — INIT */
        anim_begin(o, 2, dir);
        obj_phys_begin(o);
        o->st_phase = 1;
        return;
    }
    if (o->st_phase == 0) {
        if (adx < 10) {
            o->st_phase = 1;
            o->vxq = o->vyq = o->axq = o->ayq = 0;   /* set_motion(0) */
            o->pending_state = dir + 2;
        }
    } else {
        if (adx > 10) {
            o->st_phase = 0;
            o->vxq = dir ? Q_1_0 : -Q_1_0;
        }
    }
}

/* ── Тип 13 — метатель (handler cs:0x11D6:0x6A8, file 0x13C08) ─────────
 * INIT: tick_period=4, state_list DS:0x18CC, misc = (init_arg>=10)?3:0,
 * aux (idle-таймер) = 25. TICK: в покое ждём, пока aux не уйдёт ниже 0,
 * затем серия из 4 бросков; сам бросок — на кадре 1 анимации замаха. */
static void ai_type13(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    int sdx = player_units() - obj_units(o);
    int dir = sdx >= 0;
    o->pending_state = dir;
    if (!o->use_anim) {
        anim_begin(o, 4, dir);
        o->st_misc = (o->init_arg >= 10) ? 3 : 0;
        o->st_aux  = 25;
        return;
    }
    if (o->st_misc == 0) {                       /* покой */
        if (o->st_aux < 0) { o->st_misc = 4; o->pending_state = dir + 2; }
    } else {                                     /* серия бросков */
        if (o->frame_index == 1) {
            /* Снаряд — тип 12 оригинала: vx = ±3.0 ед./кадр. */
            spawn_rock(o->x, o->y + 6, dir ? 6 : -6, frame);
            if (--o->st_misc != 0) o->frame_index = (o->st_ctr & 3) ? 1 : 0;
            o->pending_state = dir + 2;
        } else if (o->frame_index == 0) {
            o->st_aux = 25; o->st_misc = 0; o->pending_state = dir;
        } else {
            o->pending_state = dir + 2;
        }
    }
    --o->st_aux;
}

/* ── Тип 17 — скелет на страусе (handler cs:0x11D6:0x617) ──────────────
 * Самый простой: игрока не видит вовсе, всегда идёт ВЛЕВО −3.0 ед./кадр,
 * анимация одна (3 кадра), tick_period=2. */
static void ai_type17(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 2, 0);
        obj_phys_begin(o);
        o->vxq = -(s32)QV(3, 0);
    }
    o->pending_state = 0;
}

/* ── Тип 16 — комарик (handler cs:0x11D6:0x1033, file 0x14593) ─────────
 * 7-фазный автомат; направление фиксируется в INIT. tick_period=3.
 * Фазы: 0/1 — отсчёт 13 тиков и разворот; 2 — снижение до уровня игрока;
 * 3 — горизонталь до |dx| > 20 ед.; 4 — подъём; 5 — выравнивание;
 * 6 — инерция (обработчик её не трогает). */
static void ai_type16(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, (player_units() > obj_units(o)) ? 1 : 0);
        obj_phys_begin(o);
        o->vxq = -Q_1_5;
        o->st_misc = 13;      /* счётчик фазы */
        o->st_phase = 0;      /* фаза */
        return;
    }
    int sdx = player_units() - obj_units(o);
    int dy  = physics_get_top_y() - o->y;
    int dir = sdx > 0;
    switch (o->st_phase) {
    case 0: case 1:
        if (--o->st_misc) break;
        if (o->st_phase == 1) {
            o->st_phase = 2;
            o->st_aux = dir;                      /* зафиксированный facing */
            o->vxq = dir ? Q_1_5 : -Q_1_5;
            o->vyq = (dy >= 0) ? Q_2_5 : -Q_2_5;
        } else {
            o->st_phase = 1; o->st_misc = 13; o->vxq = Q_1_5;
        }
        break;
    case 2:
        /* Спуск до уровня игрока: остаёмся в фазе, пока верх «комарика»
         * ВЫШЕ верха Карнова (dy > 0). Оригинал: `or di,di; jng` — переход
         * при dy <= 0 (file 0x1470F). Прежде условие было инвертировано,
         * и фаза заканчивалась сразу, поэтому спуск шёл «мимо» игрока. */
        if (dy > 0) break;
        o->st_phase = 3; o->vxq = dir ? Q_1_5 : -Q_1_5; o->vyq = 0;
        break;
    case 3:
        if (!((o->st_aux && sdx < -20) || sdx > 20)) break;
        o->st_phase = 4; o->vxq = dir ? Q_1_5 : -Q_1_5; o->vyq = -Q_2_5;
        break;
    case 4:
        if (dy <= 40) break;
        o->st_phase = 5; o->vxq = dir ? Q_1_5 : -Q_1_5; o->vyq = Q_2_5;
        break;
    case 5:
        if (dy >= 0) break;
        o->st_phase = 6; o->vxq = dir ? Q_1_5 : -Q_1_5; o->vyq = 0;
        break;
    default: break;                                /* фаза 6 — только инерция */
    }
}

/* ── Тип 10 — демон на облаке (handler cs:0x11D6:0x0413, file 0x140F3) ─
 *
 * Маятник: INIT ставит state_list = DS:0x180A, аним-состояние 0,
 * vx = −0.9375 (0xFFFF:0x1000), frame_ctr(+0xC7) = 8 и вертикальное
 * УСКОРЕНИЕ ay = ±0.625 по init_arg (>= 10 → вверх, иначе вниз).
 * THINK: раз в 16 кадров (`test frame_ctr, 0xF`) знак ay меняется —
 * отсюда синусоидальный «полёт по волне». Очки за смерть 0x110.
 * Никакого преследования игрока у типа нет. */
static void ai_type10(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 1, 0);
        obj_phys_begin(o);
        o->ayq   = (o->init_arg >= 10) ? -QV(0, 0xA000) : QV(0, 0xA000);
        o->vxq   = (s32)0xFFFF1000;          /* −0.9375 ед./кадр */
        o->st_ctr = 8;                        /* +0xC7 */
        return;
    }
    if ((o->st_ctr & 0xF) == 0) o->ayq = -o->ayq;
}

/* ── Тип 23 — летящий шар (handler cs:0x11D6:0x0008, file 0x13508) ────
 *
 * INIT: tick_period = 2, box0.hitflag = 1, box0.sprite = 0xB9 (#185),
 * vx = ±3.0 ед./кадр (= 6 px) в сторону игрока, phase(+0xED) = 0.
 * THINK: пока шар НЕ побывал в кадре (phase 0) — ждём попадания в окно
 * камеры (хелпер cs:0x6E:0x7A5 по box0), тогда phase = 1; после этого,
 * как только он выходит за окно — ai_result |= 1, объект удаляется.
 * Никакой коллизии с тайлами и наведения у типа НЕТ (в порту были). */
static void ai_type23(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    /* Аним-машина не используется: INIT пишет box0.sprite напрямую и
     * state_list не ставит, поэтому кадр статичен (#185). */
    if (o->custom == 0) {
        int si = (player_units() > obj_units(o));
        o->custom = 1;
        obj_phys_begin(o);
        o->vxq = si ? QV(3, 0) : -QV(3, 0);
        o->st_phase = 0;
        o->facing = si ? 1 : -1;
        return;
    }
    bool on = (o->x + 16 > g_camera_left) && (o->x < g_camera_left + VIEW_W);
    if (o->st_phase) { if (!on) o->alive = false; }
    else             { if (on)  o->st_phase = 1; }
}

/* «Выгул» в прямоугольнике (хелпер cs:0x1692:0x000D) — общий для типов
 * 43/44/45. Прямоугольник выбирается по +0xEB (= init_arg) из таблицы
 * DS:0x1D32 (по 8 байт: x0, y0, x1, y1; X — в единицах движка):
 *     если x вне [x0, x1] → vx = −vx, бит 1;
 *     если y вне [y0, y1] → vy = −vy, бит 2.
 * Возвращает маску сработавших отражений. */
static const s16 ROAM_RECT[3][4] = {
    {  56, 240,  680, 390 },
    { 696, 320,  928, 420 },
    { 936, 240, 1384, 420 },
};

static int obj_bounce_region(ai_obj_t *o)
{
    int idx = o->st_aux;                       /* +0xEB */
    if (idx < 0 || idx > 2) idx = 0;
    const s16 *r = ROAM_RECT[idx];
    int bits = 0;
    if (obj_units(o) > r[2] || obj_units(o) < r[0]) { o->vxq = -o->vxq; bits |= 1; }
    if (o->y > r[3] || o->y < r[1])                 { o->vyq = -o->vyq; bits |= 2; }
    return bits;
}

/* Сегмент ловушки (дочерний обработчик cs:0x161F:0x020A, file 0x17BFA).
 * INIT: obj_type = 3, hp = 100, box0.hitflag = 1, tick_period = 2,
 * state_list = DS:0x1CBE (кадры #271..274), аним-состояние 0.
 * THINK: единственное условие — тайл В ТОЧКЕ сегмента; если он не пуст
 * (категория != 2), объект удаляется (ai_result |= 1). Сегмент никуда
 * не движется, а состояния 4/5/6 возвращают его в THINK, то есть
 * выстрелом он не убивается. */
static void ai_trap_segment(ai_obj_t *o, u16 frame, u8 in)
{
    (void)in;
    o->anim_phase = (u16)(((frame - o->spawn_frame) / 2) & 3);
    int cat = level_tile_attr_at(o->x, o->y);
    if (cat != 2) o->alive = false;
}

/* ── Тип 47 — турель (handler cs:0x1692:0x0479, file 0x18599) ─────────
 *
 * INIT: +0xEB = init_arg, tick_period = 10, y_world поднимается на
 * [DS:0x10D4] = 16 px, state_list = DS:0x1DA4, аним-состояние 0.
 * THINK: объект НЕ движется (прежняя реализация «прыгуна» была
 * выдумкой). Раз в тик (а тик — раз в 10 кадров) с шансом 1 из 16
 * стреляет НАВЕДЁННЫМ снарядом из своей точки в (player.x,
 * player.y + 10) со скоростью 0.75. Очки за смерть 0x1000. */
static void ai_type47(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 10, 0);
        obj_phys_begin(o);
        o->st_aux = (int)o->init_arg;
        return;
    }
    if ((rng_next() & 0xF) != 0) return;
    int slot = alloc_obj_slot();
    if (slot < 0) return;
    ai_obj_t *b = &g_obj[slot];
    *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT, .subtype = 3,
                     .x = o->x, .y = o->y, .hp = 10, .level_idx = -1,
                     .facing = (player_units() > obj_units(o)) ? 1 : -1 };
    obj_phys_begin(b);
    /* Единичный вектор к цели со скоростью 0.75 ед./кадр. */
    int tx = physics_get_left_x() / 2 - obj_units(o);
    int ty = physics_get_top_y() + 10 - o->y;
    int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
    if (len < 1) len = 1;
    b->vxq = ((s32)tx * QV(0, 0xC000)) / len;
    b->vyq = ((s32)ty * QV(0, 0xC000)) / len;
}

/* ── Тип 42 — периодическая ловушка (cs:0x161F:0x029F, file 0x17C8F) ───
 *
 * INIT: tick_period = 3, подфаза (+0xE9) = 0, счётчик (+0xEB) =
 * init_arg * 10, при phase(+0xED) != 0 объект поднимается на 20 px,
 * num_bboxes = 1, state_list = DS:0x1CF2.
 * THINK: подфазы 0/1 — покой: аним-состояние = phase, кадр 0, счётчик
 * уменьшается на tick_period за тик; когда он вышел в ноль (или раньше
 * с шансом 1/128) — подфаза 2. Подфазы 2/3 — срабатывание: аним
 * phase + 2, и на ПОСЛЕДНЕМ кадре анимации вызывается cs:0x161F:0x00FC,
 * после чего счётчик = 500 и возврат в покой.
 *
 * НЕ ПЕРЕНЕСЕНО: сам cs:0x161F:0x00FC — он рождает ВЕРТИКАЛЬНЫЙ ряд из
 * 5 объектов (x + 12 ед., y + (i − 2) * 18) с обработчиком
 * cs:0x161F:0x020A (obj_type = 3, hp = 100, hitflag = 1). Этот дочерний
 * обработчик пока не декомпилирован, и выдумывать его поведение я не
 * стал — ловушка тикает и анимируется, но пока не выпускает «детей». */
static void ai_type42(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, 0);
        o->st_misc = 0;                          /* +0xE9 */
        o->st_aux  = (int)o->init_arg * 10;      /* +0xEB */
        if (o->st_phase != 0) o->y -= 20;
        return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = o->st_phase;
        o->frame_index = 0;
        /* fallthrough */
    case 1:
        o->st_aux -= o->tick_period;
        if (o->st_aux > 0 && (rng_next() & 0x7F)) break;
        o->st_misc = 2;
        break;
    case 2:
        o->pending_state = o->st_phase + 2;
        o->frame_index = 0;
        /* fallthrough */
    case 3: {
        const anim_list_t *al = (o->type < 58) ? &ANIM_LISTS[o->type] : NULL;
        int n = (al && al->count && o->anim_state < al->count)
                    ? al->states[o->anim_state].n : 1;
        if (o->frame_index != n - 1) break;
        /* cs:0x161F:0x00FC — вертикальный ряд из 5 сегментов:
         * x + 12 ед. (= 24 px), y + (i − 2) * 18. */
        for (int i = 0; i < 5; ++i) {
            int slot = alloc_obj_slot();
            if (slot < 0) break;
            g_obj[slot] = (ai_obj_t){
                .alive = true, .type = TYPE_TRAP_SEGMENT, .subtype = 3,
                .hp = 100, .level_idx = -1, .facing = 1,
                .x = o->x + 24, .y = o->y + (i - 2) * 18,
                .spawn_frame = frame,
            };
        }
        o->st_aux  = 0x1F4;                      /* 500 */
        o->st_misc = 0;
        break;
    }
    default: break;
    }
}

/* ── Тип 38 — босс LEVEL3 «динозавр» (cs:0x151A:0x0EC7, file 0x17867) ──
 *
 * INIT: tick_period = 7, подфаза (+0xE9) = 0, phase(+0xED) = СНИМОК
 * стартового hp, aux(+0xEF) = 0, state_list = DS:0x1CA4,
 * box0.hitflag = 0 (НЕУЯЗВИМ), frame_ctr = 0x4B.
 *
 * Каждый тик: d = |dx| + |dy| до игрока (хелпер cs:0xD4B:0x105);
 * aux |= (d < 0x64) — «заметил», флаг липкий. Если снимок hp минус
 * текущий hp превысил 0x46 (70 урона), hp сразу ставится в −20, т.е.
 * босс умирает — его живучесть задана этим порогом, а не полем записи.
 *
 * Подфазы: 0/1 — стойка (кадр 0), установка на землю по центру спрайта
 * 0x101; ждёт, пока не заметит игрока И пока не пройдёт 0x4B кадров;
 * 2/3 — атака: кадр 1, счётчик кадров = 0, box0.hitflag = 1 (теперь
 * уязвим), прыжок vx = −1.5, vy = −2.0 с ускорением +0.25; догнав
 * игрока (self.x <= player.x) горизонтальная скорость обнуляется.
 * Очки за смерть 0x20000.
 *
 * Не портированы два вызова из подфазы 2 — cs:0x151A:0xDDF и 0xE5D
 * (по контексту — обновление коллизии и, вероятно, огненное дыхание). */
static void ai_type38(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 7, 0);
        obj_phys_begin(o);
        o->st_misc  = 0;                    /* +0xE9 */
        o->st_phase = o->hp;                /* +0xED — снимок hp */
        o->st_aux   = 0;                    /* +0xEF — «заметил» */
        o->custom   = 0;                    /* box0.hitflag — неуязвим */
        o->st_ctr   = 0x4B;
        return;
    }
    int dx = player_units() - obj_units(o);
    int dy = physics_get_top_y() - o->y;
    int d  = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (d < 0x64) o->st_aux = 1;
    if (o->st_phase - o->hp > 0x46) o->hp = -20;   /* порог живучести */

    int ph = o->st_misc;
    o->st_misc |= 1;
    switch (ph) {
    case 0:
        o->pending_state = 0;
        obj_snap_ground(o);
        /* fallthrough */
    case 1:
        if (!o->st_aux) break;
        if (o->st_ctr <= 0x4B) break;
        o->st_misc = 2;
        break;
    case 2:
        o->pending_state = 1;
        o->st_ctr = 0;
        o->custom = 1;                      /* теперь уязвим */
        o->vxq = (s32)0xFFFE8000;           /* −1.5 */
        o->vyq = -QV(2, 0);                 /* −2.0 */
        o->ayq = QV(0, 0x4000);             /* +0.25 */
        /* cs:0x151A:0xDDF — огненное дыхание: с шансом 1/2 снаряд со
         * скоростью 2.0, иначе 1.5, из (x, y + 7) в сторону игрока. */
        spawn_enemy_shot(o->x, o->y + 7, (player_units() > obj_units(o)),
                         (rng_next() & 1) ? Q_2_0 : Q_1_5, 1);
        /* fallthrough */
    case 3:
        if (obj_units(o) <= player_units()) o->vxq = 0;   /* догнал */
        /* cs:0x151A:0xE5D — посадка: только на снижении, тайл под
         * (x − 0x10 ед., y + высота спрайта) категорий 0, 4 или 7. */
        if (o->vyq > 0) {
            int cat = level_tile_attr_at(o->x - 32, o->y + obj_height(o));
            if (cat == 0 || cat == 4 || cat == 7) {
                o->vyq = 0;
                o->ayq = 0;
                obj_snap_ground(o);
            }
        }
        break;
    default: break;
    }
}

/* ── Тип 40 — босс LEVEL4 «гусеница» (cs:0x161F:0x0447, file 0x17E37) ──
 *
 * INIT: tick_period = 2, подфаза (+0xE9) = 0, state_list = DS:0x1D22.
 * dir = (игрок правее) считается каждый тик и служит аним-состоянием.
 * Подфазы (+0xE9, чётная — одноразовый вход):
 *   0/1 — дремлет: кадр 0, active = 0, box0.hitflag = 0 (НЕУЯЗВИМ);
 *         ждёт, пока игрок не подойдёт ближе 0x78 либо не пройдёт 200
 *         кадров, затем подфаза 2;
 *   2/3 — падение: active = 1, vy = +1.5; пока тайл под нижней кромкой
 *         пуст (категория 2) — летит, иначе подфаза 4;
 *   4/5 — посадка: кадр 0, active = 0, счётчик кадров = 0, установка на
 *         землю по центру (спрайт 0x107), set_motion(0); ждёт, пока
 *         игрок не ближе 0x3C (или уже был бой), и ещё 0x4B кадров,
 *         затем подфаза 6;
 *   6/7 — бой: active = 1, phase = 1, vx = ±0.75 к игроку, счётчик
 *         кадров = 0 и box0.hitflag = 1 — только теперь босса можно
 *         ранить; далее раз в 8 кадров подключается вертикальный рывок.
 * Порт хранит hitflag в поле custom и учитывает его в is_damageable_obj. */
static void ai_type40(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    int dx  = player_units() - obj_units(o);
    int adx = dx < 0 ? -dx : dx;
    int dir = (dx > 0);

    if (!o->use_anim) {
        anim_begin(o, 2, dir);
        obj_phys_begin(o);
        o->st_misc = 0;                 /* +0xE9 */
        o->custom  = 0;                 /* box0.hitflag — неуязвим */
        return;
    }
    int ph = o->st_misc;
    o->st_misc |= 1;
    switch (ph) {
    case 0:
        o->pending_state = dir;
        o->frame_index = 0;
        o->custom = 0;                  /* неуязвим */
        /* fallthrough */
    case 1:
        if (adx >= 0x78 && o->st_ctr <= 0xC8) break;
        o->st_misc = 2;
        break;
    case 2:
        o->pending_state = dir;
        o->vyq = Q_1_5;
        /* fallthrough */
    case 3: {
        int cat = level_tile_attr_at(o->x, o->y + obj_height(o));
        if (cat == 2) break;            /* ещё падает */
        o->st_misc = 4;
        break;
    }
    case 4:
        o->pending_state = dir;
        o->frame_index = 0;
        o->st_ctr = 0;
        obj_snap_ground(o);
        o->vxq = o->vyq = 0; o->axq = o->ayq = 0;   /* set_motion(0) */
        /* fallthrough */
    case 5:
        if (adx >= 0x3C && o->st_phase == 0) break;
        if (o->st_ctr <= 0x4B) break;
        o->st_misc = 6;
        break;
    case 6:
        o->pending_state = dir;
        o->st_phase = 1;
        o->vxq = dir ? QV(0, 0xC000) : (s32)0xFFFF4000;   /* ±0.75 */
        o->st_ctr = 0;
        o->custom = 1;                  /* теперь уязвим */
        /* fallthrough */
    case 7:
        /* Раз в 8 кадров — вертикальный рывок (frame_ctr >> 3, бит 0). */
        if ((((o->st_ctr >> 3) & 1) == 0)) o->vyq = Q_1_5;
        else                               o->vyq = 0;
        break;
    default: break;
    }
}

/* ── Тип 15 — каменный страж (handler cs:0x11D6:0x0ACB, file 0x1402B) ──
 *
 * Преамбула: si = (игрок правее), аним-состояние = si.
 * INIT: tick_period = 3, state_list = DS:0x1918, аним si + 2 (стойка),
 * подфаза (+0xE9) = 0, +0xEB = x_world (дом).
 * Подфазы (si2 = +0xE9, затем +0xE9 |= 1):
 *   0/1 — спит, пока |dx| > 0x50; ближе — подфаза 2;
 *   2   — прыжок к игроку: с шансом 1/2 vx = ±2.5 (иначе 0), vy = +3.0,
 *         аним si;
 *   3   — в полёте: тайл под (x+5, y+vy+h) категории 2 — ещё летим,
 *         иначе подфаза 4;
 *   4   — посадка: phase(+0xED) = 0, set_motion(0), встать на землю по
 *         центру (спрайт 0x99), ВЫСТРЕЛ в игрока со скоростью 2.25,
 *         аним ((rand&1) ? 2 : 4) + si;
 *   5   — если phase уже 1 или выпал rand&1 → подфаза 6, иначе второй
 *         выстрел (скорость 2.0) и phase = 1;
 *   6   — подскок вверх: vx = 0, vy = −3.0, аним si. */
static void ai_type15(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    int dx = player_units() - obj_units(o);
    int si = (dx > 0);
    int adx = dx < 0 ? -dx : dx;

    if (!o->use_anim) {
        anim_begin(o, 3, si + 2);
        obj_phys_begin(o);
        o->st_misc = 0;                        /* +0xE9 */
        o->st_aux  = obj_units(o);             /* +0xEB — дом */
        return;
    }
    int ph = o->st_misc;
    o->st_misc |= 1;
    switch (ph) {
    case 0:
    case 1:
        if (adx > 0x50) break;                 /* спит, пока игрок далеко */
        o->st_misc = 2;
        break;
    case 2:
        o->vxq = (rng_next() & 1) ? (si ? QV(2, 0x8000) : (s32)0xFFFD8000) : 0;
        o->vyq = QV(3, 0);
        o->pending_state = si;
        /* fallthrough */
    case 3: {
        /* Оригинал: тайл под будущей позицией ног категории 2 — значит
         * ещё воздух. level_solid_at использует ту же маску ходьбы
         * (проходимы только 1 и 2), поэтому проверка эквивалентна.
         * level_solid_at использует ту же маску ходьбы (проходимы только
         * 1 и 2), поэтому проверка эквивалентна оригинальной. */
        int feet = o->y + (int)(o->vyq >> 16) + obj_height(o);
        if (!level_solid_at(o->x + 10, feet)) break;
        o->st_misc = 4;
        break;
    }
    case 4:
        o->st_phase = 0;                       /* +0xED */
        o->vxq = o->vyq = 0; o->axq = o->ayq = 0;   /* set_motion(0) */
        obj_snap_ground(o);
        spawn_enemy_shot(o->x, o->y + 5, si, QV(2, 0x4000), 1);
        o->pending_state = ((rng_next() & 1) ? 2 : 4) + si;
        /* fallthrough */
    case 5:
        if (o->st_phase != 0 || (rng_next() & 1)) { o->st_misc = 6; break; }
        spawn_enemy_shot(o->x, o->y + 5, si, Q_2_0, 1);
        o->st_phase = 1;
        break;
    case 6:
        o->vxq = 0;
        o->vyq = -QV(3, 0);                    /* подскок вверх */
        o->pending_state = si;
        /* +0xE9 |= 1 уже перевёл подфазу в 7 */
        /* fallthrough */
    case 7: {
        /* Пока игрок ниже больше чем на 0x1E — страж «подтягивается»
         * вверх по 3 px за тик и снова уходит в прыжок (0xD55). */
        int dyp = physics_get_top_y() - o->y;
        if (dyp > 0x1E) {
            o->y -= 3;
            o->yq = (s32)o->y << 16;
            o->st_misc = 2;
        } else {
            o->vyq = 0;
            o->st_misc = 3;                    /* иначе — ждать посадки */
        }
        break;
    }
    default: break;
    }
}

/* ── Тип 31 — лев (handler cs:0x1348:0x1B1A, file 0x1679A) ────────────
 *
 * INIT: tick_period = 4, подфаза (+0xE9) = 0, постановка на землю со
 * спрайтом 0xDD (#221), state_list = DS:0x1BC4.
 * THINK (si = +0xE9, затем +0xE9 |= 1):
 *   0/1 — лежит: аним-состояние 0, кадр выбирается случайно
 *         (rand & 0xF) < 4 ? 0 : 1 — «дыхание». Пока лев цел
 *         (hp >= 0x1F) и случайный порог (rand & 0x3F) + 0x64 (100…163)
 *         не меньше счётчика кадров — остаётся лежать; иначе (или сразу
 *         после ранения) → подфаза 2;
 *   2   — бросок: аним-состояние 1, vx = −1.5 (бежит влево, на игрока).
 * Очки за смерть 0x7000 — как у самой хозяйки. */
static void ai_type31(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 0);
        obj_phys_begin(o);
        o->st_misc = 0;                     /* +0xE9 */
        obj_snap_ground(o);
        return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = 0;
        o->frame_index = ((rng_next() & 0xF) < 4) ? 0 : 1;
        /* fallthrough */
    case 1:
        if (o->hp >= 0x1F) {
            int t = (int)((rng_next() & 0x3F) + 0x64);
            if (t >= o->st_ctr) break;      /* ещё лежим */
        }
        o->st_misc = 2;
        break;
    case 2:
        o->pending_state = 1;
        o->vxq = (s32)0xFFFE8000;           /* −1.5 — бросок влево */
        break;
    default: break;
    }
}

/* ── Тип 32 — босс LEVEL2 (handler cs:0x1348:0x1C34, file 0x168B4) ─────
 *
 * INIT: tick_period = 4, постановка на землю со спрайтом 0xDE (#222),
 * state_list = DS:0x1BE0, аним-состояние 0.
 * THINK: пока frame_ctr(+0xC7) <= 0x5A — стоит в состоянии 0. Как только
 * счётчик перевалил 90, стреляет: spawn_projectile из
 * (x, y + 3 + (rand & 8)) в игрока со скоростью 2.0 и флагом
 * «горизонтально», после чего frame_ctr = 0x5A − ((rand & 0xF) + 0x46),
 * т.е. следующий выстрел через 70…85 кадров, и переключается в
 * состояние 1. Очки за смерть 0x7000. Сам босс НЕ ходит. */
static void ai_type32(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 0);
        obj_snap_ground(o);
        return;
    }
    if (o->st_ctr <= 0x5A) { o->pending_state = 0; return; }

    /* Горизонтальный снаряд в сторону игрока. */
    int si = (player_units() > obj_units(o));
    spawn_enemy_shot(o->x, o->y + 3 + (int)(rng_next() & 8), si, Q_2_0, 1);
    o->st_ctr = 0x5A - (int)((rng_next() & 0xF) + 0x46);
    o->pending_state = 1;
}

/* ── Тип 22 — статичный страж (handler cs:0x1348:0x0C82, file 0x15902) ─
 *
 * Вопреки прежней реконструкции («tile-aware патруль») тип НЕ двигается:
 * его THINK (state 2 = 0x0D1B) состоит ровно из `ai_result |= 4`, то есть
 * только просит применить текущий кадр анимации.
 *
 * INIT: phase(+0xED) = init_arg и он же — номер аним-состояния
 * (state_list = DS:0x1A78); затем вниз по колонке ищется первая строка
 * карты с тайлом props & 1 (пол), и её пиксельный Y (row * 10) кладётся
 * в +0xEB — объект запоминает уровень земли под собой.
 * Смерть (state 4): очки 0x500, затем spawn_projectile из
 * (box0.x0 + 2, box0.y0 + 0x19) и рождение ещё одного объекта. */
static void ai_type22(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 1, (int)o->init_arg);
        o->st_phase = (int)o->init_arg;             /* +0xED */
        int row = level_ground_row_below(o->x, o->y / TILE_H, 0x0001);
        o->st_aux = (row >= 0) ? row * TILE_H : o->y;   /* +0xEB — земля */
        return;
    }
    /* THINK пуст — объект статичен. */
}

/* ── Тип 12 — скачущий валун (handler cs:0x11D6:0x08B3, file 0x13E13) ──
 *
 * INIT: phase(+0xED) = init_arg, misc(+0xE9) = 0, obj_type = 1, hp = 20,
 * bbox активен. При init_arg == 0: y_world поднимается на высоту спрайта,
 * vx = −3.0, vy = −2.5, ay = +0.6666 (0xAAAA) — заброс по параболе.
 * Иначе: vx = +2.0, vy = +2.5, ay = 0. state_list = DS:0x18F4.
 * THINK: тайл под нижней кромкой; если его props & 9 (категории 0, 3, 4 —
 * пол/склон) и объект ЛЕТИТ ВНИЗ (vy > 0) — отскок: первые три раза
 * play_sound(8); при phase != 0 разворачиваются обе скорости, ay
 * становится +0.333 и phase обнуляется, иначе просто vy = −vy. */
static void ai_type12(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 1, 0);
        obj_phys_begin(o);
        o->st_phase = (int)o->init_arg;         /* +0xED */
        o->st_misc  = 0;                        /* +0xE9 — счётчик отскоков */
        o->hp       = 20;
        if (o->st_phase == 0) {
            o->y  -= obj_height(o);
            o->yq  = (s32)o->y << 16;
            o->vxq = -QV(3, 0);
            o->vyq = (s32)0xFFFE8000;           /* −2.5 */
            o->ayq = QV(0, 0xAAAA);             /* +0.667 */
        } else {
            o->vxq = QV(2, 0);
            o->vyq = QV(2, 0x8000);             /* +2.5 */
            o->ayq = 0;
        }
        return;
    }
    int a = level_tile_attr_at(o->x, o->y + obj_height(o));
    bool floor_here = (a == 0 || a == 3 || a == 4);   /* props & 9 */
    if (!floor_here || o->vyq <= 0) return;

    if (o->st_misc++ < 3) audio_play_sfx(8);          /* звук отскока */
    if (o->st_phase != 0) {
        o->vxq = -o->vxq;
        o->vyq = -o->vyq;
        o->ayq = QV(0, 0x5555);                       /* +0.333 */
        o->st_phase = 0;
    } else {
        o->vyq = -o->vyq;
    }
}

/* ── Тип 35 — прыгающий голем (handler cs:0x151A:0x08B0, file 0x17250)
 *
 * INIT: tick_period = 4, подфаза (+0xE9) = 0, +0xEB = 0,
 * phase(+0xED) = (anim_len > 10) — «стреляющий» вариант,
 * state_list = DS:0x1C40, постановка на землю по ЦЕНТРУ (спрайт 0xF5).
 * THINK: направление dirsel = (игрок правее) при |dx| > 30, иначе
 * случайное; подфазы (+0xE9, чётная — одноразовая):
 *   0/1 — покой: кадр 0; «просыпается» (active = 1), если hp < 0x46 или
 *         |dx| < 0x3C; когда анимация досмотрена до последнего кадра —
 *         подфаза 2;
 *   2/3 — прыжок: кадр dirsel+1, постановка на землю (спрайт 0xEF),
 *         vx = 0 в 3 случаях из 4, иначе ±1.0; vy = −1.25, ay = +0.125;
 *         приземлился — подфаза 6;
 *   6/7 — второй прыжок: кадр dirsel+3, vy = −1.25, ay = +0.125; на
 *         подъёме, если игрок ниже на 0x19 — зависание (vy = 0);
 *         приземлился — снова подфаза 2. */
static bool obj_on_ground(const ai_obj_t *o)
{
    return level_solid_at(o->x + 8, o->y + obj_height(o) + 1);
}

static void ai_type35(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 0);
        obj_phys_begin(o);
        o->st_misc  = 0;                        /* +0xE9 */
        o->st_aux   = 0;                        /* +0xEB */
        o->st_phase = (o->init_arg > 10);       /* +0xED — «стреляющий» */
        obj_snap_ground(o);
        return;
    }
    int di = o->st_misc;
    o->st_misc |= 1;
    int dx = player_units() - obj_units(o);
    int adx = dx < 0 ? -dx : dx;
    int dirsel = (adx > 30) ? (dx > 0) : (int)(rng_next() & 1);

    switch (di) {
    case 0:
        o->frame_index = 0;
        o->pending_state = 0;
        /* fallthrough */
    case 1:
        /* +0xDB (active) в оригинале гейтит ПРОДВИЖЕНИЕ анимации в
         * ai_dispatch: пока голем «спит», кадр не меняется и до конца
         * цикла дело не доходит. Просыпается при hp < 0x46 либо когда
         * игрок ближе 0x3C. */
        if (o->hp < 0x46 || adx < 0x3C) o->custom = 1;
        if (!o->custom) { o->frame_index = 0; break; }
        { const anim_list_t *al = &ANIM_LISTS[35];
          int n = (al->count && o->anim_state < al->count)
                      ? al->states[o->anim_state].n : 1;
          if (o->frame_index >= n - 1) o->st_misc = 2; }
        break;
    case 2:
        o->pending_state = dirsel + 1;
        obj_snap_ground(o);
        o->vxq = ((rng_next() & 3) != 0) ? 0 : (dirsel ? Q_1_0 : -Q_1_0);
        o->vyq = (s32)0xFFFEC000;               /* −1.25 */
        o->ayq = QV(0, 0x2000);                 /* +0.125 */
        /* fallthrough */
    case 3:
        if (obj_on_ground(o) && o->vyq > 0) o->st_misc = 6;
        break;
    case 6:
        o->pending_state = dirsel + 3;
        o->vyq = (s32)0xFFFEC000;
        o->ayq = QV(0, 0x2000);
        /* fallthrough */
    case 7:
        if (o->vyq < 0 && (physics_get_top_y() - o->y) > 0x19) {
            o->vyq = 0;                          /* завис над игроком */
            o->ayq = QV(0, 0x2000);
        }
        if (obj_on_ground(o) && o->vyq > 0) o->st_misc = 2;
        break;
    default: break;
    }
}

/* ── Тип 44 — дрейфующий летун (handler cs:0x1692:0x052E, file 0x1864E)
 *
 * INIT: +0xEB = init_arg (номер прямоугольника «выгула»),
 * vx = −0.875 + (rand & 0x1FFF) в Q16.16, vy = +0.1875, tick_period = 3,
 * state_list = DS:0x1DC0, аним-состояние 0.
 * THINK: si = 1 с шансом 1 из 32 (`test rand, 0x1F`); отскок от границ
 * зоны даёт di. Если si или отражение по X — кадр переключается
 * 0 ⇄ 1, а при si дополнительно разворачивается vx. Очки 0x1800. */
static void ai_type44(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, 0);
        obj_phys_begin(o);
        o->st_aux = (int)o->init_arg;
        o->vxq = (s32)0xFFFF2000 + (s32)(rng_next() & 0x1FFF);
        o->vyq = QV(0, 0x3000);
        return;
    }
    int si = ((rng_next() & 0x1F) == 0);
    int di = obj_bounce_region(o);
    if (si || (di & 1)) {
        o->pending_state = (1 - o->anim_state) & 1;
        if (si) o->vxq = -o->vxq;
    }
}

/* ── Тип 45 — дрейфующая искра (handler cs:0x1692:0x00B4, file 0x181D4)
 *
 * INIT: +0xEB = init_arg (номер прямоугольника), vx = −0.5 + (rand &
 * 0x1FFF)/65536 (т.е. чуть медленнее полупикселя влево), vy = +0.125,
 * tick_period = 7, state_list = DS:0x1D56, аним-состояние 0.
 * THINK: если (rand & 0x1F) < frame_ctr — сбросить счётчик и подтолкнуть
 * vy на случайную величину (rand & 0x1FFF) в Q16.16 вверх или вниз (по
 * биту 0 того же значения); затем отскок от границ прямоугольника, и
 * при отражении по X — переключение аним-состояния 0 ⇄ 1.
 * Очки за смерть 0x100. */
static void ai_type45(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 7, 0);
        obj_phys_begin(o);
        o->st_aux = (int)o->init_arg;          /* +0xEB — номер региона */
        o->vxq = (s32)0xFFFF8000 + (s32)(rng_next() & 0x1FFF);
        o->vyq = QV(0, 0x2000);
        return;
    }
    if ((int)(rng_next() & 0x1F) < o->st_ctr) {
        o->st_ctr = 0;
        s32 si = (s32)(rng_next() & 0x1FFF);
        if (si & 1) o->vyq -= si;
        else        o->vyq += si;
    }
    if (obj_bounce_region(o) & 1)
        o->pending_state = (1 - o->anim_state) & 1;
}

/* ── Тип 43 — «крестовый» летун (handler cs:0x158C:0x0205, file 0x17085)
 *
 * INIT: подфаза (+0xE9) = 0, +0xEB = init_arg, tick_period = 3,
 * state_list = DS:0x1D7E.
 * THINK (si = +0xE9, затем +0xE9 |= 1 — чётная подфаза одноразовая):
 *   при si == 0 случайно выбирается vx = ±0.75;
 *   0/1 — движение «крестом»: если игрок ближе 0x50 и выпал 1 из 16 и
 *         frame_ctr > 20 → подфаза 2 (атака). Смена оси: пока vx ≠ 0 —
 *         через 150 кадров vy = −vx, vx = 0; пока vx == 0 — через 30
 *         кадров vx = vy, vy = 0 (и каждый раз frame_ctr = 0);
 *   2/3 — атака: аним-состояние = сторона + 2, скорость пополам,
 *         на КАДРЕ 1 анимации — выстрел из (x, y+10) вертикально в
 *         игрока со скоростью 0.875; на кадре 0 без смены состояния —
 *         возврат в подфазу 0. Очки за смерть 0x1800. */
static void ai_type43(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, 0);
        obj_phys_begin(o);
        o->st_misc = 0;                       /* +0xE9 */
        o->st_aux  = (int)o->init_arg;        /* +0xEB */
        return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    if (si == 0)
        o->vxq = (rng_next() & 1) ? QV(0, 0xC000) : (s32)0xFFFF4000;

    int di  = obj_bounce_region(o);     /* отскок от границ «выгула» */
    int dir = (o->vxq > 0);
    int dx  = player_units() - obj_units(o);
    int adx = dx < 0 ? -dx : dx;

    switch (si) {
    case 0:
        o->pending_state = dir;
        /* fallthrough */
    case 1:
        if (adx < 0x50 && (rng_next() & 0xF) == 0 && o->st_ctr > 20)
            o->st_misc = 2;
        if (di & 1) o->pending_state = dir;      /* развернулся — сменить кадр */
        if (o->vxq != 0) {
            if (o->st_ctr <= 0x96) break;     /* 150 кадров по горизонтали */
            o->vyq = -o->vxq; o->vxq = 0; o->st_ctr = 0;
        } else {
            if (o->st_ctr <= 0x1E) break;     /* 30 кадров по вертикали */
            o->vxq = o->vyq; o->vyq = 0; o->st_ctr = 0;
        }
        break;
    case 2:
        o->pending_state = dir + 2;
        o->vxq /= 2;
        o->st_ctr = 0;
        /* fallthrough */
    case 3:
        if (o->frame_index == 1) {            /* кадр 1 — выстрел вниз */
            int slot = alloc_obj_slot();
            if (slot >= 0) {
                ai_obj_t *b = &g_obj[slot];
                *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                                 .subtype = 3, .x = o->x, .y = o->y + 10,
                                 .hp = 10, .level_idx = -1, .facing = 1,
                                 .custom = 0 };
                obj_phys_begin(b);
                b->vxq = 0;
                b->vyq = (physics_get_top_y() > o->y) ? QV(0, 0xE000)
                                                      : -QV(0, 0xE000);
            }
        }
        if (o->frame_index == 0 && o->pending_state < 0) o->st_misc = 0;
        break;
    default: break;
    }
}

/* ── Тип 14 — ныряющая птица (handler cs:0x11D6:0x0E0A, file 0x13D6A) ──
 *
 * Преамбула каждого тика: dy = (player_y + h_p) − (self.y + h_s);
 * если |dx| > 30, направление si = (игрок правее), иначе берётся
 * сохранённое в +0xEB.
 * INIT: state_list = DS:0x1958, подфаза (+0xE9) = 2, +0xEB = si,
 * vx = ±1.0, vy = +2.0, y_world = player_y − 20, аним-состояние = si.
 * THINK — две дуги (parabola) через ускорение ±0.25 за тик:
 *   0/1 — «дуга вверх»: vy += −0.25 каждый тик; когда dy >= 40 (птица
 *         высоко над игроком) → подфаза 2, vx = ±1.0, vy = −2.0;
 *   2   — «дуга вниз»: пока dy >= 5 — vy += +0.25; когда dy < 5 (дошла
 *         до уровня игрока) — ВЫСТРЕЛ по горизонтали из (x, y+20) со
 *         скоростью 2.125, затем vy = +2.0, подфаза 0, аним si+2.
 * Аним: 0/1 — полёт влево/вправо (#149/#151), 2/3 — после выстрела
 * (#150/#152). Очки за смерть 0x2600. */
static void ai_type14(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    int dx = player_units() - obj_units(o);
    int adx = dx < 0 ? -dx : dx;
    int si  = (adx > 30) ? (dx > 0) : o->st_aux;      /* +0xEB */
    int dy  = physics_get_top_y() - o->y;

    if (!o->use_anim) {
        o->y = physics_get_top_y() - 20;
        anim_begin(o, 1, si);
        obj_phys_begin(o);
        o->st_misc = 2;                    /* +0xE9 — дуга вниз */
        o->st_aux  = si;                   /* +0xEB — направление */
        o->vxq = si ? Q_1_0 : -Q_1_0;
        o->vyq = Q_2_0;
        return;
    }

    switch (o->st_misc) {
    case 0:
        o->pending_state = o->st_aux;
        /* fallthrough */
    case 1:
        if (dy >= 0x28) {
            o->st_misc = 2;
            o->vxq = o->st_aux ? Q_1_0 : -Q_1_0;
            o->vyq = -Q_2_0;
        }
        o->vyq -= QV(0, 0x4000);           /* −0.25 за тик */
        break;
    case 2:
        if (dy < 5) {
            o->st_aux = si;
            o->vxq = o->st_aux ? Q_1_0 : -Q_1_0;
            o->vyq = Q_2_0;
            o->st_misc = 0;
            o->pending_state = o->st_aux + 2;
            /* Горизонтальный снаряд из (x, y+20), скорость 2.125. */
            spawn_enemy_shot(o->x, o->y + 20, o->st_aux, QV(2, 0x2000), 1);
        } else {
            o->vyq += QV(0, 0x4000);       /* +0.25 за тик */
        }
        break;
    default: break;
    }
}

/* ── Тип 18 — ныряющий чёрт (handler cs:0xFFA:0x127D, file 0x12A1D) ────
 *
 * INIT: si = (игрок правее), aux(+0xEF) = 0, phase(+0xED) = init_arg,
 * phase_ctr(+0xEB) = СНИМОК player.y_world, tick_period = 3,
 * подфаза (+0xE9) = 2, vx = ±1.5, vy = +1.5, y_world = player_y − 20,
 * state_list = DS:0x19B4, аним-состояние = si.
 *
 * THINK (si пересчитывается каждый тик):
 *   tgt = phase ? (player_y − снимок + 40) : 0;  dy = (player_y − tgt) − self.y
 *   0   — подфаза 1, аним si+6
 *   1   — dy >= 40 → подфаза 5, vx = ±1.5, vy = 0, аним si
 *   2   — dy < 0 (или phase и frame_ctr > 30) → vx = ±1.5, vy = 0,
 *         подфаза 4, frame_ctr = 0
 *   4   — атака, если у игрока есть живые снаряды и |dx| > 10, либо
 *         (phase и rand&1): подфаза 0, аним si+4, vx = ±1.5, vy = −1.5;
 *         при phase — ++aux, на 5-м разе phase = 0, и с шансом 1/2
 *         spawn_projectile(self → (self.x ± 8, player_y), скорость 2.0)
 *   5   — |dx| < 10 → подфаза 2, vx = 0, vy = +1.5
 * Аним-состояния: 0/1 полёт, 2/3 запас, 4/5 атака, 6/7 разворот. */
static void ai_type18(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    int si = (player_units() > obj_units(o));
    if (!o->use_anim) {
        o->y = physics_get_top_y() - 20;
        anim_begin(o, 3, si);
        obj_phys_begin(o);
        o->custom   = 0;                        /* +0xEF */
        o->st_phase = (int)o->init_arg;         /* +0xED */
        o->st_aux   = physics_get_top_y();      /* +0xEB — снимок Y игрока */
        o->st_misc  = 2;                        /* +0xE9 — подфаза */
        o->vxq = si ? Q_1_5 : -Q_1_5;
        o->vyq = Q_1_5;
        return;
    }
    int tgt = o->st_phase ? (physics_get_top_y() - o->st_aux + 0x28) : 0;
    int dy  = (physics_get_top_y() - tgt) - o->y;
    int dx  = player_units() - obj_units(o);
    int adx = dx < 0 ? -dx : dx;

    switch (o->st_misc) {
    case 0:
        o->st_misc = 1;
        o->pending_state = si + 6;
        /* fallthrough */
    case 1:
        if (dy < 0x28) break;
        o->st_misc = 5;
        o->vxq = si ? Q_1_5 : -Q_1_5;
        o->vyq = 0;
        o->pending_state = si;
        break;
    case 2:
        if (dy >= 0 && (!o->st_phase || o->st_ctr <= 30)) break;
        o->vxq = si ? Q_1_5 : -Q_1_5;
        o->vyq = 0;
        o->st_misc = 4;
        o->st_ctr  = 0;
        break;
    case 4:
        if (!((player_shots_alive() != 0 && adx > 10) ||
              (o->st_phase && (rng_next() & 1))))
            break;
        o->st_misc = 0;
        o->pending_state = si + 4;
        o->vxq = si ? Q_1_5 : -Q_1_5;
        o->vyq = -Q_1_5;
        if (o->st_phase) {
            if (++o->custom > 4) o->st_phase = 0;
            if (rng_next() & 1) {               /* сброс снаряда в игрока */
                int sx = o->x + ((rng_next() & 1) ? 16 : -16);
                int sy = physics_get_top_y();
                int slot = alloc_obj_slot();
                if (slot >= 0) {
                    ai_obj_t *b = &g_obj[slot];
                    *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                                     .subtype = 3, .x = o->x, .y = o->y,
                                     .hp = 10, .level_idx = -1,
                                     .facing = si ? 1 : -1, .custom = 2 };
                    obj_phys_begin(b);
                    /* Единичный вектор к цели со скоростью 2.0 ед./кадр. */
                    int vx = (sx - o->x) / 2, vy = sy - o->y;
                    int len = (vx < 0 ? -vx : vx) + (vy < 0 ? -vy : vy);
                    if (len < 1) len = 1;
                    b->vxq = ((s32)vx << 17) / len;
                    b->vyq = ((s32)vy << 17) / len;
                }
            }
        }
        break;
    case 5:
        if (adx >= 10) break;
        o->st_misc = 2;
        o->vxq = 0;
        o->vyq = Q_1_5;
        break;
    default: break;
    }
}

/* Поставить объект НОГАМИ на ближайшую поверхность снизу — порт
 * place_on_ground_core (cs:0xD4B:0x14C): ищет первую строку карты с
 * подходящим тайлом и пишет y_world = row*10 − высота спрайта. */
static void obj_snap_ground(ai_obj_t *o)
{
    int h   = obj_height(o);
    int row = level_ground_row_below(o->x + 6, o->y / TILE_H, 0x0001);
    if (row >= 0) {
        o->y = row * TILE_H - h;
        if (o->phys) o->yq = (s32)o->y << 16;
    }
}

/* ── Тип 26 — прыгающий воин (handler cs:0x1348:0x0E46, file 0x15AC6) ──
 *
 * INIT: tick_period = 2, state_list = DS:0x1AA4, подфаза (+0xE9) = 2
 * («приземление»), направление (+0xED) = игрок правее, vx = ±0.75.
 *
 * THINK каждый тик:
 *   гистерезис разворота: смотрит вправо и (−10 − rand&7) > dx → влево;
 *   смотрит влево и (10 + rand&7) < dx → вправо; затем ЦЕЛАЯ часть vx
 *   переписывается на ±1.0 (дробная сохраняется);
 *   подфазы (si = +0xE9, потом +0xE9 |= 1 — чётная срабатывает один раз):
 *     0/1 — включить гравитацию ay = 0.3125 и ждать vy > 0; тайл под
 *           следующей позицией (хелпер cs:0x1348:0xE0B) категории 0/4 →
 *           подфаза 2 (посадка), категории 6 → подфаза 4 (склон);
 *     2/3 — посадка: vy = 0, ay = 0, встать на землю; затем 2 из 16 —
 *           прыжок vy = −2.0 и снова подфаза 0, иначе по категории тайла
 *           впереди: 2 → подфаза 0 (шагнуть в пустоту), 6 → подфаза 4;
 *     4/5 — склон: vy = 16 / (vx/10), ay = 0, встать на землю.
 *   аним-состояние = ((+0xE9 & 0xFE) ? 0 : 2) + направление
 *   (0/1 — ходьба влево/вправо, 2/3 — прыжок), очки за смерть 0x400. */
static int t26_tile_ahead(const ai_obj_t *o)
{
    /* cs:0x1348:0xE0B — категория тайла под НОГАМИ в следующей позиции:
     * (x + vx_int + 3, y + vy_int + sprite_h + 1). */
    int nx = o->x + ((int)(o->vxq >> 16)) * 2 + 6;
    int ny = o->y + (int)(o->vyq >> 16) + obj_height(o) + 1;
    int a  = level_tile_attr_at(nx, ny);
    return (a < 0) ? 2 : a;                  /* вне карты — «пусто» */
}

static void ai_type26(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 2, 0);
        obj_phys_begin(o);
        o->st_misc  = 2;                                   /* +0xE9 подфаза */
        o->st_phase = (player_units() > obj_units(o));      /* +0xED вправо? */
        o->vxq = o->st_phase ? QV(0, 0xC000) : (s32)0xFFFF4000;   /* ±0.75 */
        return;
    }

    int pdx = player_units() - obj_units(o);
    if (o->st_phase) { if (-10 - (int)(rng_next() & 7) > pdx) o->st_phase = 0; }
    else             { if ( 10 + (int)(rng_next() & 7) < pdx) o->st_phase = 1; }
    /* Пишется только ЦЕЛАЯ часть vx (как `mov [+0x10], ±1`). */
    o->vxq = ((s32)(o->st_phase ? 1 : -1) << 16) | (o->vxq & 0xFFFF);

    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->ayq = QV(0, 0x5000);                 /* гравитация 0.3125 */
        /* fallthrough */
    case 1:
        if (o->vyq <= 0) break;
        { int cat = t26_tile_ahead(o);
          if (cat == 0 || cat == 4) o->st_misc = 2;
          else if (cat == 6)        o->st_misc = 4; }
        break;
    case 2:
        o->vyq = 0; o->ayq = 0;
        obj_snap_ground(o);
        /* fallthrough */
    case 3:
        if ((rng_next() & 0xF) < 2) {           /* 2 из 16 — подпрыгнуть */
            o->vyq = -QV(2, 0);
            o->st_misc = 0;
        } else {
            int cat = t26_tile_ahead(o);
            if (cat == 2)      o->st_misc = 0;  /* впереди пусто — падать */
            else if (cat == 6) o->st_misc = 4;  /* склон */
        }
        break;
    case 4: {
        s32 vx = o->vxq / 10;
        o->vyq = vx ? (s32)((QV(16, 0)) / vx) : 0;
        o->ayq = 0;
        obj_snap_ground(o);
    }
        /* fallthrough */
    case 5: {
        int cat = t26_tile_ahead(o);
        if (cat == 0 || cat == 4 || cat == 7) o->st_misc = 2;
        else if (cat == 2)                    o->st_misc = 0;
        else if (cat == 6)                    o->st_misc = 4;
        break;
    }
    default: break;
    }
    o->pending_state = ((o->st_misc & 0xFE) ? 0 : 2) + (o->st_phase ? 1 : 0);
}

/* ── Тип 25 — PRNG-lurker (handler cs:0x1348:0xA20, file 0x156A0) ──────
 * INIT: tick_period=4, set_motion(4) → vy = +2.0 px/кадр.
 * TICK: когда счётчик КАДРОВ превысил 30 + (rand&15), выбирается новое
 * направление — по одной оси к игроку, по другой случайно из таблицы
 * пресетов DS:0x1348 — и счётчик сбрасывается. */
static void ai_type25(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 0);
        obj_phys_begin(o);
        o->vyq = Q_2_0;
        return;
    }
    int thr = 30 + (int)(rng_next() & 15);
    if (thr < o->st_ctr) {
        static const int HT[4] = { 1, 2, 0, 0 };
        int hcode, vcode;
        if (rng_next() & 1) {
            hcode = HT[rng_next() & 3];
            vcode = (physics_get_top_y() > o->y) ? 4 : 8;
        } else {
            hcode = (player_units() > obj_units(o)) ? 1 : 2;
            vcode = HT[rng_next() & 3] * 4;
        }
        /* Таблица пресетов DS:0x1348: 1/2 = ±2.0 по X, 4/8 = ±2.0 по Y,
         * комбинации 5/6/9/10 = ±1.5 по обеим осям. */
        static const s32 VX[3] = { 0, Q_2_0, -Q_2_0 };
        static const s32 VY[3] = { 0, Q_2_0, -Q_2_0 };
        int hi = hcode, vi = vcode / 4;
        if (hi && vi) { o->vxq = (hi == 1 ? Q_1_5 : -Q_1_5);
                        o->vyq = (vi == 1 ? Q_1_5 : -Q_1_5); }
        else          { o->vxq = VX[hi]; o->vyq = VY[vi]; }
        o->st_ctr = 0;
    }
    o->pending_state = (o->vxq > 0) ? 1 : 0;
}

/* ── Тип 27 — падающий сверху (handler cs:0x1348:0x17AB, file 0x1642B) ─
 * state 0 ставит y = player_y − 70 (в порту это делает спавн). INIT:
 * tick_period=3, aux = −(init_arg*15) — ступенчатая задержка для стаи,
 * phase = init_arg > 2. TICK: цикл «8 тиков по диагонали вниз» ⇄
 * «8 тиков горизонтально назад», despawn ниже нижней границы региона. */
static void ai_type27(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, 0);
        obj_phys_begin(o);
        o->y = physics_get_top_y() - 70;
        obj_phys_begin(o);
        o->st_misc  = 0;
        o->st_aux   = -((int)o->init_arg * 15);
        o->st_phase = (o->init_arg > 2);
        return;
    }
    if (level_num_map_rows && o->y > (int)level_num_map_rows * TILE_H) {
        o->alive = false; return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    ++o->st_aux;
    switch (si) {
    case 0:
        o->pending_state = 0; o->vxq = o->vyq = 0;
        /* fallthrough */
    case 1:
        if (o->st_aux >= 0) o->st_misc = 2;
        break;
    case 2:
        o->pending_state = o->st_phase + 2;
        o->vxq = o->st_phase ? Q_1_0 : -Q_1_0;
        o->vyq = Q_1_5;
        /* fallthrough */
    case 3:
        if (o->st_aux >= 8) { o->st_misc = 4; o->st_aux = 0; }
        break;
    case 4:
        o->pending_state = o->st_phase + 4;
        o->vxq = o->st_phase ? -Q_1_0 : Q_1_0;
        o->vyq = 0;
        /* fallthrough */
    case 5:
        if (o->st_aux >= 8) { o->st_misc = 2; o->st_aux = 0; }
        break;
    default: break;
    }
}

/* ── Тип 28 — наземный стрелок (handler cs:0x1348:0x197C, file 0x165FC) ─
 * INIT: tick_period=4, направление к игроку, vx = ±0.5 ед./кадр.
 * TICK: разворот с гистерезисом 10..17 ед., один горизонтальный
 * огненный шар (2.0 ед./кадр) после 75 кадров жизни — однократно. */
static void ai_type28(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, (player_units() > obj_units(o)) ? 1 : 0);
        obj_phys_begin(o);
        o->st_phase = (player_units() > obj_units(o));
        o->st_misc = 0;                       /* «уже стрелял» */
        return;
    }
    int sdx = player_units() - obj_units(o);
    if (o->st_phase) { if (-10 - (int)(rng_next() & 7) > sdx) o->st_phase = 0; }
    else             { if ((int)(rng_next() & 7) + 10 < sdx) o->st_phase = 1; }
    o->vxq = o->st_phase ? Q_0_5 : -Q_0_5;
    if (!o->st_misc && o->st_ctr > 75) {
        spawn_enemy_shot(o->x, o->y + 7, o->st_phase, Q_2_0, 5);
        o->st_misc = 1;
    }
    o->pending_state = o->st_phase;
}

/* ── Тип 48 — турель (handler cs:0x170C:0x18C, file 0x18A4C) ───────────
 * Неподвижна. INIT: tick_period=4, стартовый кадр анимации из rand.
 * TICK: фаза 0 — взвести таймер (rand & 0x5F), фаза 1 — ждать 250
 * кадров (сброс при переходе игрока на другую сторону), фаза 2 —
 * горизонтальный шар 1.5 ед./кадр и снова в фазу 0. */
static void ai_type48(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 0);
        u16 r = (u16)(rng_next() & 3);
        o->frame_index = (u8)(r <= 2 ? 2 : (rng_next() & 3));
        o->st_misc = 0;
        return;
    }
    int sdx = player_units() - obj_units(o);
    int facing = sdx > 0;
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = facing;
        o->st_ctr = (int)(rng_next() & 0x5F);
        /* fallthrough */
    case 1:
        if (o->anim_state != (u8)facing) { o->st_misc = 0; break; }
        if (o->st_ctr > 250) o->st_misc = 2;
        break;
    default:
        spawn_enemy_shot(o->x, o->y - 5, facing, Q_1_5, 1);
        o->st_misc = 0;
        break;
    }
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

    int kx = physics_get_left_x();
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
        if (o->type == 22 && !level_solid_at(nx + 4 * o->custom, obj_feet(o) + 1))
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
            int ky = physics_get_top_y();
            int feet = obj_feet(o);
            if (ky < feet - 2)      o->y -= 1;
            else if (ky > feet + 2) o->y += 1;
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
    int kx = physics_get_left_x(), ky = physics_get_top_y();
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
    int px = physics_get_left_x() / 2;
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
        /* Тип 4 (handler cs:0xFFA:0x0CF6, file 0x12496) — предмет,
         * который виден ТОЛЬКО пока идёт таймер Glasses, и при этом
         * мигает по биту 3 счётчика кадров:
         *     visible = ([0xD8B8] != 0) && ([0xCDEA] & 8)
         *     box0.sprite = visible ? 0x27 + item_id : 0   (0 = бокс выкл.)
         *     box0.hitflag = visible                        (иначе не берётся)
         * Подбор (state 4) — play_sound(5) + option_pickup(item_id). */
        o->invisible = !(inv_glasses_active() && ((frame >> 3) & 1));
        return;
    }
    /* Тип 6 (handler cs:0xFFA:0x0EB8, file 0x12658). INIT: item_id =
     * (init_arg == 99) ? rand()%11 : init_arg, tick_period = 100;
     * item_id >= 11 — СКРЫТЫЙ предмет: num_bboxes = 0, phase(+0xED) = 1.
     * THINK скрытого: расстояние до игрока d = |dx| + |dy| (хелпер
     * cs:0xD4B:0x105); d < 40 → показать (aux = 1), d > 40 → снова
     * спрятать. Обычный предмет (phase = 0) в THINK ничего не делает. */
    if (o->init_arg >= 11 && o->init_arg != 99) {   /* СКРЫТЫЙ предмет */
        int d  = player_units() - obj_units(o);
        int dy = physics_get_top_y() - o->y;
        if (d < 0)  d = -d;
        if (dy < 0) dy = -dy;
        int dist = d + dy;
        if (!o->invisible) { if (dist > 0x28) o->invisible = true;  }
        else               { if (dist < 0x28) o->invisible = false; }
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


/* ─── Тип 24 — «ЗОЛОТОЙ ЧЕЛОВЕК» и его шары ────────────────────────────
 *
 * Декомпилировано целиком (2026-08-15): человек — handler cs:0x1348:0x3A9
 * (file 0x15029), шар — cs:0x1348:0x0D7 (file 0x14D57), спавн ребёнка —
 * cs:0x1348:0x2D7 (file 0x14F57), таблица смещений — DS:0x19F4.
 *
 * Это НЕ «спаунер стаи»: объект живёт бесконечным циклом
 *   человек (#187 ↔ #188, жёлтый/белый — мерцание) 38 тиков
 *     → рассыпается на 7 шаров (#185/#186), на его месте остаётся
 *       КОЛЬЦО из точек (спрайт #6, аним-состояние 0)
 *     → шары за 10 тиков разлетаются в полукруг радиусом 50 ед.
 *       (DS:0x19F4 — 7 точек через 30°), висят 15 тиков,
 *       за 10 тиков слетаются обратно в центр и гаснут
 *     → пока шаров нет — человек собирается снова.
 * Между рассыпанием и сбором кольцо ещё и ИДЁТ к игроку (±2.0 ед./кадр,
 * 15 тиков), останавливаясь на краю площадки (тайл категории 2 под ним).
 *
 * Прежняя реализация («спаунер 7 зелёных орлов, слот сразу освобождается»)
 * была выдумкой: отсюда жалоба «появляется и мгновенно рассыпается». */

/* Живых шаров — глобальный счётчик [DS:0xD97C]: человек ждёт, пока он
 * обнулится, и только тогда собирается. */
static int g_gold_balls = 0;

/* DS:0x19F4 — 7 точек полукруга R=50 (dx в единицах движка, dy в строках). */
static const s16 GOLD_RING[7][2] = {
    { -50,   0 }, { -43, -25 }, { -25, -43 }, { 0, -50 },
    {  25, -43 }, {  43, -25 }, {  50,   0 },
};

/* Центр человека: x + width(#187)/2, y + height(#187)/2 — оригинал берёт
 * полуметрики спрайта 0xBB из таблиц DS:0x0D08/0x0FAC. */
static void gold_man_center(const ai_obj_t *m, int *cx, int *cy)
{
    const sprite_t *s = sprite_find(187);
    *cx = m->x + (s ? (int)s->art_w / 2 : 16);
    *cy = m->y + (s ? (int)s->h    / 2 : 17);
}

/* Скорость «долететь из (x0,y0) в (x1,y1) ровно за n тиков» — порт
 * cs:0x11D6:0x00DD: vx = (x1−x0)/n, vy = (y1−y0)/n в Q16.16. */
static void obj_aim(ai_obj_t *o, int from_x, int from_y, int to_x, int to_y, int n)
{
    o->vxq = (((s32)(to_x / 2 - from_x / 2)) << 16) / n;
    o->vyq = (((s32)(to_y - from_y)) << 16) / n;
}

static void gold_spawn_ball(int man_slot, int idx, int kind, u16 frame)
{
    int slot = alloc_obj_slot();
    if (slot < 0) return;
    ai_obj_t *m = &g_obj[man_slot];
    int cx, cy;
    gold_man_center(m, &cx, &cy);
    ai_obj_t *b = &g_obj[slot];
    *b = (ai_obj_t){
        .alive = true, .type = TYPE_GOLD_BALL, .subtype = 1, .hp = 10,
        .level_idx = -1, .facing = 1, .spawn_frame = frame,
        .x = cx, .y = cy,
        .custom = idx,          /* номер точки на окружности (+0xED) */
        .st_phase = kind,       /* стартовая фаза (+0xE9): 4 = сразу сбор */
        .st_aux = man_slot,     /* родитель (+0xF1) */
    };
    if (kind == 4) {            /* kind 4 — сразу в точке круга */
        b->x += GOLD_RING[idx][0] * 2;
        b->y += GOLD_RING[idx][1];
    }
    obj_phys_begin(b);
    ++g_gold_balls;             /* ++[0xD97C] в INIT ребёнка */
}

static void ai_gold_ball(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)frame; (void)karnov_input;
    ai_obj_t *p = (o->st_aux >= 0 && o->st_aux < g_num) ? &g_obj[o->st_aux] : NULL;
    if (!p || !p->alive || p->type != 24) {      /* человека убили — гаснем */
        o->alive = false; if (g_gold_balls > 0) --g_gold_balls;
        return;
    }
    obj_integrate(o);
    o->anim_phase ^= 1;                          /* #185 ↔ #186 каждый кадр */

    ++o->st_misc;                                /* +0xEB */
    int si = o->st_phase;                        /* +0xE9 */
    o->st_phase |= 1;                            /* чётная фаза — одноразовый вход */
    if (!(si & 1)) o->st_misc = 0;

    int cx, cy;
    gold_man_center(p, &cx, &cy);
    switch (si) {
    case 0:                                      /* разлёт к своей точке круга */
        obj_aim(o, o->x, o->y,
                cx + GOLD_RING[o->custom][0] * 2, cy + GOLD_RING[o->custom][1], 10);
        o->invisible = false;
        /* оригинал проваливается в проверку фазы 1 */
        /* fallthrough */
    case 1:
        if (o->st_misc >= 10) o->st_phase = 2;
        break;
    case 2:                                      /* set_motion(0) + спрятаться */
        o->vxq = o->vyq = 0;
        o->invisible = true;
        /* fallthrough */
    case 3:
        if (o->st_misc >= 15) o->st_phase = 4;
        break;
    case 4:                                      /* сбор обратно в человека */
        obj_aim(o, o->x, o->y, cx, cy, 10);
        o->invisible = false;
        /* fallthrough */
    case 5:
        if (o->st_misc >= 10) {                  /* state 6: --[0xD97C], удалить */
            o->alive = false;
            if (g_gold_balls > 0) --g_gold_balls;
        }
        break;
    default: break;
    }
}

static void ai_gold_man(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    int slot = (int)(o - g_obj);
    if (!o->use_anim) {                          /* state 1 — INIT */
        anim_begin(o, 1, 0);                     /* +0xC9 не задан → 1 кадр/тик */
        obj_phys_begin(o);
        g_gold_balls = 0;                        /* [0xD97C] = 0 */
        for (int i = 0; i < 7; ++i) gold_spawn_ball(slot, i, 4, frame);
        o->st_misc  = 4;                         /* +0xE9 = 4: ждать сбора шаров */
        o->st_aux   = 0;                         /* +0xEB */
        o->hp       = 0x5A;                      /* 90 */
        return;
    }

    ++o->st_aux;                                 /* +0xEB — тики в фазе */
    int si = o->st_misc;                         /* +0xE9 — фаза */
    o->st_misc |= 1;
    if (!(si & 1)) {
        o->st_aux = 0;
        /* +0x2E — флаг КОЛЛИЗИИ bbox (не видимости): человек уязвим
         * только собранным. Кольцо и разлёт неуязвимы. */
        o->custom = (si == 6);
    }
    switch (si) {
    case 0:
        o->pending_state = 0;                    /* кольцо из точек (#6) */
        /* fallthrough */
    case 1:
        if (o->st_aux >= 10) o->st_misc = 2;
        break;
    case 2:                                      /* идти к игроку ±2.0 ед./кадр */
        o->vxq = (player_units() > obj_units(o)) ? Q_2_0 : -Q_2_0;
        /* fallthrough */
    case 3:
        if (o->st_aux >= 15) o->st_misc = 4;
        /* Край площадки: тайл под нижней кромкой категории 2 → стоп. */
        if (level_tile_attr_at(o->x, o->y + obj_height(o) + 2) == 2)
            o->vxq = 0;
        break;
    case 4:
        o->vxq = o->vyq = 0;                     /* set_motion(0) */
        /* fallthrough */
    case 5:
        if (g_gold_balls == 0 && o->st_aux > 2) o->st_misc = 6;
        break;
    case 6:
        o->pending_state = 1;                    /* сам человек: #187 ↔ #188 */
        /* fallthrough */
    case 7:
        if (o->st_aux >= 38) {                   /* пожил собранным — снова врозь */
            o->custom = 0;
            o->hp = 0x5A;
            for (int i = 0; i < 7; ++i) gold_spawn_ball(slot, i, 0, frame);
            o->st_misc = 0;
        }
        break;
    default: break;
    }
}

/* ─── Многосегментные объекты (типы 21/33/34/39/50) ────────────────────
 *
 * Раскладки спрайт-боксов взяты ПРЯМО ИЗ INIT'ов KARNOV.EXE (дизасм
 * 2026-08-14), а не из C++-реконструкции: та описывала эти типы как
 * «цепочки» с горизонтальным шагом, чего в бинарнике нет.
 *
 * Единицы: dx оригинала — в единицах движка (1 ед. = 2 px), поэтому в
 * таблицах ниже он уже удвоен; dy — строки развёртки, 1:1. Проверка
 * сходимости: у type 21 шаг колонок 64 ед. = 128 px = ровно ширина
 * спрайта 180, шаг строк 30 = его высота (бесшовная мозаика 2×3); у
 * type 50 шаг 24 ед. = 48 px = ширина спрайта 308; у type 39 шаг
 * 8 ед. = 16 px = ширина спрайта 259.
 *
 * TICK этих типов в оригинале богаче (у 33 — «схлопывание» сегментов,
 * у 50 — триггер по позиции игрока, у 34 — ожидание игрока в 100 px),
 * и пока не портирован: объекты статичны. Прежняя «волна» сегментов
 * была изобретением порта и к тому же интегрировала собственное dy,
 * из-за чего тело уползало за экран.
 */
typedef struct { u16 sprite; s16 dx, dy; } part_def_t;

/* type 21 (INIT file 0x15DFD): голова 178 (+2,−3) + мозаика 2×3 из 180.
 * Внешний цикл X = 16, 80 (шаг 64, пока < 144); внутренний Y = −30, 0, +30. */
static const part_def_t PARTS_21[] = {
    {178,   +4,  -3},
    {180,   32, -30}, {180,  32,   0}, {180,  32, +30},
    {180,  160, -30}, {180, 160,   0}, {180, 160, +30},
};
/* type 33 (INIT file 0x16A97): вертикальный стек ВВЕРХ. Голова 230
 * (dx=−4), тело 231 (dx=−2); dy накапливается как −Σ sprite_h + 2,
 * где h[230] = h[231] = 10 (таблица DS:0x0FAC). */
static const part_def_t PARTS_33[] = {
    {230, -8,  -8}, {231, -4, -18}, {231, -4, -28}, {231, -4, -38},
    {231, -4, -48}, {231, -4, -58}, {231, -4, -68},
};
/* type 34 (phase 0 file 0x16CDB): слои sprite = 0xEE − i (238…233),
 * dx = 0, dy из таблицы DS:0x1C14 = {1,1,1,1,2,0}. */
static const part_def_t PARTS_34[] = {
    {238, 0, 1}, {237, 0, 1}, {236, 0, 1},
    {235, 0, 1}, {234, 0, 2}, {233, 0, 0},
};
/* type 39 (INIT file 0x17A18): ряд из 6 спрайтов 0x103 (259), шаг
 * si*8 ед. = 16 px, dy = 0. TICK крутит кадры 259..262. */
static const part_def_t PARTS_39[] = {
    {259,  0, 0}, {259, 16, 0}, {259, 32, 0},
    {259, 48, 0}, {259, 64, 0}, {259, 80, 0},
};
/* type 50 (INIT file 0x18E19): голова 0x132 (306) при dx=−4, dy=0 +
 * 6 тел 0x134 (308), x = 32..152 шаг 24 (ед.), y = −10. */
static const part_def_t PARTS_50[] = {
    {306,  -8,   0},
    {308,  64, -10}, {308, 112, -10}, {308, 160, -10},
    {308, 208, -10}, {308, 256, -10}, {308, 304, -10},
};

static void parts_apply(ai_obj_t *o, const part_def_t *defs, int n)
{
    if (n > MAX_PARTS) n = MAX_PARTS;
    o->num_parts = n;
    for (int i = 0; i < n; ++i) {
        o->parts[i].sprite = defs[i].sprite;
        o->parts[i].dx     = defs[i].dx;
        o->parts[i].dy     = defs[i].dy;
    }
    /* dx задан для facing-right; flip в рендере зеркалит смещения. */
    o->facing = 1;
}

static void ai_multipart(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->num_parts == 0) {
        switch (o->type) {
        case 21: parts_apply(o, PARTS_21, 7); break;
        case 33: parts_apply(o, PARTS_33, 7); break;
        case 34: parts_apply(o, PARTS_34, 6); break;
        case 39: parts_apply(o, PARTS_39, 6); break;
        case 50: parts_apply(o, PARTS_50, 7); break;
        default: parts_apply(o, PARTS_33, 4); break;
        }
    }
    /* Единственная портированная TICK-логика: у type 39 каждый бокс
     * прокручивает кадр в диапазоне 0x103..0x106 (file 0x17ABB). */
    if (o->type == 39 && (frame & 3) == 0) {
        for (int i = 0; i < o->num_parts; ++i) {
            u16 f = (u16)((o->parts[i].sprite - 259 + 1) & 3);
            o->parts[i].sprite = (u16)(259 + f);
        }
    }
    o->anim_phase = (frame >> 3) & 1;
}



/* Type 30 — растущая цепь (ai_type30): num_bboxes начинается с 0 и растёт
 * во времени; спрайты чередуются 0x05 / 0x98 (152). */
static void ai_grow_chain(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    /* INIT file 0x15FB5: спрайт КОНСТАНТНЫЙ 0xDA (218); чередуются
     * x_offset 152 и 5 (единицы → 304 и 10 px), dy = (i>>1)*20.
     * C++-реконструкция принимала 0x98/0x05 за sprite_id — отсюда была
     * «цепь из 152/5». TICK (file 0x1608A): +2 бокса каждые 11 тиков. */
    if (o->num_parts < 6 && ++o->custom > 10) {
        o->custom = 0;
        for (int k = 0; k < 2 && o->num_parts < 6; ++k) {
            int i = o->num_parts;
            o->parts[i].sprite = 218;
            o->parts[i].dx = (s16)((i & 1) ? 304 : 10);
            o->parts[i].dy = (s16)((i >> 1) * 20);
            ++o->num_parts;
        }
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
        /* INIT file 0x16FFF/0x17017: голова 0xF5 при dx=+2 ед. (=+4 px),
         * хвост 0xF9 при dx=0 и dy = sprite_h[0xF5] = 20 — т.е. РОВНО
         * ПОД головой (file 0x17058), а не слева, как было в порту. */
        o->parts[0].sprite = 245; o->parts[0].dx = 4; o->parts[0].dy = 0;
        o->parts[1].sprite = 249; o->parts[1].dx = 0; o->parts[1].dy = 20;
    }
    int kx = physics_get_left_x();
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
                .x = o->x, .y = o->y + 8,
                .facing = o->facing, .hp = 1,
                .level_idx = -1, .spawn_frame = frame,
            };
        }
    }
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
    int kx = physics_get_left_x(), ky = physics_get_top_y();
    o->facing = (kx >= o->x) ? 1 : -1;
    if ((frame & 1) == 0) {
        o->x += 2 * o->facing;
        int feet = obj_feet(o);
        if (ky < feet - 2)      o->y -= 1;
        else if (ky > feet + 2) o->y += 1;
    }
}

/* Type 51 — статический элемент арены (ai_type51): 8 частей, sprite 0x135
 * (309), y выровнен по 10, sub_timer_max=0xFF (вечный). */
static void ai_arena_static(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)frame; (void)karnov_input;
    if (o->num_parts == 0) {
        /* INIT (file 0x192A0): num_bboxes=8, но заполняется только
         * bbox[0] = спрайт 0x135 (309) в (0,0), плюс y выравнивается
         * по 10. Настоящее 8-сегментное тело арены (столб из спрайтов
         * 0x13D=317 с шагом 10) в оригинале создаёт ОТДЕЛЬНЫЙ объект,
         * поднимаемый в state 4 через queue_state(cs:175E:0x3A9) — его
         * нет в таблице DS:0x10FE и он пока не портирован. Прежняя
         * сетка 4×2 из 309 была изобретением порта. */
        o->y -= o->y % 10;
        o->num_parts = 1;
        o->parts[0].sprite = 309;
        o->parts[0].dx = 0;
        o->parts[0].dy = 0;
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
    [10] = ai_type10,          /* демон на облаке — маятник */
    [11] = ai_type11,
    [12] = ai_type12,          /* скачущий валун */
    [13] = ai_type13,   /* Y-snap метатель */
    [14] = ai_type14,          /* ныряющая птица (босс L1) */
    [15] = ai_type15,          /* каменный страж */
    [16] = ai_type16,
    [17] = ai_type17,   /* скелет на страусе */
    [18] = ai_type18,          /* ныряющий чёрт */
    [19] = ai_slow_helper,
    [20] = ai_head_column,     /* колонна голов LEVEL2 (multi-bbox, статична) */
    [21] = ai_multipart,            /* 7-сегментная цепь */
    [22] = ai_type22,          /* статичный страж */
    [23] = ai_type23,          /* летящий шар #185 */
    [24] = ai_gold_man,        /* «золотой человек» + 7 шаров */
    [25] = ai_type25,
    [26] = ai_type26,          /* прыгающий воин */
    [27] = ai_type27,
    [28] = ai_type28,
    [29] = ai_simple,          /* multi-phase хелпер */
    [30] = ai_grow_chain,
    [31] = ai_type31,          /* лев */
    [32] = ai_type32,          /* босс LEVEL2 */
    [33] = ai_multipart,
    [34] = ai_multipart,            /* червь с HP-индикатором */
    [35] = ai_type35,          /* прыгающий голем */
    [36] = ai_ryu,             /* ★ Рю */
    [37] = ai_simple,          /* slow boss */
    [38] = ai_type38,          /* босс LEVEL3 */
    [39] = ai_multipart,
    [40] = ai_type40,          /* босс LEVEL4 */
    [41] = ai_stub,            /* mid-boss HP=100 — в данных уровней не встречается */
    [42] = ai_type42,          /* периодическая ловушка */
    [43] = ai_type43,          /* «крестовый» летун */
    [44] = ai_type44,          /* дрейфующий летун */
    [45] = ai_type45,          /* дрейфующая искра */
    [46] = ai_simple,
    [47] = ai_type47,          /* турель */
    [48] = ai_type48,
    [49] = ai_ryu_helper,
    [50] = ai_multipart,
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
        case TYPE_GOLD_BALL:       return ai_gold_ball;
        case TYPE_TRAP_SEGMENT:    return ai_trap_segment;
        case TYPE_BOMB_BLAST:      return ai_bomb_blast;
        case TYPE_ENEMY_SHOT:      return ai_enemy_shot;
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
        if (arg == 99)      item_id = (u16)(rng_next() % (u16)ITEM_NUM);   /* file 0x12477: rand() % 11 */
        else if (arg >= 11) { item_id = (u16)(arg % 11u); hidden_item = true; }  /* см. ниже: custom = arg */
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
    g_obj[slot].cur_h = obj_height(&g_obj[slot]);

    /* Постановка на землю в INIT. Дизассемблирование показало 14 типов,
     * чей INIT зовёт хелпер cs:0xD4B:0x19F (file 0xEE4F):
     *     snap_to_ground(obj, base_sprite, attr_category = 0)
     * → сканирует строки вниз от row(y_world)−1 до первой «половой» и
     * ставит y_world = row*10 − sprite_h, т.е. НОГАМИ на её кромку.
     * Поэтому у этих типов запись хранит линию земли (напр. LEVEL1 type 11:
     * spawn_y = 290 = поверхность песка), а не верх спрайта. Без снапа они
     * рисовались бы закопанными по макушку. Тип 8 из списка исключён:
     * это sky-спаунер, в порту он не материализуется. */
    place_mode_t pm = type_place_mode(e->type);
    if (pm == PLACE_LIFT || pm == PLACE_ALIGN10 || pm == PLACE_PLAYER_Y) {
        if (pm == PLACE_LIFT)            g_obj[slot].y -= type_place_lift(e->type);
        else if (pm == PLACE_ALIGN10)    g_obj[slot].y -= g_obj[slot].y % TILE_H;
        else                             g_obj[slot].y = physics_get_top_y() - 22;
        g_obj[slot].home_y = g_obj[slot].y;
        g_obj[slot].spawn_y = (u16)g_obj[slot].y;
    } else if (pm != PLACE_NONE) {
        /* Спрайт для метрик берём из аним-таблиц EXE (тот же кадр, что
         * даст obj_height), иначе колонка пробы и высота считаются по
         * РАЗНЫМ спрайтам и объект встаёт не на ту строку. */
        u16 base_sid = 0xFFFF;
        if (type < 58 && ANIM_LISTS[type].count && ANIM_LISTS[type].states[0].n)
            base_sid = ANIM_LISTS[type].states[0].spr[0];
        if (base_sid == 0xFFFF) base_sid = sprite_for_type(type, 0);
        const sprite_t *bs = sprite_find(base_sid);
        int probe_x = (pm == PLACE_SNAP_CENTER && bs) ? sx + bs->art_w / 2 : sx;
        u16 mask = (pm == PLACE_SNAP_CENTER) ? 0x0089 : 0x0001;
        int row0 = sy / TILE_H - 1;
        int r = level_ground_row_below(probe_x, row0 < 0 ? 0 : row0, mask);
        if (r >= 0) {
            int ny = r * TILE_H - g_obj[slot].cur_h;
            g_obj[slot].y = ny;
            g_obj[slot].home_y = ny;
            g_obj[slot].spawn_y = (u16)ny;
        }
    }
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

    g_camera_left = camera_left_px;
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
        /* В лог идёт позиция ИЗ ЗАПИСИ (до постановки на землю в INIT) —
         * оракул проверяет алгоритм триггеров, а не визуальную укладку. */
        if (spawn_log())
            fprintf(spawn_log(), "S %u %d %u %u %d %d %d %d\n",
                    frame, si, r->type, r->subtype,
                    (int)r->spawn_x * 2, (int)r->spawn_y, g_obj[slot].hp,
                    g_obj[slot].is_boss ? 1 : 0);
        if (dbg)
            fprintf(stderr, "spawn[%d]: f=%u type=%u sub=%u at=(%d,%d) hp=%d%s\n",
                    si, frame, r->type, r->subtype,
                    g_obj[slot].x, g_obj[slot].y, g_obj[slot].hp,
                    g_obj[slot].is_boss ? " GOAL" : "");
    }
}

/* ─── Off-screen despawn (THINKCYCLE фаза B, file 0xFD6A..0xFD98) ──────
 *
 * Оригинал обходит активный список ДВУМЯ проходами (внешний si = 0,1;
 * внутренний di = si с шагом 2), и блок деспавна стоит под `or si,si;
 * jz` — т.е. выполняется только во втором проходе, для объектов с
 * НЕЧЁТНЫМ индексом слота. Индекс выдаёт alloc_object(parity)
 * (file 0x1FFB) с parity = subtype & 1 из spawn_active_object, поэтому
 * критерий эквивалентен «нечётный subtype»: враги (1/7), multi-bbox (9)
 * и предметы (5); Karnov-маркеры (0) и спец (4) не деспавнятся.
 *
 * Само условие: объект не является goal-объектом ([DS:0xD959]) и
 * player_x − obj.x_world > 0x7D (125 ед. = 250 px) → state_byte = 6
 * (умирание) и collision_cnt = 0. В порту state-машины объектов нет,
 * поэтому эквивалент — снять alive; запись остаётся flags=1, т.е.
 * враг не вернётся (как и в оригинале — он умер).
 *
 * Без этого отставшие враги жили вечно и занимали слоты пула (128). */
void ai_despawn_behind(int player_x_px)
{
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *o = &g_obj[i];
        if (!o->alive) continue;
        if (o->is_boss || o->is_scroll) continue;   /* goal-объект и его награда */
        if (o->level_idx < 0) continue;             /* объекты порта гаснут сами */
        if (!(o->subtype & 1)) continue;            /* чётная parity — второй проход не трогает */
        if (player_x_px - o->x > 250) o->alive = false;
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
    /* Единственная точка инициализации AI — prepare_level() в game.c. */
    /* KARNOV_DBG_OBJECTS=1 — сколько объектов живо/выделено (диагностика
     * заполнения пула и работы off-screen despawn'а). */
    { const char *dbo = getenv("KARNOV_DBG_OBJECTS");
      int every = dbo ? atoi(dbo) : 0;
      if (every > 0 && (frame % (u16)every) == 0) {
        int alive = 0, from_level = 0, shots = 0;
        for (int i = 0; i < g_num; ++i) {
            if (!g_obj[i].alive) continue;
            ++alive;
            if (g_obj[i].level_idx >= 0) ++from_level;
            if (g_obj[i].type == TYPE_PLAYER_FIREBALL) ++shots;
        }
        fprintf(stderr, "f=%u alive=%d (из уровня %d) выстрелов=%d сила=%d slots=%d/%d kx=%d\n",
                frame, alive, from_level, shots, inv_firepower(), g_num, MAX_AI, physics_get_left_x());
    } }
    /* KARNOV_DBG_TYPE=<n> — трассировка объектов одного типа рядом с
     * координатами игрока в конвенции движка (левый край / верх спрайта). */
    { const char *dt = getenv("KARNOV_DBG_TYPE");
      if (dt) { int want = atoi(dt);
        for (int i = 0; i < g_num; ++i) {
            if (!g_obj[i].alive || g_obj[i].type != (u16)want) continue;
            fprintf(stderr, "f=%u t%d x=%d y=%d h=%d ph=%d misc=%d aux=%d st=%d "
                    "| karnov left=%d top=%d feet=%d\n",
                    frame, want, g_obj[i].x, g_obj[i].y, obj_height(&g_obj[i]),
                    g_obj[i].st_phase, g_obj[i].st_misc, g_obj[i].st_aux,
                    g_obj[i].anim_state, physics_get_left_x(), physics_get_top_y(),
                    physics_get_y());
        } } }
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) continue;
        ai_obj_t *o = &g_obj[i];
        o->cur_h = obj_height(o);
        ++o->st_ctr;                              /* +0xC7: каждый кадр */
        if (o->phys) obj_integrate(o);            /* фаза 1 (UPDATELOCS) */
        if (o->use_anim) {
            /* Троттлинг THINKCYCLE: обработчик думает раз в tick_period
             * кадров (file 0x0FD88), скорость при этом применяется каждый. */
            if (--o->tick_countdown > 0) continue;
            o->tick_countdown = o->tick_period;
            const anim_list_t *al = (o->type < 58) ? &ANIM_LISTS[o->type] : NULL;
            if (al && al->count && o->anim_state < al->count) {
                if (++o->frame_index >= al->states[o->anim_state].n) o->frame_index = 0;
            }
            o->pending_state = -1;
            pick_handler(o->type)(o, frame, karnov_input);
            anim_apply(o);
        } else {
            pick_handler(o->type)(o, frame, karnov_input);
        }
    }

    /* Sky-spawner — драйвер вне объектной системы (file 0x12DB5). */
    ai_sky_tick(frame);

    /* Off-screen despawn — та же фаза B, сразу после AI (file 0xFD6A). */
    ai_despawn_behind(physics_get_left_x());

    /* После убийства boss'а через ~30 ticks спавним свиток (scroll item).
     * custom=999 — sentinel маркер подбора (level-end). item_sprite=0x90 —
     * настоящий спрайт выпадающего свитка (user-verified 2026-07-10, скриншот:
     * белый объект на земле). Karnov автоматически подходит к нему (неуязвим),
     * берёт и садится со свитком. Прежняя заглушка #0x32 (Treasure) была
     * неверной — #329-333 из ai_type56 вообще не рисуются как обычные спрайты
     * (это extended-data, не блиттятся), а реальный свиток = обычный #0x90. */
    /* KARNOV_DBG_KILLBOSS=1 — убить goal-объект сразу, как он появится
     * (проверка выпадения свитка и level-end без прохождения уровня). */
    { static bool kb_armed = true;
      if (kb_armed && getenv("KARNOV_DBG_KILLBOSS")) {
        for (int i = 0; i < g_num; ++i) {
            if (!g_obj[i].alive || !g_obj[i].is_boss) continue;
            g_obj[i].alive = false;
            g_boss_dead = true; g_boss_dead_frame = frame;
            g_boss_x = g_obj[i].x; g_boss_y = g_obj[i].y;
            kb_armed = false;
            fprintf(stderr, "DBG: boss убит на (%d,%d), кадр %u\n", g_boss_x, g_boss_y, frame);
            break;
        }
      } }

    if (g_boss_dead && !g_scroll_picked) {
        bool scroll_exists = false;
        int  scroll_slot   = -1;
        for (int i = 0; i < g_num; ++i) {
            if (g_obj[i].alive && g_obj[i].is_scroll) {
                scroll_exists = true; scroll_slot = i; break;
            }
        }
        /* Страховка проходимости: если свиток лежит там, куда Карнов не
         * может дойти (на LEVEL4 «гусеница» висит на 126 px выше него,
         * и свиток ложился на верхний ярус — уровень было не закончить),
         * через 400 кадров переносим свиток к самому Карнову. */
        if (scroll_exists && (u16)(frame - g_boss_dead_frame) > 400) {
            ai_obj_t *sc = &g_obj[scroll_slot];
            int h = sc->cur_h > 0 ? sc->cur_h : obj_height(sc);
            if (sc->y + h < physics_get_y() - 20) {
                sc->x = physics_get_left_x();
                sc->y = physics_get_y() - h;
                sc->custom = 1;                 /* уже «приземлился» */
            }
        }
        if (!scroll_exists && (frame - g_boss_dead_frame) >= 30) {
            int slot = alloc_obj_slot();
            /* X падения: место гибели boss'а, но только если под ним есть
             * на что упасть. Если там пропасть (LEVEL4 — boss висит над
             * ямой), роняем свиток на площадку самого Карнова: иначе он
             * улетает вниз и до него не дойти. */
            /* X падения — место гибели boss'а. Ниже под ним площадка
             * может быть на другом ярусе (LEVEL4 — на 90 px ниже); это
             * нормально: Карнов доходит до свитка авто-ходьбой и просто
             * спрыгивает к нему. Важно лишь не уронить свиток ВЫШЕ него. */
            int sx = g_boss_x;
            if (level_ground_row_below(sx, physics_get_y() / TILE_H, 0x0001) < 0)
                sx = physics_get_left_x();      /* совсем нет опоры — к Карнову */
            if (slot >= 0) {
                g_obj[slot] = (ai_obj_t){
                    .alive       = true,
                    .type        = 5,             /* падающий бонус */
                    .x           = sx,            /* X boss'а, если туда можно дойти */
                    /* Падает с НЕБОЛЬШОЙ высоты над местом гибели boss'а, а
                     * не с y=50: на уровнях с ярусами (LEVEL4 — «гусеница»)
                     * свиток, падавший с самого верха, ложился на площадку
                     * ВЫШЕ Карнова и уровень нельзя было закончить. Теперь
                     * он приземляется ровно на ту поверхность, где стоял
                     * boss, — туда, где Карнов с ним и дрался. */
                    /* Стартовая высота — НЕ выше Карнова: иначе свиток
                     * ложится на ярус над его головой (LEVEL4). */
                    .y           = (g_boss_y - 40 > physics_get_top_y())
                                       ? g_boss_y - 40 : physics_get_top_y(),
                    .vy          = 1,
                    .facing      = 1,
                    .is_scroll   = true,
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
/* ─── Разворот врага = ДРУГОЙ СПРАЙТ, а не отражение ──────────────────
 *
 * Движок оригинала арт не зеркалит: в архиве оба разворота лежат
 * отдельными спрайтами (75 зеркальных пар, Карнов 98..105 ↔ 106..113), а
 * направление выбирается НОМЕРОМ СОСТОЯНИЯ анимации. Конвенция —
 * state 0 = влево, state 1 = вправо — подтверждена двумя декомпили-
 * рованными INIT'ами: type 16 ставит state = (игрок правее) и type 25
 * ставит state = (vx > 0).
 *
 * У 16 типов состояния 0/1 действительно зеркальная пара; у остальных
 * (в т.ч. боссов 32 «девушка со львами», 38 «динозавр», 40 «гусеница»)
 * второго разворота в архиве НЕТ — в оригинале они всегда нарисованы
 * мордой влево. Порт же отражал их при facing < 0 и показывал спиной к
 * Карнову; отражение убрано, вместо него — эта подмена спрайта. */
static signed char g_dir_states[58] = { 0 };   /* 0 не проверено, 1 да, -1 нет */

static bool type_has_dir_states(u16 type)
{
    if (type >= 58) return false;
    if (g_dir_states[type] == 0) {
        const anim_list_t *al = &ANIM_LISTS[type];
        g_dir_states[type] =
            (al->count >= 2 && al->states[0].n && al->states[1].n &&
             sprite_is_mirror(al->states[0].spr[0], al->states[1].spr[0])) ? 1 : -1;
    }
    return g_dir_states[type] > 0;
}

/* Перевести кадр в набор нужного разворота, сохранив НОМЕР кадра. */
static u16 sprite_face(u16 type, u16 sid, int facing)
{
    if (sid == 0xFFFF || !type_has_dir_states(type)) return sid;
    const anim_state_t *s0 = &ANIM_LISTS[type].states[0];
    const anim_state_t *s1 = &ANIM_LISTS[type].states[1];
    for (int k = 0; k < s0->n; ++k)
        if (s0->spr[k] == sid) return (facing > 0) ? s1->spr[k % s1->n] : sid;
    for (int k = 0; k < s1->n; ++k)
        if (s1->spr[k] == sid) return (facing > 0) ? sid : s0->spr[k % s0->n];
    return sid;
}

static u16 sprite_for_type(u16 type, u16 anim_phase)
{
    switch (type) {
        case TYPE_GOLD_BALL:                       /* шар «золотого человека» */
            return (u16)(185 + (anim_phase & 1));
        case TYPE_TRAP_SEGMENT:                    /* сегмент ловушки type 42 */
            return (u16)(271 + (anim_phase & 3));
        case  23:                                  /* летящий шар: INIT ставит
                                                    * box0.sprite = 0xB9 (#185)
                                                    * и больше не меняет */
            return 185;
        case   5: {                                /* падающий бонус — option icon */
            /* User-verified 2026-05-29 (project_inventory_items.md):
             *   #39 ladder, #40 boots, #41 bomb, #42 fireballs(30),
             *   #43 boomerang, #44 mask, #45 wings, #47 aqualung,
             *   #48 treasure, #49 multi-shot.
             * #46 не указан — gap. Anim_phase крутит 10 опций. */
            /* item_id = sprite − 39 (см. inventory.h): #39..#49 подряд. */
            return (u16)(39 + (anim_phase % (u16)ITEM_NUM));   /* #39..#49 */
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
 * Anchor по X — ЛЕВЫЙ КРАЙ арта в o->x (update_bboxes: box.x0 =
 * x_world + dx). Кэш переизвлечён без зеркалирования, поэтому арт лежит
 * так же, как в архиве — прижатым влево. Разворот в оригинале делается
 * выбором зеркального спрайта; там, где порт этого ещё не делает, мы
 * отражаем в пределах АРТА (art_w), а не канвы — иначе спрайт прыгал бы
 * на ширину прозрачного паддинга. */
static void blit_sprite_world(u16 sid, int wx, int wy, int facing,
                              int scroll_x, int scroll_y)
{
    const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
    if (!s) return;
    (void)facing;      /* оригинал арт не зеркалит — разворот выбирается спрайтом */
    int x0 = wx - scroll_x;
    int y0 = wy - scroll_y;
    u8 ck = sprite_is_cga() ? 0xFF : 1;
    const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
    for (int r = 0; r < s->h; ++r) {
        int yy = y0 + r;
        if (yy < 0 || yy >= WIN_H) continue;
        for (int c = 0; c < s->art_w; ++c) {
            u8 v = s->pixels[r * s->w + c];
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
        /* Скрытые предметы (misc_state ≥ 11 в оригинале) видны, пока идёт
         * таймер Glasses — file 0x12507 читает [0xD8B8]. */
        if (g_obj[i].invisible &&
            !((g_obj[i].type == 4 || g_obj[i].type == 6) && inv_glasses_active()))
            continue;
        if (g_obj[i].type == 7) continue;  /* невидимый триггер-«призрак» (num_bboxes=0) */
        if (g_obj[i].type == 9) continue;  /* стартовый дракон — рисуется draw_level1_dragon() */
        if (g_obj[i].type == TYPE_BOMB_BLAST) continue;  /* вспышка — поверх Karnov'а (ai_render_overlay) */
        /* Пасс 0 — только фоновые летуны; пасс 1 — все остальные поверх. */
        if (ai_is_background_flyer(g_obj[i].type) != (pass == 0)) continue;
        /* Karnov-выстрел рисуется своим спрайтом из архива (не процедурным
         * ромбиком, как было): state_list снаряда DS:0x16B2 — состояние 0
         * (обычный выстрел) = один кадр #1, состояние 1 (firepower > 1) =
         * цикл #2,#2,#3,#3. Выбор состояния — по силе огня на момент
         * выстрела (cs:0xFFA:0x0009). */
        if (g_obj[i].type == TYPE_PLAYER_FIREBALL) {
            static const u16 POWERED[4] = { 2, 2, 3, 3 };
            u16 sid = g_obj[i].custom ? POWERED[g_obj[i].anim_phase & 3] : 1;
            blit_sprite_world(sid, g_obj[i].x, g_obj[i].y, +1,
                              scroll_x, scroll_y);
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
        if (g_obj[i].type == TYPE_ENEMY_SHOT && g_obj[i].item_sprite > 0) {
            sid = g_obj[i].item_sprite;
        } else if ((g_obj[i].type == 6 || g_obj[i].type == 4) && g_obj[i].item_sprite > 0) {
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
            /* Разворот — подменой спрайта (оригинал арт не зеркалит). */
            sid = sprite_face(g_obj[i].type, sid, g_obj[i].facing);
        }
        const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
        if (s) {
            /* Anchor: X — ЛЕВЫЙ край арта, Y — ВЕРХ спрайта
             * (box.x0 = x_world + dx, box.y0 = y_world + dy). */
            int x0 = g_obj[i].x - scroll_x;
            int y0 = g_obj[i].y - scroll_y;
            u8 ck = sprite_is_cga() ? 0xFF : 1;
            const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
            for (int r = 0; r < s->h; ++r) {
                int yy = y0 + r;
                if (yy < 0 || yy >= WIN_H) continue;
                for (int c = 0; c < s->art_w; ++c) {
                    u8 v = s->pixels[r * s->w + c];
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
        int x0 = g_obj[i].x - scroll_x;
        int y0 = g_obj[i].y - scroll_y;
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
 * Karnov bbox ≈ 16×24 от (kx, ky) = центр-ног (его физика ведёт Y по
 * ногам). Предмет: o->y — ВЕРХ иконки, высота ≈ 12 → (o->y .. o->y+12).
 * Простое AABB-пересечение. */
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
        if (o->is_scroll) {
            int ix0 = o->x, ix1 = o->x + 16;
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
            int ix0 = o->x, ix1 = o->x + 16;
            int iy0 = o->y, iy1 = o->y + 12;
            if (kx1 < ix0 || kx0 > ix1) continue;
            if (ky1 < iy0 || ky0 > iy1) continue;
            give_item_by_sprite(o->item_sprite);
            audio_play_sfx(5);   /* подбор предмета — play_sound(5) */
            o->alive = false;    /* запись остаётся flags=1 — предмет одноразов */
            continue;
        }
        if (o->type != 6 && o->type != 4) continue;
        if (o->invisible && !inv_glasses_active()) continue;  /* скрытые — только с Glasses */
        u16 sid = (o->item_sprite > 0) ? o->item_sprite
                : (o->custom > 0)      ? (u16)o->custom : 0;
        if (sid == 0) continue;
        /* Item bbox: y = ВЕРХ иконки → (x-8..x+8, y..y+12). */
        int ix0 = o->x, ix1 = o->x + 16;
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
        g_boss_y          = o->y;
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
            g_boss_y          = o->y;
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
            .x           = physics_get_left_x(),
            /* Karnov-Y — ноги; вспышке нужен ВЕРХ (общая объектная
             * конвенция), поэтому поднимаем на высоту спрайта вспышки. */
            .y           = physics_get_top_y() - 24,
            .spawn_frame = g_ai_frame,
        };
    }
    ai_clear_enemies(false);
}

bool ai_boss_dead(void)        { return g_boss_dead;    }
bool ai_scroll_picked(void)    { return g_scroll_picked; }
u16  ai_boss_dead_frame(void)  { return g_boss_dead_frame; }
int  ai_scroll_target_x(void)
{
    /* Идти надо к САМОМУ свитку: он мог упасть не там, где погиб boss. */
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].is_scroll) return g_obj[i].x;
    return g_boss_x;
}

/* Свиток (type5 custom=999) уже заспавнен и падает? Karnov должен начать
 * авто-подход к свитку ТОЛЬКО после того как свиток начал выпадать. */
bool ai_scroll_spawned(void)
{
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].is_scroll)
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
        if (g_obj[i].alive && g_obj[i].is_scroll) {
            int sx = g_obj[i].x - cam_x;
            int sy = g_obj[i].y - cam_y;
            return sx >= 0 && sx <= view_w && sy >= 0 && sy <= view_h;
        }
    }
    return false;
}

/* ─── Player fireball spawn ──────────────────────────────────────────── */
/* Сколько снарядов игрока сейчас в воздухе — оригинал не даёт стрелять,
 * если их 15 и больше (счётчик DS:0x1424, проверка в fire() @file 0x1303B). */
static int player_shots_alive(void)
{
    int n = 0;
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].type == TYPE_PLAYER_FIREBALL) ++n;
    return n;
}

void ai_spawn_player_fireball(int x, int y, int facing, u16 frame)
{
    /* Порт fire() (cs:0xFFA:0x1888, file 0x13028) и spawn_player_shot
     * (cs:0xFFA:0x17AF, file 0x12F4F):
     *   • при 15 живых снарядах выстрел не происходит вовсе (file 0x1303B);
     *   • звук — play_sound(0) (file 0x13045), НЕ 7;
     *   • число снарядов = firepower (1..3, [DS:0xD949]), и ветки таблицы
     *     file 0x12FB5 «проваливаются» СВЕРХУ ВНИЗ, спавня РАЗНЫЕ объекты
     *     с разными обработчиками — то самое «раздвоение/растроение»:
     *       fp=3 → верхний (y−2, cs:0x00FF) + нижний (y+2, cs:0x0183)
     *              + прямой (y, cs:0x00C2)
     *       fp=2 → нижний + прямой      fp=1 → только прямой
     *   • точка вылета: x = player.x_world + 4 ед. (= левый край + 8 px),
     *     y = верх спрайта Карнова + 7 (+16 если сидит);
     *   • состояние анимации выбирается по firepower при СПАВНЕ
     *     (cs:0xFFA:0x0009: [0xD88C] = firepower > 1). */
    if (player_shots_alive() >= 15) return;

    int fp = inv_firepower();
    if (fp < 1) fp = 1;
    if (fp > 3) fp = 3;
    int dir = (facing < 0) ? -1 : +1;
    bool powered = (fp > 1);

    /* variant, dy, |vx| (единицы движка/кадр) */
    static const struct { int var, dy, spd; } SHOTS[3] = {
        { SHOT_RISING,   -2, 7 },   /* спавнится только при fp = 3 */
        { SHOT_GROUND,   +2, 7 },   /* при fp >= 2 */
        { SHOT_STRAIGHT,  0, 6 },   /* всегда */
    };
    for (int k = 3 - fp; k < 3; ++k) {
        int slot = alloc_obj_slot();
        if (slot < 0) break;
        ai_obj_t *o = &g_obj[slot];
        *o = (ai_obj_t){
            .alive       = true,
            .type        = TYPE_PLAYER_FIREBALL,
            .x           = x,
            .y           = y + SHOTS[k].dy,
            .vx          = dir,
            .facing      = dir,
            .spawn_frame = frame,
            .st_phase    = SHOTS[k].var,
            .custom      = powered ? 1 : 0,
        };
        obj_phys_begin(o);
        o->vxq = dir * QV(SHOTS[k].spd, 0);
        if (SHOTS[k].var == SHOT_RISING) {
            o->vyq    = -QV(5, 0);
            o->st_aux = 1;            /* «поднимается» */
            o->st_misc = o->y - 16;   /* цель подъёма (+0xE9 = y − 0x10) */
        }
    }
    audio_play_sfx(0);   /* выстрел — play_sound(0), KARNOV.EXE @ file 0x13045 */
}
