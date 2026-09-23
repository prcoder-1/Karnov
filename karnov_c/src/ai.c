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
/* TYPE_FLYING_BIRD (1004) убран (2026-09-13) вместе с ai_flying_bird/
 * ai_bird_flock_tick — это была честно подписанная «чистая синтетика
 * порта» (случайная «птичья стая»), не соответствующая ничему в оригинале;
 * настоящие «орлы» LEVEL1 — это байт-точный AI TYPE 10 (ai_type10). */
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
#define TYPE_ARENA_PILLAR      1009  /* 8-сегментный столб арены (тип 51):
                                      * отдельный объект cs:0x175E:0x3A9,
                                      * queue_state из state4/6 родителя,
                                      * см. ai_arena_static/ai_arena_pillar. */

/* Сентинел hit_idx для «партнёр — сам игрок» (obj_type партнёра == 0 в
 * оригинале, см. ai_type49 и ai_check_player_hit). Игрок не входит в
 * g_obj[], поэтому обычный индекс сюда попасть не может. */
#define HIT_IDX_PLAYER (-2)

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
static void arm_real_scroll(void);
static int  obj_width(const struct ai_obj *o);
static void ai_head_column_hit_box(struct ai_obj *o, int idx);

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
    /* Вспышка попадания (порт puff-обработчика cs:0xD4B:0x3C0): пока
     * счётчик > 0, вместо врага рисуется кадр вспышки #4..#6. */
    int    puff_frames;
    /* «Добитый, но оставшийся на карте»: у некоторых типов состояние 4
     * не удаляет объект, а переводит его в obj_type = 9 с hitflag = 0 —
     * он остаётся стоять неуязвимой декорацией (тип 54, file 0x1953B). */
    bool   is_decor;
    /* Гибнет: показываем вспышку и снимаем объект по её окончании.
     * В оригинале это состояние 4 → die_puff → пуф-обработчик → 6. */
    bool   is_dying;
    /* Порт полей pair-loop: +0xDD (задели в этом кадре) и +0xDF (индекс
     * объекта, с которым пересеклись боксы). Оригинал заполняет их
     * проходом по боксам, file 0x2CB0/0x2D72. */
    bool   hit_flag;      /* коснулись в ЭТОМ кадре */
    bool   has_partner;   /* +0xDF когда-либо заполнялось */
    int    hit_idx;        /* индекс партнёра в g_obj[], либо HIT_IDX_PLAYER */
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
 * state_byte = 6. В порту реализовано — см. ai_despawn_behind() ниже. */

/* Boss-конец: когда goal-объект убит — g_boss_dead=true и через 30 ticks
 * спавним свиток (scroll item) сверху. После пикапа Karnov-ом инициируем
 * level-end transition. Goal-объект определяется данными (check_triggers
 * file 0x517F..0x518D): на уровне 9 — запись type 56, на остальных —
 * последняя (по sort cur_x) запись таблицы спаунов. */
static bool g_boss_dead = false;
static u16  g_boss_dead_frame = 0;
static bool g_scroll_picked = false;
/* Счёт игрока (в оригинале — BCD-строка DS:0xD930). */
static u32  g_score = 0;
static int  g_boss_x = 0;   /* X boss'а на момент смерти — фикс. точка падения свитка */
static int  g_boss_y = 0;
/* Левый край камеры — сохраняется каждый кадр из ai_check_triggers;
 * нужен типам, которые живут «пока видны» (23). */
static int  g_camera_left = 0;   /* Y boss'а на момент смерти — с этой высоты падает свиток */
/* Верхний край камеры — сохраняется из ai_render (единственное место в
 * порту, куда прокинут scroll_y каждый кадр); используется типом 25 для
 * отражения скорости от границ видимой области (см. t25_reflect). На кадр
 * позже, чем g_camera_left (ai_render идёт после ai_tick) — не критично,
 * скролл меняется на единицы px/кадр. */
static int  g_camera_top = 0;

/* Sky-spawner (драйвер file 0x12DB5) — работает ВНЕ объектной системы.
 * Запись type 8 cur=(0,0) range=(0,0) в BIN никогда не спавнится сама
 * (range=0 при строгом «<») — она лишь маркер «уровень имеет sky-спаун».
 * Состояние: последняя точка прогресса игрока (DS:0xD8A0) и счётчик
 * простоя (DS:0xD8A4). */
static bool s_sky_enabled = false;
static int  s_ice_ctr = 0;            /* [0xD8A4] — простой sky-цепи (лёд) */
static int  s_ice_last_x_units = 0;   /* [0xD8A0] — точка прогресса игрока */


static void spawn_rock(int x, int y, int vx, u16 frame);
static int alloc_obj_slot(void);
static void obj_aim(ai_obj_t *o, int from_x, int from_y, int to_x, int to_y, int n);

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
    if (o->is_decor || o->is_dying) return false;   /* добитая/гаснущая */
    /* Псевдотипы порта. */
    switch (o->type) {
        case TYPE_STONE_THROWER:
        case TYPE_ROCK_PROJECTILE:
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
    /* Тип 46 уязвим только «высунувшись» (file 0x1878B). */
    if (o->type == 46) return o->custom != 0;
    /* Тип 53 неуязвим, пока падает (file 0x193DD). */
    if (o->type == 53) return o->custom != 0;
    /* Тип 55 неуязвим на «отдыхе» (file 0x196E8). */
    if (o->type == 55) return o->custom != 0;
    /* Тип 37 неуязвим, пока дремлет (file 0x175D1). */
    if (o->type == 37) return o->custom != 0;
    /* Тип 48 (турель) уязвима только пока игрок ближе 120 ед. по X,
     * иначе box0.hitflag=0 (file 0x18B2E/0x18B3E). */
    if (o->type == 48) return o->custom != 0;
    /* Тип 33 теряет hitflag (box0.hitflag=0), как только начал «схлопывание»
     * (file 0x16BB3): подбитая насмерть колонна не может быть задета
     * повторно — она просто превращается в неубираемую груду обломков.
     * ВАЖНО (см. подробный разбор в ai_multipart): касание телом сюда
     * вообще не ведёт — только смертельный удар снарядом/Bomb. */
    if (o->type == 33) return o->st_phase < 2;
    /* Столб арены (порождение типа 51) неуязвим целиком: во всех 8 боксов
     * его собственного INIT hitflag=0 (file 0x191E1..0x19226). */
    if (o->type == TYPE_ARENA_PILLAR) return false;
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
    if (o->type == 7 || o->type == 8) return false;       /* свиток / спаунер */
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
    int dy = 0;
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
            if (as->n) {
                int fi = (o->frame_index < as->n) ? o->frame_index : 0;
                sid = as->spr[fi];
                /* ИСПРАВЛЕНО (2026-09): эта ветка (используется ТОЛЬКО при
                 * спавне, до первого anim_apply) игнорировала dy кадра —
                 * а он не нулевой почти везде (см. anim_apply, `ai.c:417-418`,
                 * которая для КАЖДОГО следующего тика формулу dy+h уже
                 * учитывает). Итог: постановка на землю при спавне была
                 * смещена ровно на dy кадра 0 состояния 0 (для типа 11 —
                 * на 2 px вверх, «стражники висят над землёй» на LEVEL1).
                 * Приводим формулу к тому же виду, что и в num_parts>0
                 * ветке выше (bottom = dy + sprite_h). */
                if (as->off) dy = as->off[fi * 2 + 1];
            }
        }
    }
    if (sid == 0xFFFF)
        sid = ((o->type == 4 || o->type == 5 || o->type == 6) && o->item_sprite > 0)
                  ? o->item_sprite
                  : sprite_for_type(o->type, o->anim_phase);
    const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
    return s ? dy + (int)s->h : 16;
}

/* Верхняя граница многосегментного объекта относительно o->y — минимум
 * (самый отрицательный) parts[i].dy. Нужна отдельно от obj_height
 * (которая берёт МАКСИМУМ dy+h, т.е. НИЖНИЙ край): у объектов со
 * стеком, растущим ВВЕРХ (тип 33 — голова/тело торчат на dy до −68,
 * тип 21 — верхний ряд мозаики на dy=−30), верх спрайта лежит ЗНАЧИТЕЛЬНО
 * выше o->y, а не в нём самом. Без этой поправки AABB-хиттест снаряда
 * (ai_player_fireball) считал верх бокса равным o->y и промахивался мимо
 * всей верхней части такого объекта — файербол физически не мог попасть
 * в дерево (тип 33) выше его нижней кромки. */
static int obj_top_offset(const ai_obj_t *o)
{
    int top = 0;
    for (int i = 0; i < o->num_parts; ++i)
        if (o->parts[i].dy < top) top = o->parts[i].dy;
    return top;
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
#define Q_4_0    QV(4, 0)

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
 *     17, 24, 28, 29, 31, 32, 46, 48, 54, 55 (8 — sky-спаунер, в
 *     порту не материализуется) + тип 26, который зовёт ядро 0x14C сам.
 *     Тип 53 из списка ИСКЛЮЧЁН: его INIT землю не трогает, постановка
 *     происходит в фазе приземления, а при спавне он должен падать
 *     сверху (иначе проба «пол на 10 px ниже ног» промахивается).
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
        case 31: case 32: case 46: case 48: case 54: case 55:
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

/* Урон снаряда Karnov-а по врагу — ТЕПЕРЬ БАЙТ-ВЕРИФИЦИРОВАНО (code-review
 * PSEUDO-TYPES 2026-09, две независимые точки в дизасме):
 *   1) Общий INIT снаряда cs:0xFFA:0x0009 (file 0x117A9, см. коммент выше
 *      «TYPE_PLAYER_FIREBALL») пишет `mov word [es:bx+0xE7],0xA` (file
 *      0x117C2) — то есть hit_points САМОГО снаряда = 10, безусловно для
 *      всех трёх вариантов выстрела (firepower 1/2/3).
 *   2) Общий pair-loop урона (level.cpp, file 0xFC2C..0xFC42) для ЛЮБОЙ
 *      пары объектов с obj_type атакующего в [1..9]: `b.hit_points -=
 *      a.hit_points; a.hit_points -= (b.hit_points ДО вычитания)` —
 *      симметричный обмен по хитпоинтам самих объектов, не константа урона.
 * Combining 1+2: снаряд Карнова всегда отнимает у цели ровно 10 (своих же
 * hit_points), независимо от firepower — что и объясняет, почему все
 * HP-пулы врагов в LEVEL%d.BIN кратны 10 (обычные=10 → 1 удар, type18=20 →
 * 2, type13/15=30 → 3, боссы 95..250 → 10..25 попаданий). */
#define PLAYER_FIREBALL_DAMAGE 10

/* Варианты снаряда: три разных обработчика оригинала (см. spawn_player_shot,
 * file 0x12F4F — таблица переходов по firepower, file 0x12FB5).
 * Дизассемблировано заново и байт-подтверждено (2026-09, code-review этой
 * сессии): каждый обработчик вызывает общий INIT (cs:0xFFA:0x0009, file
 * 0x117A9) с параметрами (dx,ax) = магнитуда скорости Q16.16, из которой
 * INIT сам ставит знак по facing ([DS:0xD8B0] бит0=вправо):
 *   STRAIGHT: file 0x1187F "mov dx,6; xor ax,ax" → магнитуда 6.0
 *   RISING:   file 0x118BC "mov dx,7; xor ax,ax" → магнитуда 7.0
 *   GROUND:   file 0x11945 "mov dx,7; xor ax,ax" → магнитуда 7.0
 * obj_integrate() порта удваивает целую часть xq при переводе в пиксели
 * (`o->x = (xq>>16)*2`), поэтому величины 6.0/7.0 ниже — это именно то,
 * что нужно передавать в QV(spd,0) БЕЗ дополнительного удвоения. */
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
        /* Подъём ровно на 16 px от точки выстрела, затем горизонтально —
         * переразобрано и байт-подтверждено (2026-09, file 0x118BC/0x118E7,
         * = cs:0xFFA:0x011C init / 0x0147 tick):
         *   init: vy_hi = 0xFFFB (= −5, т.е. vy = −5.0 Q16.16), private-флаг
         *     «поднимается» = 1, target_y = y_world − 16;
         *   tick: `if (flag) { if (y_world < target_y) { vy=0; flag=0 } }`
         *     (cmp+jnl: сброс срабатывает, когда y_world СТРОГО МЕНЬШЕ
         *     target, т.е. поднялись НЕ МЕНЕЕ чем на 16 px). */
        if (o->st_aux && o->y < o->st_misc) { o->vyq = 0; o->st_aux = 0; }
        break;
    case SHOT_GROUND: {
        /* Каждый тик щупается тайл ПОД снарядом: категории 2 и 9 (небо/
         * фон) — продолжаем падать со скоростью 5.0, иначе ложимся на
         * поверхность (vy = 0). Тайл В точке снаряда категории 3 или 4 —
         * снаряд гаснет (cs:0xFFA:0x01B3, file 0x11953/0x119A6) —
         * байт-подтверждено заново (2026-09): обе ветки таблицы `ax==3` и
         * `ax==4` (file 0x119BF) ведут на один и тот же код, ставящий
         * state_byte=4; в оригинале это на один кадр ОТКЛАДЫВАЕТ удаление
         * (снаряд гасится не мгновенно, а на следующем тике, после ещё
         * одной итерации UPDATELOCS) — порт убирает объект немедленно
         * (`return` ниже). Разница на 1 кадр движения признана
         * незначительной и не портируется (см. project convention
         * «непортированное помечать явно»). */
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

    /* Despawn off-screen — ПРАВИЛЬНО screen-relative, не world-relative.
     * Передизассемблировано (2026-09): общая THINK-функция снаряда игрока
     * (cs:0xFFA:0x0083, file 0x11823) сравнивает тайл-колонку снаряда с
     * [0xB834]/[0xB838] — а эти два слова пересчитываются КАЖДЫЙ кадр из
     * ТЕКУЩЕГО экранного прямоугольника (file 0xFFCF: `[0xb834] =
     * [0xcc8e] + [0xcc9a]`; file 0xFFE3: `[0xb838] = [0xcc92] - [0xba66]`,
     * где [0xCC8E]/[0xCC92] — левая/правая граница ВИДИМОГО экрана, не
     * уровня). Т.е. снаряд снимается, как только выходит из ВИДИМОЙ
     * области — а не когда долетает до края уровня. Прежняя версия этого
     * комментария ("теперь world-coords, не screen") ссылалась на
     * непроверенное предположение и была неверна: именно она объясняла
     * баг «Карнов иногда перестаёт стрелять» — снаряды, не долетевшие до
     * края уровня, годами оставались «живыми» и забивали лимит в 15 штук
     * ([0x1424], см. player_shots_alive() выше).
     * Точные original-константы [0xCC9A]/[0xBA66] (небольшие поля-запасы)
     * не восстановлены — используется тот же идиом «видим на экране»,
     * что и у is_damageable-видимости в type23/type20 (g_camera_left..+
     * VIEW_W), с запасом в один тайл на сторону. */
    if (o->x < g_camera_left - TILE_W || o->x > g_camera_left + VIEW_W + TILE_W) {
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
        /* Тип 20 (колонна голов, obj_type=9): в оригинале ЛЮБОЕ попадание
         * снаряда в obj_type==9 НЕ идёт через симметричный обмен hit_points
         * (generic pair-loop, file 0xFB68: таблица по b.obj_type для
         * значения 9 ведёт на file 0xFBCB — ставит только state_byte=5 и
         * ВООБЩЕ ПРОПУСКАЕТ hp-обмен, см. ai_head_column_hit_box) — у него
         * ПЕР-БОКСОВАЯ система повреждений (2 бита счётчика попаданий на
         * каждый box в self.st_aux/+0xEB, file 0x152B8): 3 попадания по
         * ОДНОМУ И ТОМУ ЖЕ сегменту гасят именно его (спрайт→0, вне игры),
         * не весь объект разом. Поэтому проверяем КАЖДЫЙ part отдельно,
         * а не общий AABB объекта. */
        if (e->type == 20 && e->num_parts > 0) {
            for (int p = 0; p < e->num_parts; ++p) {
                if (e->parts[p].sprite == 0) continue;   /* уже разрушен — box.flag=0 */
                const sprite_t *ps = sprite_find(e->parts[p].sprite);
                int pw = ps ? (int)ps->art_w : 16, ph = ps ? (int)ps->h : 20;
                int px0 = e->x + e->parts[p].dx, px1 = px0 + pw;
                int py0 = e->y + e->parts[p].dy, py1 = py0 + ph;
                if (fx1 < px0 || fx0 > px1) continue;
                if (fy1 < py0 || fy0 > py1) continue;
                ai_head_column_hit_box(e, p);
                o->alive = false;
                return;
            }
            continue;      /* ни один сегмент не задет — снаряд летит дальше */
        }
        /* bbox врага в объектной конвенции: (x, y) — левый-верх,
         * размеры — из спрайта (в оригинале это box.x0..x1/y0..y1). */
        int ew = obj_width(e);
        int ex0 = e->x, ex1 = e->x + ew;
        int ey0 = e->y + obj_top_offset(e);
        int ey1 = e->y + (e->cur_h > 0 ? e->cur_h : 24);
        if (fx1 < ex0 || fx0 > ex1) continue;
        if (fy1 < ey0 || fy0 > ey1) continue;
        /* Hit. */
        ai_notify_enemy_hit(i, frame);
        o->alive = false;
        return;
    }
}

/* TYPE_BOMB_BLAST — анимированная вспышка Bomb-предмета. Врагов уже убил
 * ai_bomb_explode() при спавне (area-kill, не эта функция).
 *
 * ЧАСТИЧНО байт-верифицировано (code-review PSEUDO-TYPES 2026-09):
 * Bomb-предмет (item id 2, file 0x61ED) спавнит объект с обработчиком
 * cs:0xFFA:0xBCB (file 0x1236B). Его INIT: box0.sprite = 0x19 (#25),
 * x=player.x, y=player.y+5, hit_points=100, таймер(+0xE9) = 0x32 = **50**
 * — длительность 50 кадров ПОДТВЕРЖДЕНА байтом (age>=50 ниже — верно).
 * НО за все 50 тиков box0.sprite в этом обработчике НИ РАЗУ не
 * перезаписывается — по дизасму спрайт всё это время остаётся #25, а
 * НЕ растёт до #28. По истечении таймера объект спаунит ВТОРОЙ объект
 * (handler cs:0xFFA:0xA7B, file 0x1221B) с собственным StateScript
 * (DS:0x1706), 3-кадровая анимация со спрайтами [5, 27, 28] — назначение
 * spрайта #5 в этом контексте не установлено (возможно другой FX, не
 * относящийся к росту вспышки). Т.е. «рост через #25→#26→#27→#28» ниже —
 * НЕ подтверждённая байтом реконструкция, а правдоподобная замена;
 * оставлено как есть (визуально приемлемо), но не выдавать за декомпиляцию.
 * Разобрать до конца — cs:0xFFA:0xA7B TICK (условия смены кадра, подфазы
 * 0xB2C, возможный урон игроку по близости — поле +0x26 сравнивается с
 * player.x_world в этом обработчике и не объяснено). */
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


/* УСТАРЕЛО И ОШИБОЧНО (code-review GROUND MELEE/CHASER, 2026-09): здесь
 * раньше утверждалось, что тип 17 в LEVEL1 — «Зелёный каменный человек»,
 * якобы диспетчеризуемый на ai_stone_thrower. Это неверно и опровергнуто
 * побайтовой сверкой: AI_HANDLERS[17] = ai_type17 (см. ниже), отдельный
 * обработчик cs:0x11D6:0x0617 (file 0x13B77) — «скелет на страусе»,
 * который действительно всегда идёт влево с vx=−3.0 и НЕ отслеживает
 * игрока (повторная декомпиляция подтвердила это дословно: THINK-ветка
 * состоит только из `ai_result|=4`, без единого обращения к player_ptr).
 * ai_stone_thrower по-прежнему используется — но только для LEVEL0
 * splash-декорации, к type 17 отношения не имеет. */

/* ai_walking_minion — диспетчеризуется на ТИП 9 (AI_HANDLERS[9]), а не на
 * тип 18 (у того — отдельный ai_type18 ниже): заголовок «Type 18 — Чёртик»
 * ниже был скопирован не туда и описывает чужой дизасм — исправлено.
 *
 * ПОВТОРНАЯ ДЕКОМПИЛЯЦИЯ (code-review MULTIPART/STATIC, 2026-09): тип 9,
 * handler cs:0x11D6:0x1644 (file 0x14BA4) — «2-bbox boss-like декор-дракон».
 * INIT (file 0x14BC6): num_bboxes=2, box0=спрайт 0x91(145) в (0,0),
 * box1=спрайт 0x92(146) при dx=5 ед., dy=[DS:0x103D] (байт-таблица,
 * зависит от уровня/варианта); tick_period=1; финал — cs:0x6E:0x52B(self)
 * + ОДИН вызов «boss_helper» cs:0x11D6:0x15D6. THINK (state2) делает РОВНО
 * ТО ЖЕ — просто зовёт тот же boss_helper КАЖДЫЙ тик, больше ничего.
 *
 * boss_helper (file 0x14B36..0x14B73) НЕ двигает объект (нет записи в
 * x_world/y_world, vx/vy не трогаются) — это ОДНОРАЗОВАЯ проверка,
 * работающая только пока frame_ctr<=10: если player.x−self.x>30 ед. И
 * self.active(+0xB0)==0 → cs:0x6E:0x4B4(self) + cs:0xFFA:0x724() (похоже
 * на «показать»/эффект появления); отдельно если |self.x−player.x|<=8 И
 * self.active==0 → cs:0x6E:0x52B(self) (пересчёт bbox). НИКАКОГО
 * преследования игрока по X или Y-подтягивания в этом дизасме НЕТ.
 *
 * КОНФЛИКТ С ИСТОРИЕЙ ПОРТА — РАЗРЕШЁН (2026-09-12, повторный проход по
 * прямому указанию: приоритет у алгоритма оригинала над пользовательскими
 * правками, внесёнными ДО полного извлечения алгоритма). Прежняя реализация
 * (идёт к игроку 2px/кадр + Y-snap) была добавлена 2026-07-07 по жалобе
 * «демон застревает у точки спавна» — но полная передизассемблировка ОБЕИХ
 * функций типа 9 (сам обработчик, cs:0x11D6:0x1644..0x17F6, файл
 * 0x14BA4..0x14C56, включая общий с типом 19 boss_helper по адресу
 * cs:0x11D6:0x15D6 = file 0x14B36..0x14BA3) не находит ни единой записи в
 * self.x_world(+0x08)/self.y_world(+0x0A) НИГДЕ — ни в INIT, ни в THINK, ни
 * в boss_helper. Разобран boss_helper целиком:
 *   if (self.frame_ctr <= 10) return;                      // первые 10 кадров — no-op
 *   dx = player.x_world - self.x_world;
 *   if (dx > 30 && self.field_0xB0 != 0)
 *       { call cs:0x6E:0x4B4(self); call cs:0xFFA:0x724(); }  // непрозрачный эффект
 *   dx2 = player.x_world - self.x_world;                    // тот же dx, пересчитан
 *   if (dx2 > 8 && self.field_0xB0 == 0)
 *       call cs:0x6E:0x52B(self);                           // recompute-bbox
 * Оба вызова НЕ трогают позицию. К тому же САМ объект типа 9 не рендерится
 * через ai_render (см. `if (g_obj[i].type == 9) continue;` ниже по файлу) —
 * видимая картинка LEVEL1-дракона рисуется отдельно, хардкод-координатами,
 * в draw_level1_dragon() (game.c), НЕЗАВИСИМО от x/y этого AI-объекта. То
 * есть прежний «фикс» двигал невидимый (нерисуемый) объект — на экране
 * никакого эффекта дать не мог; предполагаемая жалоба 2026-07-07 была либо
 * про другой тип/объект, либо возникла ДО того как рендер дракона был
 * выделен в отдельную функцию. Возвращено к байт-точной статике оригинала:
 * позиция не меняется никогда, num_bboxes=2 с фиксированными смещениями
 * (INIT: box0=спрайт 145 @ (0,0), box1=спрайт 146 @ (dx=5, dy=DS:0x103D=57,
 * глобальная константа, НЕ зависит от уровня — «зависит от уровня/варианта»
 * было ошибочным домыслом более раннего прохода). Непрозрачные вызовы
 * 0x6e:0x4b4/0xffa:0x724/0x6e:0x52b не портированы — они не двигают объект
 * и не влияют на то немногое, что от него видно (или не видно). */
static void ai_walking_minion(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input; (void)frame;
    if (o->num_parts == 0) {
        o->num_parts = 2;
        o->parts[0].sprite = 145; o->parts[0].dx = 0; o->parts[0].dy = 0;
        o->parts[1].sprite = 146; o->parts[1].dx = 5; o->parts[1].dy = 57;
    }
    /* Оригинал никогда не пишет x_world/y_world для типа 9 — движения нет. */
}


/* Type 5 — падающий бонус (file 0x1254A, cs:0xFFA:0x0DAA; повторная
 * декомпиляция 2026-09, byte-точная). INIT: box0.dy=0, box0.hitflag=1,
 * vy=+2.0 px/frame. THINK (пока не приземлился): тест ФИКСИРОВАННОЙ точки
 * (x_world, box0.y0+10) — box0.y0=y_world (dy=0), т.е. это (x, y+10), а
 * НЕ высота спрайта и НЕ «ноги» (12px-иконки #39-48 ниже настоящей точки
 * контакта на ~2px — так в оригинале). Тест — bit0 таблицы свойств тайла
 * (props & 1 = категория 0 или 4, см. ai_type08/level_tile_attr_at выше).
 * Приземление (file 0x125D5): ТОЛЬКО set_motion(0) (обнуление скорости) —
 * в оригинале НЕТ привязки Y к сетке тайлов, предмет замирает там, где
 * упал в тот момент. */
static void ai_falling_bonus(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    /* anim_phase индексирует sprite cycle (39-50 — все опции, перебираем
     * для отладки. Реальный оригинал хранит конкретный sprite_id в misc_state). */
    o->anim_phase = (frame >> 4) & 7;
    if (o->custom == 1) return;  /* приземлился */
    /* Падаем 2 px/frame. */
    o->y += 2;
    int cat = level_tile_attr_at(o->x, o->y + 10);
    if (cat == 0 || cat == 4) {
        o->custom = 1;              /* set_motion(0) — байт-точно */
    }
    /* Страховка ТОЛЬКО от вылета за нижний край карты (порт, не из
     * оригинала). Раньше здесь стояло «feet >= physics_get_ground_y() →
     * положить на этот пол», и на многоярусных уровнях предмет, летящий
     * мимо площадок, ПОДТЯГИВАЛСЯ ВВЕРХ к расчётному «основному» полу: на
     * LEVEL4 это y=373, выше Карнова (его ноги на 480), из-за чего свиток
     * за боссом-«гусеницей» оказывался недосягаем и уровень нельзя было
     * закончить. */
    else {
        int h = o->cur_h > 0 ? o->cur_h : obj_height(o);
        if (o->y + h >= (int)level_num_map_rows * TILE_H) {
            o->y = (int)level_num_map_rows * TILE_H - h;
            o->custom = 1;
        }
    }
}

/* Type 19 (декомпилирован полностью, file 0x14C20..0x14C86, cs:0x11D6:
 * 0x16C0..0x1726 — единственная своя функция, следующий адрес в сегменте
 * это уже handler типа 23, cs:0x1348:0x0007≡файл 0x14C87). INIT: active
 * (+0xDB) = 0, num_bboxes = 1, box0.sprite = 0xAC (172) — ОДИН РАЗ,
 * tick_period = 1000, затем recompute-bbox(self) + вызов ТОГО ЖЕ
 * boss_helper (cs:0x11D6:0x15D6 = file 0x14B36..0x14B73), что и у типа 9
 * (см. комментарий ai_walking_minion). THINK каждый тик зовёт ровно тот
 * же boss_helper и БОЛЬШЕ НИЧЕГО — box0.sprite нигде не переписывается
 * (нет второго кадра), x_world/y_world объект тоже не трогает сам.
 * hurt(state4) и delete(state6) — оба просто ai_result|=1 (без вспышки,
 * без очков в этом обработчике). Т.е. это ПОЛНОСТЬЮ статичный единственный
 * спрайт: прежняя «очень медленная флипа» была домыслом — в листинге
 * второго кадра нет вообще (манифест SPRITES.EGA тоже даёт для ai_type=19
 * только один спрайт #172, так что видимого различия эта правка не даёт,
 * но код не должен изображать несуществующую анимацию). */
static void ai_slow_helper(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input; (void)frame;
    o->anim_phase = 0;                      /* единственный кадр — #172 */
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

/* Снаряд врага — порт cs:0x11D6:0x02A3 (его ставит spawn_projectile).
 *
 * INIT: state_list = DS:0x1784, tick_period = 2, obj_type = 3, hp = 10,
 * num_bboxes = 1, box0.hitflag = 1, а АНИМ-СОСТОЯНИЕ берётся из +0xE9 —
 * то есть «kind», переданный стрелявшим. Дальше по этому же kind (проверено
 * повторной декомпиляцией file 0x1380x — таблицы @0x31C/@0x388, у прежней
 * версии этого комментария kind 7/8/9/10 были перепутаны местами):
 *   kind 8/9/10 — obj_type меняется на 1 (снаряд становится «врагом» для
 *                 pair-loop, может ранить); ДОПОЛНИТЕЛЬНО kind 10 —
 *                 вертикальная скорость принудительно вниз (vy = |vy|).
 * THINK — правила гибели тоже по kind:
 *   kind 7  — жив, пока тайл в его точке — категория 2 («sky/passable»,
 *             см. level.c); иначе зовётся cs:0x11D6:0x021A (разрыв о
 *             препятствие) и снаряд удаляется;
 *   kind 10 — гибнет, как только тайл под ним — категория 9 (вода);
 *   kind 8/9 и прочие (1,4,5,6) — живут до frame_ctr > 0x28 (40 кадров).
 * Прежняя реализация просто летела до края карты. */
static void ai_enemy_shot(ai_obj_t *o, u16 frame, u8 in)
{
    (void)in;
    /* Кадры снаряда — StateScript DS:0x1784, ПОЛНОСТЬЮ вычитан по формату
     * StateScript/AnimScript (см. extract_anim_tables.py), все 11 состояний
     * (проверка code-review PSEUDO-TYPES 2026-09, дамп таблицы напрямую):
     *   state0(kind0)=2,3            state1(kind1)=1,2,3,2
     *   state2(kind2)=9 (статика)    state3(kind3)=34,36
     *   state4(kind4, наведённый снаряд типа 14)=10,11,12,13
     *   state5(kind5)=14,14,14,15,15,15 (влево)
     *   state6(kind6)=16,16,16,17,17,17 (вправо)
     *   state7(kind7, «огненное дыхание» типа 38)=10,11,12,13 (тот же
     *     набор, что и kind4)
     *   state8=[19] (статика)  state9=[20] (статика)  state10=[283] (статика)
     * Индекс кадра — think-тик (раз в tick_period=2 кадра, тот же счётчик
     * для всех состояний) по модулю длины ТЕКУЩЕЙ таблицы.
     * ИСПРАВЛЕНО: раньше kind 0 и kind 2 (оба реально спаунятся — тип 29,
     * тип 47, наземный турельный «горелка» и т.п.) попадали в generic
     * fallback «спрайт 14-17 по знаку vxq», который на самом деле
     * принадлежит kind 5/6 — то есть эти снаряды рисовались чужими
     * кадрами. kind 5/6 сами тоже раньше шли через тот же fallback: та же
     * пара спрайтов, но с неверным периодом смены кадра (4 тика вместо
     * правильных 3-кратных повторов из таблицы). */
    static const u16 K0[2] = { 2, 3 };
    static const u16 K1[4] = { 1, 2, 3, 2 };
    static const u16 K3[2] = { 34, 36 };
    static const u16 K4[4] = { 10, 11, 12, 13 };   /* kind 4 и kind 7 */
    static const u16 K5[6] = { 14, 14, 14, 15, 15, 15 };
    static const u16 K6[6] = { 16, 16, 16, 17, 17, 17 };
    u16 tick = (u16)(frame >> 1);
    o->anim_phase = tick;
    switch (o->custom) {
    case 1:  o->item_sprite = K1[tick & 3]; break;
    case 2:  o->item_sprite = 9; break;
    case 3:  o->item_sprite = K3[tick & 1]; break;
    case 4:
    case 7:  o->item_sprite = K4[tick & 3]; break;
    case 5:  o->item_sprite = K5[tick % 6]; break;
    case 6:  o->item_sprite = K6[tick % 6]; break;
    case 8:  o->item_sprite = 19; break;
    case 9:  o->item_sprite = 20; break;
    case 10: o->item_sprite = 283; break;
    default: o->item_sprite = K0[tick & 1]; break;   /* kind 0 */
    }

    int cat = level_tile_attr_at(o->x, o->y);
    switch (o->custom) {
    case 7:                                  /* разрывается о препятствие */
        if (cat != 2) { o->alive = false; return; }
        break;
    case 10:                                 /* гибнет в воде */
        if (cat == 9) { o->alive = false; return; }
        break;
    default:                                 /* 1,4,5,6,8,9 */
        if (o->st_ctr > 0x28) { o->alive = false; return; }   /* 40 кадров */
        break;
    }

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
            /* Снаряд — тип 12 оригинала (spawn cs:0x11D6:0x788, file 0x13CE8).
             * Ребёнок наследует x_world РОДИТЕЛЯ без сдвига и Y = y_world −
             * [DS:0xFC1] (=16, file 0x13E7D: `sub ax,[0xfc1]`), а не y+6 —
             * прежнее значение было визуальной подгонкой, не байтом. Дочерний
             * обработчик (уже портирован как TYPE_ROCK_PROJECTILE) при
             * child.frame_idx==dir берёт vx=±3.0 (file 0x13E8B/0x13E92),
             * а НЕ ±6.0 — тут раньше был баг вдвое завышенной скорости. */
            spawn_rock(o->x, o->y - 16, dir ? 3 : -3, frame);
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

/* ── Тип 10 — демон на облаке (handler cs:0x11D6:0x0413, file 0x13973) ─
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

/* ── Тип 23 — летящий шар (handler cs:0x1348:0x0007, file 0x14C87) ────
 * (прежний адрес cs:0x11D6:0x0008/file 0x13508 в комментарии был неверен —
 * настоящий обработчик по таблице DS:0x10FE лежит в сегменте 0x1348).
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

/* ── Тип 8 — падающий «с неба» (cs:0xFFA:0x139F, file 0x12B3F) ────────
 *
 * Это не абстрактный «спаунер», а сам объект: INIT ставит obj_type = 1
 * (враг), box0.hitflag = 0, num_bboxes = 1 и СРАЗУ размещает себя
 * относительно игрока: x = player.x + (rand & 0x3F) − 10,
 * y = player.y − 100 − (rand & 0x3F), state_list = DS:0x171C.
 * Подфазы:
 *   0/1 — падение: аним 0, vy = +1.0, tick_period = 3. Если тайл под
 *         точкой (x, y + 5) имеет props & 1 (пол) — подфаза 2. Если
 *         объект провалился ниже игрока больше чем на 0x82 — он
 *         ПЕРЕЗАПУСКАЕТСЯ сверху: x += rand & 0x1F, y = player_y − 0x8C;
 *   2/3 — приземлился: аним 1, vy = 0, счётчик кадров сброшен.
 * ВАЖНО: запись типа 8 есть в КАЖДОМ уровне, но у всех она нулевая —
 * cur = (0,0), range = (0,0), а условие спауна строгое (|dx| < range_x),
 * поэтому по триггеру такой объект не рождается НИКОГДА. Его создаёт код:
 * спаунер cs:0xFFA:0x1585 в цикле выделяет N объектов, СВЯЗЫВАЯ их в
 * цепочку через +0xF1/+0xF3 (каждый указывает на предыдущего), каждому
 * ставит обработчик cs:0xFFA:0x139F, а голову цепи кладёт в
 * [DS:0xD880/0xD882]. Обратная операция cs:0xFFA:0x1561 идёт по цепочке
 * от головы и ставит всем state_byte = 4 — это и есть «убить sky-цепь».
 *
 * Кадры типа 8 — #72,73,74,73 (падение) и #75 (приземлился), то есть это
 * НЕ «два орла» из начала LEVEL1 (#147/148): их порт спаунит своим
 * драйвером ai_sky_tick, и это отдельная механика. Обработчик здесь
 * перенесён верно; кто и с каким N зовёт спаунер — пока не найдено. */
static void ai_type08(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, 0);
        obj_phys_begin(o);
        o->x = physics_get_left_x() + (int)(rng_next() & 0x3F) * 2 - 20;
        o->y = physics_get_top_y() - 100 - (int)(rng_next() & 0x3F);
        obj_phys_begin(o);
        o->st_misc = 0;
        return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = 0;
        o->vyq = Q_1_0;
        /* fallthrough */
    case 1: {
        int cat = level_tile_attr_at(o->x, o->y + 5);
        if (cat == 0 || cat == 4) { o->st_misc = 2; break; }   /* props & 1 */
        if (o->y > physics_get_top_y() + 0x82) {               /* улетел вниз */
            o->x += (int)(rng_next() & 0x1F) * 2;
            o->y  = physics_get_top_y() - 0x8C;
            obj_phys_begin(o);
        }
        break;
    }
    case 2: {
        o->pending_state = 1;
        o->vyq = 0;
        o->st_ctr = 0;
        /* ИСПРАВЛЕНО (2026-09): раньше здесь только гасили vy, БЕЗ привязки
         * Y к земле — объект «закапывался» (верх спрайта на уровне земли
         * вместо низа). Оригинал в этот момент вызывает
         * snap_to_ground(obj, sprite=#75, category=0) — cs:0xD4B:0x19F,
         * file 0xEE4F (тот же хелпер, что и у obj_snap_ground() выше по
         * файлу): y_world = row*TILE_H − высота ИМЕННО спрайта #75
         * (приземлившийся кадр), а не текущего/произвольного. */
        const sprite_t *ls = sprite_find(75);
        int h = ls ? (int)ls->h : 24;
        int row = level_ground_row_below(o->x + 6, o->y / TILE_H, 0x0001);
        if (row >= 0) {
            o->y = row * TILE_H - h;
            if (o->phys) o->yq = (s32)o->y << 16;   /* иначе интегратор на
                                                      * следующем кадре вернёт
                                                      * старое (незаснэпленное)
                                                      * значение из yq */
        }
        break;
    }
    case 3: {
        /* ИСПРАВЛЕНО (2026-09): подфаза «устойчиво приземлился» отсутствовала
         * целиком — st_misc застревал на 3 навсегда (см. `|= 1` выше) и
         * каждый тик проваливался в default, поэтому объект молчал после
         * посадки. Портирован реальный алгоритм (file 0x12CB0-0x12CDE +
         * хелпер-триплет file 0x12AE0):
         *   если st_ctr(+0xC7) > 100 — три снаряда из
         *   (self.x_world+2, self.y_world + i*2 + 2) в
         *   (player.x_world, player.y_world + jitter − 5), jitter
         *   накапливается между тремя выстрелами (+= rand()&0x1F каждый
         *   раз) — нарастающий вертикальный разброс; скорость Q16.16
         *   = (speed_hi=1, speed_lo=0x4000) = 1.25, нормировано по
         *   Манхэттену — та же схема, что уже верифицирована для типов
         *   14/20/32. После триплета st_ctr = −30 (file 0x12CC4).
         *   Независимо от триплета — дистанционный гейт cs:0xD4B:0x105
         *   (тот же хелпер, что и у скрытого предмета типа 6: d=|dx|+|dy|):
         *   если d < 15 — st_ctr += 100 (file 0x12CD9), т.е. пока игрок
         *   слишком близко, следующая попытка откладывается ещё на 100
         *   кадров (сам st_ctr тикает каждый кадр отдельно от этого
         *   обработчика — обычный o->st_ctr порта).
         * kind/спрайт снаряда (+ поле bp+0x12 оригинала) байтом не
         *   подтверждён — используется kind=0 (générique #2/#3), честно
         *   не точный, но не влияющий на траекторию/урон. */
        if (o->st_ctr > 100) {
            int jitter = 0;
            for (int i = 0; i < 3; ++i) {
                int slot = alloc_obj_slot();
                if (slot >= 0) {
                    int fx = o->x + 4, fy = o->y + i * 2 + 2;
                    int tx = player_units() - fx / 2;
                    int ty = physics_get_top_y() + jitter - 5 - fy;
                    int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
                    if (len < 1) len = 1;
                    ai_obj_t *b = &g_obj[slot];
                    *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                                      .subtype = 3, .x = fx, .y = fy, .hp = 10,
                                      .level_idx = -1, .facing = (tx >= 0) ? 1 : -1,
                                      .custom = 0 };
                    obj_phys_begin(b);
                    b->vxq = ((s32)tx * QV(1, 0x4000)) / len;
                    b->vyq = ((s32)ty * QV(1, 0x4000)) / len;
                }
                jitter += (int)(rng_next() & 0x1F);
            }
            o->st_ctr = -30;
        }
        {
            int d  = player_units() - obj_units(o);
            int dy = physics_get_top_y() - o->y;
            int dist = (d < 0 ? -d : d) + (dy < 0 ? -dy : dy);
            if (dist < 15) o->st_ctr += 100;
        }
        break;
    }
    default: break;
    }
}

/* ── Тип 7 — СВИТОК конца уровня (cs:0xFFA:0x1032, file 0x127D2) ──────
 *
 * Это не «невидимый триггер», как значилось в порту: INIT ставит
 * num_bboxes = 0 (не рисуется), box0.sprite = 0x90 (#144 — тот самый
 * свиток), hitflag = 0 и запоминает объект в [DS:0xD94F/0xD951], чтобы
 * до него могли дотянуться извне.
 *
 * THINK: пока phase(+0xED) == 0 — ничего (свиток «не взведён»; взводит
 * его гибель goal-объекта). Взведённый ждёт, пока игрок не подойдёт
 * ближе 0x50, и тогда: play/state 9 через cs:0xE62:0x1E7(9) — то самое
 * состояние, при котором функция урона (file 0xF81E) перестаёт бить
 * игрока, — num_bboxes = 1 (свиток ПОЯВЛЯЕТСЯ), vy = +2.0 (падает) и
 * подмена обработчика игрока на cs:0xE62:0x0CF6 (авто-секвенция конца
 * уровня). Порт до этого спавнил вместо него выдуманный свиток «с неба».
 */
static void ai_type07(ai_obj_t *o, u16 frame, u8 in)
{
    (void)in;
    if (!o->use_anim) {
        anim_begin(o, 1, 0);
        o->invisible   = true;          /* num_bboxes = 0 */
        o->item_sprite = 0x90;          /* #144 — свиток */
        /* +0xED — «взведён». Обычно его поднимает гибель goal-объекта
         * (arm_real_scroll), но запись свитка может заспауниться и ПОСЛЕ
         * этой гибели: бомбой/Clapper'ом (ai_clear_enemies) босса убивают
         * издалека, а на LEVEL9 goal-запись (idx 142) вообще стоит в
         * sorted-таблице ПЕРЕД свитком (idx 143). Безусловный ноль здесь
         * обезоруживал свиток навсегда, а real_scroll глушил запасной
         * синтетический путь — уровень становился непроходимым. */
        o->st_phase    = g_boss_dead ? 1 : 0;
        return;
    }
    if (!o->st_phase) return;           /* boss ещё жив */
    int dx = player_units() - obj_units(o);
    if ((dx < 0 ? -dx : dx) >= 0x50) return;
    if (o->invisible) {                 /* появление */
        o->invisible = false;
        o->is_scroll = true;
        o->vy = 2;                      /* vy = +2.0 — падает */
        o->spawn_frame = frame;         /* отсчёт страховки — с появления */
        physics_set_invuln(true);       /* player_state = 9 */
    }
    /* Падение до поверхности — как у падающего бонуса. */
    if (o->custom != 1) {
        o->y += 2;
        int h = (o->cur_h > 0) ? o->cur_h : obj_height(o);
        if (level_solid_at(o->x, o->y + h + 1)) {
            o->y = ((o->y + h) / TILE_H) * TILE_H - h;
            o->custom = 1;
        } else if (o->y + h >= (int)level_num_map_rows * TILE_H) {
            /* Аварийный пол по нижней кромке карты: без него свиток над
             * пропастью падает из мира навсегда и уровень не закончить.
             * Дальше его подберёт 400-кадровая страховка в ai_tick. */
            o->y = (int)level_num_map_rows * TILE_H - h;
            o->custom = 1;
        }
    }
}

/* ── Тип 29 — прыгающий стрелок (cs:0x1348:0x1531, file 0x161B1) ──────
 *
 * INIT: tick_period = 8, подфаза (+0xE9) = 0, state_list = DS:0x1AF0,
 * постановка на землю со спрайтом 0xD3 (#211).
 * Подфазы:
 *   0/1 — дремлет (аним 0), пока расстояние до игрока не меньше 0x32;
 *   2/3 — прыжок: аним 1, vx = +1.0, vy = −2.5, ay = +0.129 (0x2100);
 *         ждём, пока vy не станет положительной, и смотрим тайл под
 *         ногами — по его категории идёт разбор посадки. Таблицы
 *         @0x1659/@0x1734 (file 0x162D9/0x163B4 — по 8 записей на
 *         категории 0..7) приземляют ТОЛЬКО на 0, 4, 7; на 1, 2, 3, 5,
 *         6 продолжает падать. Прежняя проверка `== 2` была записана
 *         с точностью до наоборот (та же ошибка, что нашлась у типов
 *         26/40 в этом же файле).
 *   4/5 — приземлился: аним 2 и выстрел в игрока со скоростью 2.0,
 *         затем подфаза 6;
 *   6/7 — снова подскок: аним 3, vy = −2.5, приземление по той же
 *         таблице {0,4,7}.
 *
 * ИСПРАВЛЕНО (2026-09, выстрел): полная передизассемблировка вызова
 * spawn_projectile в подфазе 4 (пуши file 0x162F5..0x16318, сам call
 * cs:0x1348:0x1699 → file 0x16319) даёт аргументы (глубина→мелко):
 * flag=1, speed_hi=2, speed_lo=0,
 * kind=0, target_y=player.y_world, target_x=player.x_world, source_y=
 * self.y_world, source_x=self.x_world. flag=1 у cs:0x11D6:0x139 —
 * ТОТ ЖЕ override, что у типа 22 (см. его комментарий и урок в шапке
 * аудита): target_y принудительно заменяется на source_y ДО нормировки,
 * так что реальная вертикальная составляющая всегда 0 — снаряд летит
 * СТРОГО ГОРИЗОНТАЛЬНО со скоростью 2.0, а не «наведённый». Прежняя
 * реализация считала полный Манхэттен-вектор к игроку (с реальным
 * physics_get_top_y()) — бралась «по смыслу» из факта, что вызов идёт
 * через общий аиминг-хелпер, без проверки самого флага (в точности
 * ловушка типа 22 из шапки аудита). Заменено на flat-порт через
 * spawn_enemy_shot(), как у типа 15. */
static void ai_type29(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 8, 0);
        obj_phys_begin(o);
        o->st_misc = 0;
        obj_snap_ground(o);
        return;
    }
    int dxp = player_units() - obj_units(o);
    int dyp = physics_get_top_y() - o->y;
    int d = (dxp < 0 ? -dxp : dxp) + (dyp < 0 ? -dyp : dyp);

    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = 0;
        /* fallthrough */
    case 1:
        if (d < 0x32) o->st_misc = 2;
        break;
    case 2:
        o->pending_state = 1;
        o->vxq = Q_1_0;
        o->vyq = (s32)0xFFFD8000;            /* −2.5 */
        o->ayq = QV(0, 0x2100);
        /* fallthrough */
    case 3: {
        if (o->vyq <= 0) break;
        int cat = level_tile_attr_at(o->x, o->y + obj_height(o));
        if (!(cat == 0 || cat == 4 || cat == 7)) break;
        o->st_misc = 4;
        break;
    }
    case 4:
        o->pending_state = 2;
        o->vyq = 0; o->ayq = 0;
        obj_snap_ground(o);
        /* flag=1 у spawn_projectile — строго горизонтальный выстрел
         * (см. комментарий к обработчику), НЕ наведённый. */
        spawn_enemy_shot(o->x, o->y, dxp > 0, Q_2_0, 0);
        o->st_misc = 6;
        break;
    case 6:
        o->pending_state = 3;
        o->vyq = (s32)0xFFFD8000;            /* снова подскок */
        o->ayq = QV(0, 0x2100);
        /* fallthrough */
    case 7: {
        if (o->vyq <= 0) break;
        int cat = level_tile_attr_at(o->x, o->y + obj_height(o));
        if (cat == 0 || cat == 4 || cat == 7) o->st_misc = 4;
        break;
    }
    default: break;
    }
}

/* ── Тип 52 — «пробуждающийся» блок (cs:0x175E:0x0203, file 0x18FE3) ──
 *
 * INIT: подфаза (+0xE9) = 0, tick_period = 3, y_world выравнивается по
 * строке тайлов (y -= y % 10), state_list = DS:0x1E70, вызов
 * cs:0x175E:0x01A3(1).
 * Подфазы:
 *   0/1 — покой: аним-состояние 0, box0.hitflag = 1 (уязвим);
 *   2/3 — «пробуждён»: аним-состояние 1 или 2 в зависимости от того, с
 *         какой стороны игрок (сравнение с x + 8), box0.hitflag = 0
 *         (неуязвим), кадр ставится в 1, а счётчик +0xEB увеличивается
 *         каждый раз, когда индекс кадра обнуляется; после трёх таких
 *         оборотов анимации — возврат в подфазу 0.
 *
 * Пробуждение — это state 5 обработчика (НЕ 4!), file 0x1910B
 * (cs:0x175E:0x32B), таблица состояний @0x21B: индексы 0/1→INIT,
 * 2→THINK, 3/4→пусто (retf), 5→код ниже, 6→удаление. Кто ставит
 * state_byte=5 — отдельный общий диспетчер пар cs:0xD4B (file
 * 0xFAB3+, список DS:0x2722, дизассемблирован построчно): для пары
 * (A касается B) он смотрит A.obj_type и затем B.obj_type по вложенным
 * таблицам file 0xFB3A/0xFC06 и ТОЛЬКО для B.obj_type∈{7,9} ставит
 * B.state_byte=5 (при A=игрок(0) или A=файербол(2)). Сам код state5:
 *   ax = [+0xDF] (индекс партнёра из pair-loop, file 0x2D72);
 *   если partner.obj_type != 0 (не игрок) — выход, блок спит дальше;
 *   если player.y_world >= self.y_world (игрок НЕ выше блока) — тоже
 *   выход; иначе misc(+0xE9) = 2 — пробуждение.
 * То есть будит блок СТРОГО касание ИГРОКОМ (не любым объектом, не
 * файерболом — тот проходит только до B.obj_type-диспетчера, а
 * внутренний recheck partner.obj_type==0 его отсеивает) И ТОЛЬКО когда
 * игрок находится НАД блоком (обычно — приземлился сверху). Прежний
 * порт будил от hit_flag БЕЗ этих двух условий (любой партнёр, любая
 * сторона) — это неверно. Игрок не входит в g_obj[], поэтому «касание
 * игроком» помечается отдельно в ai_check_player_hit (HIT_IDX_PLAYER),
 * а условие «игрок выше» проверяется здесь по живым координатам. */
static void ai_type52(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, 0);
        o->st_misc = 0;
        o->y -= o->y % TILE_H;
        return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = 0;
        o->custom = 1;                       /* уязвим в покое */
        if (o->hit_flag && o->hit_idx == HIT_IDX_PLAYER &&
            physics_get_top_y() < o->y)
            o->st_misc = 2;
        break;
    case 2:
        o->pending_state = (player_units() > obj_units(o) + 8) ? 2 : 1;
        o->st_aux = 0;                       /* +0xEB */
        o->custom = 0;                       /* неуязвим «пробуждённым» */
        o->frame_index = 1;
        /* fallthrough */
    case 3:
        if (o->frame_index == 0) ++o->st_aux;
        if (o->st_aux > 2) o->st_misc = 0;   /* три оборота — назад в покой */
        break;
    default: break;
    }
}

/* ── Тип 54 — стационарный плевун (cs:0x17B6:0x01DB, file 0x1953B) ────
 *
 * INIT: tick_period = 4, подфаза (+0xE9) = 0, state_list = DS:0x1EB0,
 * постановка на землю со спрайтом 0x140 (#320).
 * Подфазы:
 *   0/1 — покой: аним 0, счётчик кадров сброшен; выстрел разрешён, лишь
 *         когда прошло больше 0x6E кадров И расстояние до игрока меньше
 *         0x78 — тогда подфаза 2;
 *   2/3 — выстрел: аним 1, счётчик сброшен, горизонтальный снаряд
 *         влево из (x, y + 0x0F) со скоростью 3.0; через 0x19 кадров
 *         возврат в покой;
 *   4   — получил урон: аним 2, obj_type = 9, box0.hitflag = 0 —
 *         объект превращается в неуязвимую декорацию.
 * Сам плевун неподвижен.
 * Выстрел передизассемблирован полностью (file 0x19628 →
 * cs:0x11D6:0x139): flag=1 (принудительно горизонтальный), speed=3.0,
 * kind=2 (было 1 — чужой спрайт {1,2,3,2} вместо статичного #9),
 * target_x = self.x_world−10 (фиксированная, БЕЗ игрока — отсюда
 * «всегда влево», не наведение), source=(x_world, y_world+0x0F) —
 * подтверждено без изменений, кроме kind. */
static void ai_type54(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 0);
        o->st_misc = 0;
        obj_snap_ground(o);
        return;
    }
    int dxp = player_units() - obj_units(o);
    int dyp = physics_get_top_y() - o->y;
    int d = (dxp < 0 ? -dxp : dxp) + (dyp < 0 ? -dyp : dyp);

    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = 0;
        o->st_ctr = 0;
        /* fallthrough */
    case 1:
        if (o->st_ctr <= 0x6E) break;
        if (d >= 0x78) break;
        o->st_misc = 2;
        break;
    case 2:
        o->pending_state = 1;
        o->st_ctr = 0;
        spawn_enemy_shot(o->x, o->y + 0x0F, 0, QV(3, 0), 2);   /* влево, 3.0 */
        /* fallthrough */
    case 3:
        if (o->st_ctr > 0x19) o->st_misc = 0;
        break;
    default: break;
    }
}

/* ── Тип 37 — стреляющий ползун (cs:0x151A:0x0C31, file 0x175D1) ──────
 *
 * INIT: tick_period = 7, подфаза (+0xE9) = 0, +0xEB = 0,
 * phase(+0xED) = СНИМОК hp, state_list = DS:0x1C86, постановка на землю
 * по центру спрайта 0xFB (#251).
 * Порог 0x46 (file 0x17651/0x1775B): НЕ «70 суммарного урона с момента
 * спауна» — phase(+0xED) перезаписывается hit_points'ом В КОНЦЕ КАЖДОГО
 * тика (file 0x1763C, общий хвост THINK), поэтому проверка в начале
 * THINK сравнивает hp с его же значением НА КОНЕЦ ПРЕДЫДУЩЕГО тика —
 * это защита от разового «взрывного» урона (>70 за один тик, т.е. за
 * tick_period=7 кадров — бомба/несколько попаданий разом), а не общий
 * счётчик живучести. Обычный выстрел (10 урона) эту защиту практически
 * никогда не задевает; убивает объект штатный hp<=0. Прежний порт брал
 * снимок ОДИН РАЗ при спауне и никогда не обновлял — это накапливало
 * урон с рождения и убивало объект принудительно намного раньше, чем в
 * оригинале (после ~7 обычных попаданий вместо «почти никогда»).
 * Подфазы:
 *   0/1 — дремлет: аним 0, box0.hitflag = 0 (НЕУЯЗВИМ); просыпается,
 *         когда расстояние до игрока меньше 0x55;
 *   2/3 — бой: аним 1, hitflag = 1, vx = −0.5. Каждый 4-й кадр
 *         (`test [0xCDEA], 4`), пока счётчик очереди < 6: выстрел через
 *         cs:0x151A:0xB18 (play_sound(2) + снаряд), ++счётчик, сброс
 *         frame_ctr и, если hp < 0xFA, hp += 0x0A — то есть за каждый
 *         выстрел враг ЛЕЧИТСЯ на 10. Отстреляв шесть, ждёт 0x14 кадров
 *         и обнуляет счётчик. Догнав игрока (self.x <= player.x),
 *         останавливается и замирает (active = 0).
 *
 * Скорость снаряда (byte-verified, file 0x174B8=cs:0x151A:0xB18):
 *   хелпер 0xB18 зовёт play_sound(2), затем spawn_projectile(
 *     x0=self.x_world, y0=self.y_world+7,
 *     x1=player.x_world, y1=self.box0.y1−4,
 *     kind=4, speed_hi=2, speed_lo=0, flag=0)  — `mov dx,2 / xor ax,ax`
 *     прямо перед четырьмя position-пушами (file 0x174B2/0x174B7), т.е.
 *     QV(2,0) = 2.0 — ровно текущая константа Q_2_0.
 *   flag=0 ⇒ в spawn_projectile (cs:0x11D6:0x139, file 0x13699+0x1A4)
 *     y1 НЕ подменяется (в отличие от типа 22, где flag=1 обнуляет
 *     вертикаль) — цель реально (player.x, self.box0.y1−4), т.е. снаряд
 *     нормализуется Манхэттен-хелпером (cs:0x11D6:0x006) по вектору к
 *     игроку с небольшой вертикальной составляющей от собственной
 *     геометрии хитбокса. Порт (`spawn_enemy_shot`, ниже) упрощает это
 *     до строго горизонтального выстрела (vy=0) — величина 2.0
 *     подтверждена побайтово, но ТРАЕКТОРИЯ не байт-точна (малая
 *     вертикальная компонента не переносится); не подтверждено, что
 *     этим можно пренебречь во всех случаях. */
static void ai_type37(ai_obj_t *o, u16 frame, u8 in)
{
    (void)in;
    if (!o->use_anim) {
        anim_begin(o, 7, 0);
        obj_phys_begin(o);
        o->st_misc  = 0;
        o->st_aux   = 0;                     /* +0xEB — счётчик очереди */
        o->st_phase = o->hp;                 /* +0xED — снимок hp */
        o->custom   = 0;                     /* box0.hitflag */
        obj_snap_ground(o);
        return;
    }
    if (o->st_phase - o->hp > 0x46) o->hp = -20;

    int dxp = player_units() - obj_units(o);
    int dyp = physics_get_top_y() - o->y;
    int d = (dxp < 0 ? -dxp : dxp) + (dyp < 0 ? -dyp : dyp);

    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = 0;
        o->custom = 0;                       /* неуязвим, пока дремлет */
        /* fallthrough */
    case 1:
        if (d < 0x55) o->st_misc = 2;
        break;
    case 2:
        o->pending_state = 1;
        o->custom = 1;                       /* теперь уязвим */
        o->vxq = (s32)0xFFFF8000;            /* −0.5 */
        /* fallthrough */
    case 3:
        if ((frame & 4) && o->st_aux < 6) {
            spawn_enemy_shot(o->x, o->y + obj_height(o), (dxp > 0), Q_2_0, 1);
            audio_play_sfx(2);
            ++o->st_aux;
            o->st_ctr = 0;
            if (o->hp < 0xFA) o->hp += 0x0A;  /* +10 hp за выстрел */
        }
        if (o->st_aux == 6 && o->st_ctr > 0x14) o->st_aux = 0;
        if (obj_units(o) <= player_units()) {
            o->vxq = 0;
            o->st_aux = -0x19;
        }
        break;
    default: break;
    }
    o->st_phase = o->hp;   /* file 0x1775B: снимок обновляется каждый тик */
}

/* ── Тип 49 — «щит» босса (handler cs:0x170C:0x03C5, file 0x18C85) ────
 *
 * INIT: подфаза (+0xE9) = 0, +0xEB = СНИМОК hp, active = 0,
 * num_bboxes = 1, box0.dx = dy = 0, а спрайт зависит от НОМЕРА УРОВНЯ
 * ([DS:0xD8AA]): 0x131 (#305) на LEVEL6, иначе 0x130 (#304); phase
 * (+0xED) = 1 ТОЛЬКО на LEVEL9 и правее x = 0x44C (file 0x18CDF,
 * cs:0x170C:0x3FF..0x41D).
 * THINK subphase 1 (file 0x18D63): каждый тик hp ВОССТАНАВЛИВАЕТСЯ из
 * снимка — щит практически неуязвим для выстрелов. Дальше (file
 * 0x18D71, `cmp [+0xed],0; je <конец>`) весь нижеследующий блок
 * ЦЕЛИКОМ пропускается, если phase(+0xED) == 0 — то есть проверка ниже
 * реально исполняется ТОЛЬКО у той единственной LEVEL9-записи; у щита
 * LEVEL6-босса и у левого щита LEVEL9 код обрывается здесь на каждом
 * тике, и они этим путём вообще никогда не гибнут.
 * Если phase != 0: смотрим +0xDD («задели в этом кадре», file 0x18D79)
 * и, если да, читаем +0xDF (индекс объекта, с которым пересеклись
 * боксы — pair-loop, file 0x2D72) и проверяем obj_type ЭТОГО объекта
 * (file 0x18D81..0x18D92, `[hit_idx*245+0x2909]==0`). Полный скан
 * бинаря подтверждает: НИ ОДНА ветка гибели/удаления враг НЕ пишет
 * obj_type=0 — то есть «0» здесь не «партнёр умер», а «партнёр — это
 * КАРНОВ» (obj_type 0 = игрок, см. таблицу полей ActiveObject). Значит
 * LEVEL9-правый щит гибнет от ПРОСТОГО КАСАНИЯ игроком (die_puff НЕ
 * играется — сразу ai_result |= 1), а не от смерти какого-то партнёра.
 * Игрок не входит в g_obj[], поэтому в порту это отдельная проверка
 * пересечения боксов в ai_check_player_hit (сентинел HIT_IDX_PLAYER). */
static void ai_type49(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 1, 0);
        o->st_misc = 0;
        o->st_aux  = o->hp;                       /* +0xEB — снимок hp */
        o->st_phase = (g_ai_level_num == 9 && obj_units(o) > 0x44C);
        o->item_sprite = (g_ai_level_num == 6) ? 305 : 304;
        return;
    }
    o->hp = o->st_aux;                            /* hp восстанавливается */
    if (o->st_phase) {
        /* Только эта запись реально проверяет касание игроком. */
        if (o->hit_flag && o->hit_idx == HIT_IDX_PLAYER) { o->alive = false; return; }
    } else {
        /* В оригинале эти щиты НЕ гибнут этим путём никогда (см. выше);
         * ai_boss_dead() — защита порта, чтобы они не оставались
         * навсегда препятствием после смерти босса уровня. */
        if (ai_boss_dead()) o->alive = false;
    }
}

/* ── Тип 55 — патрульный (handler cs:0x17EC:0x0028, file 0x196E8) ─────
 *
 * INIT: tick_period = 4, +0xEB = СНИМОК стартового hp, подфаза
 * (+0xE9) = 2 (то есть начинает с «отдыха»), постановка на землю со
 * спрайтом 0x142 (#322), state_list = DS:0x1EF4.
 * Подфазы:
 *   0/1 — ходьба: аним-состояние = phase(+0xED), vx = ±0.5 по нему,
 *         box0.hitflag = 1 (уязвим). Проба края: тайл в точке
 *         (x + (phase ? +0x14 : −0x0A), нижняя кромка); пока он
 *         категории 0 — идём дальше, иначе phase ^= 1 (разворот) и
 *         подфаза 0 переигрывается;
 *   2/3 — отдых: аним-состояние 2, vx = 0, hitflag = 0 (НЕУЯЗВИМ),
 *         счётчик кадров сброшен; через 0x3C кадров зовётся
 *         cs:0x17EC:0x0001, который выбирает СЛУЧАЙНОЕ направление
 *         (phase = rand & 1), возвращает подфазу 0 и ВОССТАНАВЛИВАЕТ
 *         hp из снимка — то есть за время отдыха враг «лечится». */
static void ai_type55(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 2);
        obj_phys_begin(o);
        o->st_aux  = o->hp;              /* +0xEB — снимок hp */
        o->st_misc = 2;                  /* начинает с отдыха */
        o->custom  = 0;                  /* box0.hitflag */
        obj_snap_ground(o);
        return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = o->st_phase;
        o->vxq = o->st_phase ? QV(0, 0x8000) : (s32)0xFFFF8000;   /* ±0.5 */
        o->custom = 1;                   /* уязвим на ходу */
        /* fallthrough */
    case 1: {
        int probe_x = o->x + (o->st_phase ? 40 : -20);   /* ±0x14/0x0A ед. */
        if (level_tile_attr_at(probe_x, o->y + obj_height(o)) == 0) break;
        o->st_phase ^= 1;                /* край площадки — разворот */
        o->st_misc = 0;
        break;
    }
    case 2:
        o->pending_state = 2;
        o->st_ctr = 0;
        o->custom = 0;                   /* во время отдыха неуязвим */
        o->vxq = 0;
        /* fallthrough */
    case 3:
        if (o->st_ctr <= 0x3C) break;
        o->st_phase = (int)(rng_next() & 1);
        o->st_misc  = 0;
        o->hp       = o->st_aux;         /* восстановление hp */
        break;
    default: break;
    }
}

/* ── Тип 53 — падающий ползун (cs:0x17B6:0x007D, file 0x193DD) ────────
 *
 * INIT: подфаза (+0xE9) = 0, phase(+0xED) = 0, tick_period = 2.
 * Подфазы (si = +0xE9, затем +0xE9 |= 1):
 *   0/1 — падение: аним 0, vy = +5.0, box0.hitflag = 0 (НЕУЯЗВИМ);
 *         как только тайл в точке (x, нижняя кромка + 2*vy) имеет
 *         категорию 0 (пол) — подфаза 2;
 *   2/3 — ползёт: аним 1, vy = 0, vx = −2.0, постановка на землю со
 *         спрайтом 0x13F (#319), box0.hitflag = 1 (уязвим). Когда
 *         расстояние до игрока меньше 0x1E и phase ещё 0 — ОДИН раз
 *         зовётся cs:0x17B6:0x000C и phase ставится в 1. Очки 0x1000.
 *
 * ИСПРАВЛЕНО (2026-09, полная передизассемблировка cs:0x17B6:0x000C,
 * file 0x1936C): это не один снаряд 2.0, а ДВА горизонтальных
 * наведённых по X снаряда подряд (два отдельных вызова
 * cs:0x11D6:0x139, file 0x1939D и 0x193D3), у обоих flag=1, kind=2
 * (был 1 — чужой спрайт), speed=4.0 (была 2.0):
 *   #1 source=(x_world−5, y_world+3),  target_x=player.x_world
 *   #2 source=(x_world+5, y_world+20), target_x=player.x_world
 * (X — в 2px-единицах x_world, ×2 при переносе в пиксели порта). */
static void ai_type53(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 2, 0);
        obj_phys_begin(o);
        o->st_misc  = 0;
        o->st_phase = 0;
        o->custom   = 0;                 /* box0.hitflag */
        return;
    }
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = 0;
        o->vyq = QV(5, 0);
        o->custom = 0;                   /* неуязвим в полёте */
        /* fallthrough */
    case 1: {
        int probe = o->y + obj_height(o) + 2 * (int)(o->vyq >> 16);
        if (level_tile_attr_at(o->x, probe) != 0) break;
        o->st_misc = 2;
        break;
    }
    case 2:
        o->pending_state = 1;
        o->vyq = 0;
        o->vxq = -Q_2_0;
        obj_snap_ground(o);
        o->custom = 1;                   /* теперь уязвим */
        /* fallthrough */
    case 3: {
        int dxp = player_units() - obj_units(o);
        int dyp = physics_get_top_y() - o->y;
        int d = (dxp < 0 ? -dxp : dxp) + (dyp < 0 ? -dyp : dyp);
        if (d >= 0x1E || o->st_phase != 0) break;
        {
            int px  = physics_get_left_x();
            int sx1 = o->x - 10, sy1 = o->y + 3;
            int sx2 = o->x + 10, sy2 = o->y + 20;
            spawn_enemy_shot(sx1, sy1, px > sx1, QV(4, 0), 2);
            spawn_enemy_shot(sx2, sy2, px > sx2, QV(4, 0), 2);
        }
        o->st_phase = 1;                 /* выстрел одноразовый */
        break;
    }
    default: break;
    }
}

/* ── Тип 46 — «ныряющая» турель (cs:0x1692:0x066B, file 0x1878B) ──────
 *
 * INIT: постановка на землю со спрайтом 0x124 (#292), подфаза (+0xE9) = 0,
 * tick_period = 9, state_list = DS:0x1DE4.
 * THINK (dir = игрок правее; si = +0xE9, затем +0xE9 |= 1):
 *   0/1 — «спрятан»: аним-состояние dir, счётчик кадров = 0,
 *         box0.hitflag = 0 (НЕУЯЗВИМ); через 0x46 кадров — подфаза 2;
 *   2/3 — «высунулся»: аним dir + 2, box0.hitflag = 1 (уязвим), а
 *         счётчик кадров заводится ОТРИЦАТЕЛЬНЫМ: −((d >> 2) + 5), где
 *         d — расстояние до игрока, то есть чем дальше игрок, тем дольше
 *         фаза. Пока счётчик отрицательный, каждый тик зовётся
 *         cs:0x1692:0x0634 — тот же выстрел, что у типа 47: наведённый
 *         снаряд в (player.x, player.y + 10) со скоростью 0.75.
 *         Как только счётчик дошёл до нуля — снова подфаза 0.
 * Очки за смерть 0x1000.
 * ИСПРАВЛЕНО (2026-09, полная передизассемблировка выстрела): kind=8
 * (был 0 — чужой спрайт), клэмп нормализатора len<2→2 (был len<1→1,
 * тот самый Gotcha #4). flag=0, скорость 0.75, цель (player.x,
 * player.y+10) — подтверждено, без изменений. */
static void ai_type46(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 9, 0);
        obj_snap_ground(o);
        o->st_misc = 0;
        o->custom  = 0;                 /* box0.hitflag */
        return;
    }
    int dir = (player_units() > obj_units(o));
    int si = o->st_misc;
    o->st_misc |= 1;
    switch (si) {
    case 0:
        o->pending_state = dir;
        o->st_ctr = 0;
        o->custom = 0;                  /* неуязвим */
        /* fallthrough */
    case 1:
        if (o->st_ctr > 0x46) o->st_misc = 2;
        break;
    case 2: {
        o->pending_state = dir + 2;
        int dxp = player_units() - obj_units(o);
        int dyp = physics_get_top_y() - o->y;
        int d = (dxp < 0 ? -dxp : dxp) + (dyp < 0 ? -dyp : dyp);
        o->st_ctr = -((d >> 2) + 5);
        o->custom = 1;                  /* уязвим */
    }
        /* fallthrough */
    case 3:
        if (o->st_ctr >= 0) { o->st_misc = 0; break; }
        {   /* cs:0x1692:0x634 — наведённый выстрел, как у типа 47.
             * Передизассемблировка (file 0x18761..0x18781) даёт kind=8
             * (push ax,0x8 перед позиционными аргументами) — старый порт
             * не проставлял custom вовсе (снаряд рисовался чужим спрайтом
             * K0 вместо статичного #19 у kind=8). */
            int slot = alloc_obj_slot();
            if (slot < 0) break;
            ai_obj_t *b = &g_obj[slot];
            *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT, .subtype = 3,
                             .x = o->x, .y = o->y, .hp = 10, .level_idx = -1,
                             .facing = dir ? 1 : -1, .custom = 8 };
            obj_phys_begin(b);
            int tx = physics_get_left_x() / 2 - obj_units(o);
            int ty = physics_get_top_y() + 10 - o->y;
            int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
            /* Gotcha #4: клэмп нормализатора (cs:0x11D6:0x0006, file
             * 0x13566) — len<2 → 2, а не len<1 → 1. */
            if (len < 2) len = 2;
            b->vxq = ((s32)tx * QV(0, 0xC000)) / len;
            b->vyq = ((s32)ty * QV(0, 0xC000)) / len;
        }
        break;
    default: break;
    }
}

/* ── Тип 47 — турель (handler cs:0x1692:0x0479, file 0x18599) ─────────
 *
 * INIT: +0xEB = init_arg, tick_period = 10, y_world поднимается на
 * [DS:0x10D4] = 16 px, state_list = DS:0x1DA4, аним-состояние 0.
 * THINK: объект НЕ движется (прежняя реализация «прыгуна» была
 * выдумкой). Раз в тик (а тик — раз в 10 кадров) с шансом 1 из 16
 * стреляет НАВЕДЁННЫМ снарядом из своей точки в (player.x,
 * player.y + 10) со скоростью 0.75. Очки за смерть 0x1000.
 * ИСПРАВЛЕНО (2026-09): kind=8 (file 0x18608: push ax,0x8), был 0 —
 * снаряд рисовался кадрами {2,3} вместо статичного спрайта #19;
 * клэмп нормализатора len<2→2 (был len<1→1, Gotcha #4). flag=0,
 * speed=0.75, source=(x_world,y_world), target=(player.x, player.y+10)
 * — подтверждено без изменений. */
static void ai_type47(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 10, 0);
        o->y -= 16;   /* file 0x185D8: y_world -= [DS:0x10D4] (=16, byte-verified);
                        * ДО obj_phys_begin — иначе o->yq останется на старом y и
                        * obj_integrate вернёт объект вниз на следующем кадре. */
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
                     .facing = (player_units() > obj_units(o)) ? 1 : -1,
                     .custom = 8 };
    obj_phys_begin(b);
    /* Единичный вектор к цели со скоростью 0.75 ед./кадр. */
    int tx = physics_get_left_x() / 2 - obj_units(o);
    int ty = physics_get_top_y() + 10 - o->y;
    int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
    /* Gotcha #4: клэмп нормализатора — len<2 → 2, не len<1 → 1. */
    if (len < 2) len = 2;
    b->vxq = ((s32)tx * QV(0, 0xC000)) / len;
    b->vyq = ((s32)ty * QV(0, 0xC000)) / len;
}

/* ── Тип 42 — периодическая ловушка (cs:0x161F:0x029F, file 0x17C8F) ───
 *
 * INIT: tick_period = 3, подфаза (+0xE9) = 0, счётчик (+0xEB) =
 * init_arg * 10, phase(+0xED) = init_arg & 1 (file 0x17CBF..0x17CC7:
 * `self.frame_idx`, который на первом вызове INIT уже равен init_arg —
 * тот же приём, что у типа 22), и при phase != 0 объект поднимается на
 * 20 px, num_bboxes = 1, state_list = DS:0x1CF2. Порт раньше НЕ
 * присваивал st_phase вовсе (поле оставалось 0 по умолчанию) — запись
 * никогда не поднималась и всегда рисовалась анимацией 0/2 вместо
 * 1/3, хотя на LEVEL4 половина записей (idx 7,8,9,10 — trigger&0xFF
 * нечётный: 11,41,21,31) должна получать именно эту, вторую, ориентацию.
 * THINK: подфазы 0/1 — покой: аним-состояние = phase, кадр 0, счётчик
 * уменьшается на tick_period за тик; когда он вышел в ноль (или раньше
 * с шансом 1/128) — подфаза 2. Подфазы 2/3 — срабатывание: аним
 * phase + 2, и на ПОСЛЕДНЕМ кадре анимации вызывается cs:0x161F:0x00FC,
 * после чего счётчик = 500 и возврат в покой.
 *
 * cs:0x161F:0x00FC (рождение ряда из 5 сегментов) и сам дочерний
 * обработчик cs:0x161F:0x020A перенесены — см. ai_trap_segment.
 * Единственный неперенесённый вызов INIT'а — cs:0x6E:0x52B: это
 * постановка объекта в связный список движка, в порту не нужна. */
static void ai_type42(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 3, 0);
        o->st_misc  = 0;                          /* +0xE9 */
        o->st_phase = (int)o->init_arg & 1;       /* +0xED */
        o->st_aux   = (int)o->init_arg * 10;      /* +0xEB */
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
 * Оба вызова из подфазы 2/3 декомпилированы (были ранее не разобраны):
 *  cs:0x151A:0xDDF — «огненное дыхание», ОДИН прицельный выстрел из
 *    (x, y+7) при прыжке: с шансом 1/2 — точно в (player.x, player.y),
 *    kind=7 (гибнет о препятствие), скорость 2.0; иначе — в точку
 *    (player.x+15, player.y+5), kind=1, скорость 1.5. НЕ горизонтальный
 *    выстрел по направлению — цель берётся по снимку позиции игрока.
 *  cs:0x151A:0xE5D — приземление: см. комментарий у обработки vyq>0 ниже. */
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
        /* cs:0x151A:0xDDF — прицельное огненное дыхание (не горизонтальное:
         * см. комментарий к обработчику). */
        {
            int sx = o->x, sy = o->y + 7;
            int odd = (int)(rng_next() & 1);
            int dst_x_units = player_units() + (odd ? 0 : 15);
            int dst_y       = physics_get_top_y() + (odd ? 0 : 5);
            int kind        = odd ? 7 : 1;
            s32 speed       = odd ? Q_2_0 : Q_1_5;
            int tx = dst_x_units - obj_units(o);
            int ty = dst_y - sy;
            int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
            if (len < 1) len = 1;
            int slot = alloc_obj_slot();
            if (slot >= 0) {
                ai_obj_t *b = &g_obj[slot];
                *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                                 .subtype = 3, .x = sx, .y = sy, .hp = 10,
                                 .level_idx = -1, .facing = (tx > 0) ? 1 : -1,
                                 .custom = kind };
                obj_phys_begin(b);
                b->vxq = ((s32)tx * speed) / len;
                b->vyq = ((s32)ty * speed) / len;
            }
        }
        /* fallthrough */
    case 3:
        if (obj_units(o) <= player_units()) o->vxq = 0;   /* догнал */
        /* cs:0x151A:0xE5D (декомпилировано): только на снижении (vy>0),
         * тайл под (x − 0x10 ед. = −32 px, y + высота спрайта). Таблица
         * @0xEA0 приземляет на категориях 1,2,3,5,6 и >7 — НЕ приземляет
         * (продолжает падать) на 0, 4, 7. Раньше условие было записано
         * с точностью до наоборот. */
        if (o->vyq > 0) {
            int cat = level_tile_attr_at(o->x - 32, o->y + obj_height(o));
            if (!(cat == 0 || cat == 4 || cat == 7)) {
                o->vyq = 0;
                o->ayq = 0;
                /* set_motion(0) — сброс vx/vy/ax/ay, отдельно от вызова
                 * obj_snap_ground; misc(+0xE9) = 0 (назад в стойку). */
                o->vxq = 0;
                o->axq = 0;
                o->st_misc = 0;
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
 *         ранить. НИКАКОГО «вертикального рывка» в оригинале нет (vy
 *         всё время 0 — раньше было выдумано «по смыслу»): пока
 *         ((frame_ctr>>3)&1)==0 (8-тиковые окна стрельбы через 8 тиков
 *         тишины) каждый тик летит прицельный снаряд из переднего края
 *         хитбокса в (player.x, player.y + rand(0..127) − 63), kind=1,
 *         скорость 1.5; спустя 30 тиков боя (frame_ctr > 0x1E) — назад
 *         в подфазу 4 (посадка/отдых), после чего цикл 4→5→6→7
 *         повторяется. Есть ещё разовая при входе в 6 проверка стены
 *         перед мордой (cs:0x161F:0x03DD, file 0x17DCD) — гасит vx, если
 *         там сплошной тайл; НЕ портирована (редкий крайний случай).
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
        o->custom = 1;                  /* теперь уязвим (box0.hitflag=1) */
        /* fallthrough */
    case 7:
        /* Декомпилировано заново (file 0x1809F): вертикального «рывка»
         * в ASM НЕТ, vy всё время 0 — прежняя реализация была выдумана.
         * Пока ((frame_ctr>>3)&1)==0 (8-тиковое «окно стрельбы», окна
         * чередуются каждые 8 тиков) — каждый тик прицельный снаряд из
         * переднего края хитбокса (box0.x1, если dir, иначе box0.x0;
         * y_world+10) в точку (player.x_world, player.y_world +
         * rand(0..127) − 63), kind=1, скорость 1.5. После 30 тиков боя
         * (frame_ctr > 0x1E) — назад в подфазу 4 (посадка/отдых), минуя
         * повторный вход через misc=2 (обходит sub-phase 3 — на этот раз
         * приземление не проверяется тайлом, а форсируется напрямую). */
        if (((o->st_ctr >> 3) & 1) == 0) {
            int sx = dir ? (o->x + obj_width(o)) : o->x;
            int sy = o->y + 10;
            int dst_x_units = player_units();
            int dst_y = physics_get_top_y() + (int)(rng_next() & 0x7F) - 63;
            int tx = dst_x_units - sx / 2;
            int ty = dst_y - sy;
            int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
            if (len < 1) len = 1;
            int slot = alloc_obj_slot();
            if (slot >= 0) {
                ai_obj_t *b = &g_obj[slot];
                *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                                 .subtype = 3, .x = sx, .y = sy, .hp = 10,
                                 .level_idx = -1, .facing = (tx > 0) ? 1 : -1,
                                 .custom = 1 };
                obj_phys_begin(b);
                b->vxq = ((s32)tx * Q_1_5) / len;
                b->vyq = ((s32)ty * Q_1_5) / len;
            }
        }
        if (o->st_ctr > 0x1E) o->st_misc = 4;
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
 *   6   — подскок вверх: vx = 0, vy = −3.0, аним si.
 *
 * ОТДЕЛЬНО от подфаз (+0xE9) — состояние ДВИЖКА (cur_state), диспетчер
 * cs:0x11D6:0x0B20 (7 записей): 0/1=init, 2=think(выше), 4=смерть (add_score
 * 0x1800 + die_puff), 6=die_puff без очков. Состояние 5 — «ранен, но жив»
 * (file 0x1402B+0xDD9): будит стража ДОСРОЧНО — если self.misc(+0xE9)==1
 * (спит), принудительно переводит в подфазу 2 (прыжок), минуя дистанцию
 * |dx|>0x50. В порту нет отдельного диспетчера по cur_state==5, но есть
 * тот же по смыслу сигнал «нас задели в этом кадре» — o->hit_flag (как у
 * типа 52, см. его комментарий). Раньше страж просыпался ТОЛЬКО по
 * дистанции — попадание издалека (наведённым снарядом и т.п.) его не
 * будило. */
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
        if (o->hit_flag) { o->st_misc = 2; break; }   /* ранен — будим досрочно */
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
 * счётчик перевалил 90, стреляет: call cs:0x11D6:0x139 (та же наводящая
 * функция, что и у типа 14) из (self.x, self.y + 3 + (rand&8)) НА
 * снимок позиции игрока (player.x_world, player.y_world — без сдвига),
 * kind=5 (внутри 0x139 превращается в 5/6 по знаку итоговой vx — это и
 * есть выбор спрайта «влево/вправо»), скорость 2.0. Это НЕ горизонталь-
 * ный выстрел: вертикальная составляющая зависит от относительной высоты
 * игрока. После выстрела frame_ctr = 0x5A − ((rand & 0xF) + 0x46),
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

    /* Наведённый снаряд (см. комментарий выше — не горизонтальный). */
    {
        int sx = o->x, sy = o->y + 3 + (int)(rng_next() & 8);
        int tx = player_units() - sx / 2;
        int ty = physics_get_top_y() - sy;
        int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
        if (len < 1) len = 1;
        int slot = alloc_obj_slot();
        if (slot >= 0) {
            ai_obj_t *b = &g_obj[slot];
            *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                             .subtype = 3, .x = sx, .y = sy, .hp = 10,
                             .level_idx = -1, .facing = (tx > 0) ? 1 : -1,
                             .custom = 5 };
            obj_phys_begin(b);
            b->vxq = ((s32)tx * Q_2_0) / len;
            b->vyq = ((s32)ty * Q_2_0) / len;
        }
    }
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
 *         пока в воздухе (подфаза 3) — у «стреляющего» варианта (phase!=0,
 *         helper cs:0x151A:0x0870, file 0x17210) КАЖДЫЙ тик наведённый
 *         снаряд по игроку (source = self.x/self.y БЕЗ смещения, target =
 *         реальные player.x_world/y_world — это НЕ вырожденный горизон-
 *         тальный выстрел типа 15: override-флаг у 0x139 здесь 0, значит
 *         вертикальная составляющая сохраняется); приземлился — подфаза 6;
 *   6/7 — второй прыжок: кадр dirsel+3, vy = −1.25, ay = +0.125; на
 *         подъёме, если игрок ниже на 0x19 — зависание (vy = 0) И (тот же
 *         0x870) один наведённый снаряд в момент зависания; приземлился —
 *         снова подфаза 2.
 * Раньше «стреляющий» вариант (init_arg > 10) не стрелял вовсе — phase
 * читался только при INIT и нигде не использовался в THINK. */
static bool obj_on_ground(const ai_obj_t *o)
{
    return level_solid_at(o->x + 8, o->y + obj_height(o) + 1);
}

/* Наведённый снаряд «стреляющего» голема — порт cs:0x151A:0x0870
 * (file 0x17210): source = (self.x_world, self.y_world) БЕЗ смещений,
 * target = (player.x_world, player.y_world) снимком, скорость 2.25,
 * kind=1 (тот же класс снаряда, что и у типа 15, но с override-флагом 0 у
 * вызова 0x139 — вертикальная составляющая НЕ обнуляется).
 *
 * ПЕРЕПРОВЕРЕНО (2026-09, аудит скоростей снарядов): полная
 * передизассемблировка (пуши file 0x1721F..0x17245, call cs:0x151A:0x8A6
 * → file 0x17246) подтверждает байт-в-байт: flag=0, speed_hi=2,
 * speed_lo=0x4000 → 2.25 = QV(2,0x4000), kind=1, target/source — как
 * выше, оба БЕЗ смещения. Магнитуда, траектория и точки совпадают с
 * прежней реализацией — расхождение нашлось только в клэмпе минимальной
 * длины вектора: оригинал (file 0x135E2, общий хелпер 0x006) берёт
 * len<2→2, здесь было len<1→1 — исправлено. */
static void t35_homing_shot(const ai_obj_t *o)
{
    int sx = o->x, sy = o->y;
    int tx = player_units() - sx / 2;
    int ty = physics_get_top_y() - sy;
    int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
    if (len < 2) len = 2;
    int slot = alloc_obj_slot();
    if (slot < 0) return;
    ai_obj_t *b = &g_obj[slot];
    *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT, .subtype = 3,
                      .x = sx, .y = sy, .hp = 10, .level_idx = -1,
                      .facing = (tx > 0) ? 1 : -1, .custom = 1 };
    obj_phys_begin(b);
    b->vxq = ((s32)tx * QV(2, 0x4000)) / len;
    b->vyq = ((s32)ty * QV(2, 0x4000)) / len;
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
        if (o->st_phase) t35_homing_shot(o);   /* «стреляющий» — каждый тик в воздухе */
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
            if (o->st_phase) t35_homing_shot(o);   /* выстрел в момент зависания */
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

/* ── Тип 43 — «крестовый» летун (handler cs:0x1692:0x0205, file 0x18325)
 * (прежний адрес cs:0x158C:0x0205/file 0x17085 был неверен вдвойне: этот
 * cs:seg не соответствовал даже собственному file, а настоящий обработчик
 * по таблице DS:0x10FE лежит в сегменте 0x1692, рядом с типами 44/45).
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
 *         возврат в подфазу 0. Очки за смерть 0x1800.
 *
 * Выстрел передизассемблирован полностью (пуши file 0x1852A..0x18548,
 * call cs:0x11D6:0x139 → file 0x18554): flag=0, speed=QV(0,0xE000)=0.875
 * (подтверждено), НО kind=0xA=10 (push ax,0xa, file 0x18534) — было 0 в
 * порту (чужой спрайт K0 и правило гибели по таймауту вместо статичного
 * спрайта #283 и гибели в воде, как у типа 8 «падающий лёд»).
 * target_x=self.x_world (не игрок) ⇒ горизонтали нет (tx=0) уже за счёт
 * геометрии, а не флага; target_y=player.y_world, source=(self.x_world,
 * self.y_world+10) — знак vy определяется по (player.y − (self.y+10)),
 * а не по (player.y − self.y), как было в порту (расхождение только в
 * узком окне ±10px).
 * ДОПОЛНИТЕЛЬНО у kind=10 общий spawn_projectile (file 0x13755..0x1376B,
 * та же функция cs:0x11D6:0x139) имеет побочный эффект: он пишет
 * переданную speed_hi:speed_lo НЕ в снаряд, а в vy САМОГО стрелявшего
 * (self, [0xD8A6]) — то есть летун ПОСЛЕ выстрела получает vy=+0.875
 * (рывок вниз), independent от текущего фазового vy/vx. В порту это не
 * было реализовано вовсе. */
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
                                 .custom = 10 };
                obj_phys_begin(b);
                b->vxq = 0;
                b->vyq = (physics_get_top_y() > o->y + 10) ? QV(0, 0xE000)
                                                           : -QV(0, 0xE000);
                /* kind=10 у spawn_projectile (file 0x13755) дополнительно
                 * форсирует vy САМОГО летуна в +0.875 (рывок вниз после
                 * выстрела) — побочный эффект оригинала, не перепутан со
                 * снарядом. */
                o->vyq = QV(0, 0xE000);
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
 *         до уровня игрока) — ВЫСТРЕЛ, затем vy = +2.0, подфаза 0,
 *         аним si+2.
 * Выстрел (call cs:0x11D6:0x139 → 0x6, file 0x1379F/0x1369C): снаряд
 * рождается в (self.x, self.y+20) и НАВОДИТСЯ на снимок позиции игрока
 * (player.x_world, player.y_world+5) — вектор нормируется по Манхэттену
 * (|dx|+|dy|) и масштабируется на скорость 2.125 (0x139 передаёт misc=4,
 * т.е. kind=4 → obj.subtype=4, спрайты #10..#13, DS:0x1784 state 4).
 * Это НЕ горизонтальный выстрел — реальный снаряд может лететь по диагонали.
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
            /* Наведённый снаряд (НЕ горизонтальный — см. комментарий
             * к обработчику): цель — снимок позиции игрока. */
            {
                int sx = o->x, sy = o->y + 20;
                int tx = player_units() - sx / 2;
                int ty = physics_get_top_y() + 5 - sy;
                int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
                if (len < 1) len = 1;
                int slot = alloc_obj_slot();
                if (slot >= 0) {
                    ai_obj_t *b = &g_obj[slot];
                    *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                                     .subtype = 3, .x = sx, .y = sy, .hp = 10,
                                     .level_idx = -1, .facing = 1, .custom = 4 };
                    obj_phys_begin(b);
                    b->vxq = ((s32)tx * QV(2, 0x2000)) / len;
                    b->vyq = ((s32)ty * QV(2, 0x2000)) / len;
                }
            }
        } else {
            o->vyq += QV(0, 0x4000);       /* +0.25 за тик */
        }
        break;
    default: break;
    }
}

/* ── Тип 18 — ныряющий чёрт (handler cs:0x11D6:0x127D, file 0x147DD) ───
 *
 * (Прежде в комментарии стоял неверный адрес cs:0xFFA:0x127D/file 0x12A1D —
 * тот файл-адрес принадлежит совсем другой функции (тальи очков конца
 * уровня); настоящий обработчик типа 18 — по таблице DS:0x10FE — лежит
 * в сегменте 0x11D6, сразу после типа 16.)
 *
 * INIT: si = (игрок правее), aux(+0xEF) = 0, phase(+0xED) = init_arg,
 * phase_ctr(+0xEB) = СНИМОК player.y_world, tick_period = 3,
 * подфаза (+0xE9) = 2, vx = ±1.5, vy = +1.5,
 * y_world = player_y + [DS:0x100E](0x1A) − [DS:0x1050](0x1C) − 20 =
 * player_y − 22 (оба байта — константы, нигде не перезаписываются),
 * state_list = DS:0x19B4, аним-состояние = si.
 *
 * THINK (si пересчитывается каждый тик):
 *   tgt = phase ? (player_y − снимок + 40) : 0;
 *   dy = (player_y − tgt + 0x1A) − (self.y + 0x1C) = (player_y − tgt) − self.y − 2
 *   0   — подфаза 1, аним si+6
 *   1   — dy >= 40 → подфаза 5, vx = ±1.5, vy = 0, аним si
 *   2   — dy < 0 (или phase и frame_ctr > 30) → вх = ±1.5, vy = 0,
 *         подфаза 4, frame_ctr = 0
 *   4   — атака, если у игрока есть живые снаряды и |dx| > 10, либо
 *         (phase и rand&1): подфаза 0, аним si+4, vx = ±1.5, vy = −1.5;
 *         при phase — ++aux; на 5-м разе (aux>4) phase=0 И принудительно
 *         vx делается отрицательным (влево), если был положительным
 *         (file 0x1459); с шансом 1/2 (независимо от aux)
 *         spawn_projectile(self → (self.x ± 8, player_y), скорость 2.0)
 *   5   — |dx| < 10 → подфаза 2, vx = 0, vy = +1.5
 * Аним-состояния: 0/1 полёт, 2/3 запас, 4/5 атака, 6/7 разворот.
 *
 * ПЕРЕПРОВЕРЕНО (2026-09, аудит скоростей снарядов): полная
 * передизассемблировка вызова spawn_projectile (пуши file 0x149E7..
 * 0x14A1B, сам call cs:0x11D6:0x14BC → file 0x14A1C) подтверждает:
 * flag=0 (в отличие от типов 15/29 — override НЕ включён, вертикаль
 * реально сохраняется), speed_hi=2, speed_lo=0 → 2.0 = Q_2_0, kind=0,
 * target=(self.x_world ± 8, player.y_world) — знак смещения по X решает
 * НЕЗАВИСИМЫЙ от направления полёта бросок монеты (НЕ по si), source =
 * (self.x_world, self.y_world) без смещения. Порт (ниже) уже это
 * реализует байт-в-байт: ±8 «родных» единиц = ±16 px (o->x — в px,
 * ×2 к «родным»), sy = physics_get_top_y() = player.y_world. Идиома
 * `vx<<17/len` математически равна `vx*Q_2_0/len` (тот же результат,
 * что у типов 29/35 через явный QV) — ложной тревоги не подтвердилось.
 * Найдены две неточности (обе исправлены ниже):
 *   1) клэмп минимальной длины вектора — оригинал (file 0x135E2, общий
 *      хелпер 0x006) берёт len<2→2, порт использовал len<1→1;
 *   2) kind у спаунимого снаряда — порт ставил .custom=2 (статичный
 *      спрайт 9, см. ai_enemy_shot), должно быть kind=0 (анимация
 *      {2,3}) — визуальная ошибка, на скорость/траекторию не влияла. */
static void ai_type18(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    int si = (player_units() > obj_units(o));
    if (!o->use_anim) {
        o->y = physics_get_top_y() - 22;   /* file 0x1331: +0x1A −0x1C −20 */
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
    int dy  = (physics_get_top_y() - tgt) - o->y - 2;   /* file 0x1386: +0x1A −0x1C */
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
            if (++o->custom > 4) {
                o->st_phase = 0;
                /* file 0x1459: после серии из 5 атак направление
                 * принудительно делается левым (vx > 0 → -vx). */
                if (o->vxq > 0) o->vxq = -o->vxq;
            }
            if (rng_next() & 1) {               /* сброс снаряда в игрока */
                int sx = o->x + ((rng_next() & 1) ? 16 : -16);
                int sy = physics_get_top_y();
                int slot = alloc_obj_slot();
                if (slot >= 0) {
                    ai_obj_t *b = &g_obj[slot];
                    /* kind=0 (см. комментарий к обработчику) — раньше
                     * стояло .custom=2, что давало статичный спрайт 9
                     * вместо анимированного {2,3} — исправлено. */
                    *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT,
                                     .subtype = 3, .x = o->x, .y = o->y,
                                     .hp = 10, .level_idx = -1,
                                     .facing = si ? 1 : -1, .custom = 0 };
                    obj_phys_begin(b);
                    /* Единичный вектор к цели со скоростью 2.0 ед./кадр. */
                    int vx = (sx - o->x) / 2, vy = sy - o->y;
                    int len = (vx < 0 ? -vx : vx) + (vy < 0 ? -vy : vy);
                    if (len < 2) len = 2;
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

/* Ширина хитбокса объекта. У типов, идущих через аним-машину, кадр
 * лежит в parts[0] — ручной sprite_for_type для них возвращает не тот
 * спрайт (а часто и вовсе 0xFFFF), и бокс схлопывался в дефолтные
 * 16 px. Оригинал берёт размеры из box0.x0..x1, то есть из ТЕКУЩЕГО
 * кадра, что и повторяет эта функция. */
static int obj_width(const ai_obj_t *o)
{
    u16 sid = (o->num_parts > 0) ? o->parts[0].sprite
                                 : sprite_for_type(o->type, o->anim_phase);
    const sprite_t *s = (sid == 0xFFFF) ? NULL : sprite_find(sid);
    return s ? (int)s->art_w : 16;
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
 * пресетов DS:0x1348 — и счётчик сбрасывается.
 *
 * ПОСЛЕ выбора новой скорости оригинал ДВАЖДЫ (по X и по Y) зовёт хелпер
 * cs:0x1348:0x09EC (file 0x1566C) с (self.pos, self.v, view_lo, view_hi):
 * если движение унесло бы объект за край ТЕКУЩЕГО ВИДИМОГО экрана —
 * компонента скорости отражается (меняет знак). Раньше это не было
 * перенесено: лаз мог свободно улетать к краю уровня, не отражаясь от
 * рамки камеры. view_top в порту берётся из ai_render (кадром позже, чем
 * view_left из ai_check_triggers, — не критично при скролле в единицы
 * px/кадр). */
static int t25_reflect(int pos, int v, int lo, int hi)
{
    if (v > 0) return (pos + v > hi) ? -v : v;
    return (pos + v < lo) ? -v : v;
}

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
        {
            int vlo = g_camera_left / 2, vhi = (g_camera_left + VIEW_W) / 2;
            int tlo = g_camera_top,       thi = g_camera_top + VIEW_H;
            int nvx = t25_reflect(obj_units(o), (int)(o->vxq >> 16), vlo, vhi);
            int nvy = t25_reflect(o->y,          (int)(o->vyq >> 16), tlo, thi);
            o->vxq = ((s32)nvx << 16) | (o->vxq & 0xFFFF);
            o->vyq = ((s32)nvy << 16) | (o->vyq & 0xFFFF);
        }
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
 * Полностью самодостаточный листинг (retf на file 0x16799, до следующего
 * обработчика в сегменте), проверен побайтово целиком.
 * INIT/THINK слиты (state1 проваливается в state2 без jmp): tick_period=4,
 * направление к игроку, vx = ±0.5 ед./кадр (итоговое присвоение на file
 * 0x16716 — более ранняя запись vx=±1.0 на file 0x16697 тут же
 * перезаписывается и ни на что не влияет). Разворот с гистерезисом
 * 10..17 ед. (rng&7) — байт-верно как было.
 * Выстрел (file 0x1675D, `call 0x11d6:0x139` — общий spawn_projectile):
 * однократно (phase(+0xED)==0), когда frame_ctr(+0xC7) > 75; ГОРИЗОНТАЛЬНЫЙ
 * (аргумент «flatten Y»=1 обнуляет dy) снаряд в (self.x, self.y+7) со
 * скоростью 2.0 ед./кадр к игроку. Спаунер сам выбирает kind ребёнка по
 * итоговому знаку vx (5=влево/6=вправо, file 0x1373E) — здесь не влияет на
 * поведение (ai_enemy_shot берёт спрайт по vxq, не по kind), но передаём
 * направленно для точности. */
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
        spawn_enemy_shot(o->x, o->y + 7, o->st_phase, Q_2_0, o->st_phase ? 6 : 5);
        o->st_misc = 1;
    }
    o->pending_state = o->st_phase;
}

/* ── Тип 48 — турель (handler cs:0x170C:0x18C, file 0x18A4C) ───────────
 * Полностью самодостаточный листинг (retf на file 0x18C0B), проверен
 * побайтово целиком. Неподвижна. INIT: tick_period=4, стартовый кадр
 * анимации из rand (state1 проваливается в state2 без jmp, как у типа 28).
 * КАЖДЫЙ тик (независимо от фазы!) на file 0x18B2E/0x18B3E пересчитывается
 * box0.hitflag = (|player.x−self.x| < 120) — турель УЯЗВИМА только вблизи
 * игрока и НЕУЯЗВИМА издали; этого не было в порту (ai.c ниже добавляет
 * o->custom как эквивалент hitflag, зарегистрированный в is_damageable_obj).
 * TICK: фаза 0 — взвести таймер (rand & 0x5F), фаза 1 — ждать 250
 * кадров (сброс при переходе игрока на другую сторону), фаза 2 —
 * горизонтальный (flatten-Y, как у типа 13/28) шар 1.5 ед./кадр в
 * (self.x, self.y−5) со скоростью к игроку по X (file 0x18BD3, общий
 * spawn_projectile cs:0x11D6:0x139) и снова в фазу 0.
 * Смерть (state4, file 0x18BEE): add_score(0x1000) + локальный спаунер
 * обломков (cs:0x170C:0xA1, не декомпилирован до конца) вместо общего
 * die_puff() — визуальный эффект не портирован по видимому эффекту,
 * как и весь каркас состояний 4/5/6 (см. DECOMPILATION.md §9). */
static void ai_type48(ai_obj_t *o, u16 frame, u8 in)
{
    (void)frame; (void)in;
    if (!o->use_anim) {
        anim_begin(o, 4, 0);
        u16 r = (u16)(rng_next() & 3);
        o->frame_index = (u8)(r <= 2 ? 2 : (rng_next() & 3));
        o->st_misc = 0;
        o->custom  = 0;                       /* box0.hitflag, по умолч. неуязвим */
        return;
    }
    int sdx = player_units() - obj_units(o);
    int facing = sdx > 0;
    int adx = sdx < 0 ? -sdx : sdx;
    o->custom = (adx < 120) ? 1 : 0;          /* уязвим только вблизи игрока */
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
 * Все 58 типов индивидуально декомпилированы и сверены с KARNOV.EXE
 * (2026-09 review) — байт-верны и INIT, и TICK. Настоящих «по семантике»
 * заглушек не осталось; открытых вопросов — три, каждый с точным
 * инлайн-комментарием на месте:
 *   - тип 21 (и 30/50): рантайм-мутация тайловой карты коллизий, которую
 *     делает INIT/state4/6 — не портирована, нет инфраструктуры записи
 *     tile-attr из ai.c (см. комментарий «Повторная декомпиляция TICK по
 *     типам» перед ai_type21 выше);
 *   - тип 34: кривая «волны» тела (snake-follow) не до конца
 *     раздекомпилирована — портирован только подтверждённый порог
 *     активации (см. комментарий перед case 34 и case type==34 ниже);
 *   - тип 36 (Рю, финальный босс): 2026-09 повторный проход ЗАКРЫЛ оба
 *     старых вопроса (что ставит misc=2 → только собственный state4;
 *     триггер конца боя → у Рю его нет вообще, см. подробный разбор
 *     перед ai_ryu) исчерпывающим сканом всего EXE. ОСТАЮЩЕЕСЯ, по той
 *     же причине, что и тип 21 выше: тайл-стемпинг тела Рю в геометрию
 *     уровня (cs:0xD4B:0x2DB) не портирован — нужна per-cell tile-attr
 *     инфраструктура, которой в level.c сейчас нет (там attr per-графике
 *     тайла, не per-клетке карты). ОТДЕЛЬНО, вне типа 36: в LEVEL9.BIN
 *     запись Рю (rec[2]) целиком нулевая (cur/range/spawn/trigger=0) —
 *     точно как у type-8 sky-цепи — и, В ОТЛИЧИЕ от sky-цепи (у которой
 *     есть выделенный драйвер вне AI-диспетчера, ai_sky_tick), в порту
 *     СЕЙЧАС НЕТ никакого специального спаунера для типа 36: Ryu никогда
 *     не создаётся ни в одном уровне (проверено `KARNOV_DBG_SPAWN=1` —
 *     type=36 не встречается). Настоящий код-спаунер Рю в KARNOV.EXE НЕ
 *     найден (в отличие от sky-цепи, чей драйвер известен) — это отдельная
 *     задача уровня «найти и подключить спаунер», не относящаяся к
 *     обработчику типа 36 самому по себе. */

/* Табличное описание «простых» типов: базовый спрайт (индекс каталога
 * SPRITES.EGA = ENEMY_GUIDE) и число кадров. Используется ТОЛЬКО как
 * sprite_for_type() cache-miss fallback (когда для типа нет ни манифеста
 * ai_type, ни явного case) — реальная TICK-логика этих типов давно
 * перенесена в отдельные обработчики (ai_typeN выше), таблица описания
 * поведения (SK_*: chaser/patrol/flyer) удалена как мёртвый код
 * (2026-09 cleanup): поле нигде не читалось. */
typedef struct simple_ai {
    u16 base;       /* базовый sprite id (0 = нет спрайта → placeholder) */
    u8  nframes;
} simple_ai_t;

/* index = primary type (только типы без записи в манифесте ai_type). */
static const simple_ai_t SIMPLE_AI[58] = {
    /* type12: «скачущий валун» (ai_type12). Реальный state_list DS:0x18F4
     * (ANIM_LISTS[12]/ST_12 в anim_data.c) уже даёт байт-верные спрайты
     * #21/22/23 (общие sprite id — те же камни, что у TYPE_ROCK_PROJECTILE);
     * эта запись — лишь подстраховка на случай, если рендер вызовет
     * sprite_for_type() ДО первого anim_apply(). */
    [12] = { 21, 3 },
    [22] = { 181, 4 },   /* tile-aware (0xB5..B8) */
    [25] = { 189, 4 },   /* PRNG-lurker — спец-tick ниже */
    [26] = { 193, 8 },   /* быстрый воин, sub_timer_max=2 */
    [28] = { 207, 4 },   /* FX-spawn (0xCF..D2) */
    [29] = { 211, 7 },   /* multi-phase, sub_timer_max=8 */
    [31] = { 219, 3 },   /* spawn 0xDD */
    [32] = { 222, 8 },   /* spawn 0xDE (близнец 31) */
    [35] = { 239, 6 },   /* dual-mode: агрессия по init_arg>10 */
    [37] = { 251, 3 },   /* slow boss, sub_timer_max=7 */
    [38] = { 254, 5 },   /* delayed 75 кадров (state_ext=75) */
    [40] = { 263, 4 },   /* self-init, sub_timer_max=2 */
    [43] = { 275, 9 },   /* flash enemy (9 кадров) */
    [46] = { 292, 4 },   /* extended 0x124, sub_timer_max=9 */
    [48] = { 298, 6 },   /* extended 0x12B */
    [52] = { 309, 9 },   /* Y-align 10 */
    [53] = { 318, 2 },   /* простой шаблон */
    [54] = { 320, 2 },   /* extended 0x140 */
    [55] = { 322, 7 },   /* хелпер финального босса 0x142 */
};


/* ─── Настоящая sky-цепь (тип 8, падающий лёд) ──────────────────────────
 * Байт-точный порт driver cs:0xFFA:0x1615 (file 0x12DB5), работает ВНЕ
 * объектной системы:
 *   si = player.x_world − [0xD8A0] (движковые единицы, 1 = 2px);
 *   если si > 0x32 (продвинулись больше 50 ед. = 100px с последней
 *     отметки) — обойти цепь от головы [0xD880/0xD882] по +0xF1/+0xF3,
 *     каждому узлу state_byte(+0xB6) = 4 (kill-хелпер cs:0xFFA:0x155E,
 *     file 0x12CFE), затем [0xD8A0] = player.x, [0xD8A4] = 0;
 *   иначе ++[0xD8A4] (простой), КРОМЕ [0xD8CE] (player_state) == 3;
 *   когда [0xD8A4] > 0x186 (390 кадров) И цепь пуста ([0xD880] == 0) —
 *     n = (rand&1)+1, спаунер cs:0xFFA:0x1585 создаёт n объектов типа 8
 *     (handler cs:0xFFA:0x139F = ai_type08, сам расставляет себя
 *     относительно игрока), связывает их в список, [0xD8A0] = player.x,
 *     [0xD8A4] = 0.
 * Цепь убивается ТАКЖЕ при спауне любого scripted-врага subtype 1/7
 * (jump-table в check_triggers @ file 0x5155 → тот же helper 0x12CFE) —
 * см. ai_check_triggers ниже.
 * НЕ ПОРТИРОВАНО: gate «cmp [0xD8CE],3; jz» — простой не должен расти,
 * пока player_state == 3 (в порту нет переменной с такой нумерацией
 * состояний игрока); эффект минимален — счётчик тикает на 1-2 кадра
 * больше в редком состоянии игрока.
 *
 * ИСПРАВЛЕНО (code-review PSEUDO-TYPES 2026-09): этот байт-точный
 * алгоритм (kill-on-progress + простой 390 кадров) раньше по ошибке был
 * навешан на TYPE_FLYING_BIRD (декоративную «птичью стаю», с тех пор
 * убранную целиком — см. 2026-09-13), а настоящий лёд (тип 8) спаунился
 * отдельным счётчиком БЕЗ сброса по
 * продвижению игрока — то есть лёд шёл раз в 390 кадров ВСЕГДА, даже
 * пока игрок бежит вперёд не останавливаясь, чего в оригинале не
 * бывает (лёд идёт, только когда игрок топчется на месте). Заодно
 * check_triggers (line ~4494) убивал ПТИЦ вместо льда при спауне
 * scripted-врага subtype 1/7 — тоже перепутанные цепи, тоже исправлено. */
#define SKY_IDLE_FRAMES     390     /* 0x186 */
#define SKY_PROGRESS_UNITS  50      /* 0x32 (= 100 px) */

static void ai_kill_ice_chain(void)
{
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].type == 8)
            g_obj[i].alive = false;
}

static void ai_ice_chain_tick(u16 frame)
{
    if (!s_sky_enabled) return;
    int px = player_units();
    if (px - s_ice_last_x_units >= SKY_PROGRESS_UNITS) {
        ai_kill_ice_chain();
        s_ice_last_x_units = px;
        s_ice_ctr = 0;
        return;
    }
    ++s_ice_ctr;
    if (s_ice_ctr > SKY_IDLE_FRAMES) {
        bool chain_alive = false;
        for (int i = 0; i < g_num; ++i)
            if (g_obj[i].alive && g_obj[i].type == 8) { chain_alive = true; break; }
        if (!chain_alive) {
            int n = (int)(rng_next() & 1) + 1;
            for (int i = 0; i < n; ++i) {
                int slot = alloc_obj_slot();
                if (slot < 0) break;
                g_obj[slot] = (ai_obj_t){
                    .alive = true, .type = 8, .subtype = 1, .hp = 10,
                    .level_idx = -1, .facing = 1, .spawn_frame = frame,
                };
            }
            s_ice_last_x_units = px;
            s_ice_ctr = 0;
        }
    }
}

/* Птичья стая (TYPE_FLYING_BIRD/ai_bird_flock_tick) УБРАНА (2026-09-13, по
 * прямому указанию): это была явно подписанная «ЧИСТАЯ СИНТЕТИКА ПОРТА» —
 * в оригинале НЕТ периодического «вылета птиц», driver cs:0xFFA:0x1615
 * управляет ТОЛЬКО настоящей ледяной цепью (тип 8, ai_ice_chain_tick ниже).
 * ai_sky_tick теперь — тонкая обёртка, оставлена только чтобы не менять
 * сигнатуру вызова из ai_tick. */
static void ai_sky_tick(u16 frame)
{
    ai_ice_chain_tick(frame);
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
 *
 * Т.е. это неподвижный СТОЛБ из голов, растущий ВНИЗ от y_world до пола:
 * LEVEL2 записи (spawn_y=210, init_arg=5) дают 210 + 5*20 = 310 = ровно
 * уровень земли. Прежняя реализация раскладывала сегменты ГОРИЗОНТАЛЬНО
 * (dx = −16) и заставляла их ползти за игроком — отсюда «головы висят
 * в небе» вместо колонн. Реконструкция «змеи» была ошибкой.
 *
 * ИСПРАВЛЕНО (повторная декомпиляция 2026-09): TICK (state 2, file
 * 0x15429..0x155FA) НЕ пустой — предыдущее утверждение «объект статичен»
 * было ошибкой чтения таблицы состояний (спутал границу state2/state5).
 * Каждый тик:
 *   1. hit_points = 0xFA (250) — принудительно, КАЖДЫЙ тик (file 0x15429),
 *      а не один раз в INIT. Эффективно колонна НЕУЯЗВИМА: урон,
 *      нанесённый между вызовами THINK, стирается раньше, чем успевает
 *      накопиться до 0 (250 HP против ~10 урона одного файербола).
 *   2. misc(+0xE9, начальное 0) — таймер перезарядки, декрементируется
 *      к 0 (file 0x15484).
 *   3. box0.sprite приводится к 0xAD(173) каждый тик, если он был ≠0
 *      (file 0x1548F) — так «поза выстрела» 0xAE(174), выставленная в
 *      предыдущем тике, держится ровно 1 тик и гаснет обратно в голову.
 *   4. Хелпер cs:0x6E:0x7FC (= cs:0x6E:0x7A5 на каждый bbox, ТОТ ЖЕ,
 *      что уже опознан и портирован для типа 23 — см. ai_type23) — «хотя
 *      бы один bbox виден в окне камеры»; если нет — выстрела не будет
 *      в этот тик.
 *   5. Если виден И misc==0: play_sound(2), box0.sprite=0xAE(174),
 *      ДВА прицельных снаряда (spawn_projectile, kind=1) из точки box0
 *      (голова, i=0) в (player.x−40px, player.y) и (player.x+10px,
 *      player.y) (file 0x154A4..0x154FC); misc перезаряжается случайным
 *      числом 150..181 тиков.
 *
 *      ИСПРАВЛЕНО (2026-09, скорость выстрела): spawn_projectile
 *      (cs:0x11D6:0x139, file 0x13699) получает 8 слов
 *      (from_x, from_y, to_x, to_y, kind, speed_lo, speed_hi, y_flag) —
 *      здесь kind=1, y_flag=0, и, что важно, (speed_hi:speed_lo)=(4:0).
 *      spawn_projectile форвардит from/to и (speed_hi:speed_lo) в
 *      ОТДЕЛЬНЫЙ векторный хелпер cs:0x11D6:0x0006 (file 0x13566, near-
 *      call `E8 2B FE` на file 0x13738 — с truncated-target ловушкой
 *      ndisasm, см. docs/DECOMPILATION.md). Этот хелпер (НЕ obj_aim'а):
 *        len = |to_x−from_x| + |to_y−from_y|             (Манхэттен)
 *        if (len < 2) len = 2                             (file 0x135E2)
 *        scale = (speed_hi:speed_lo эта пара интерпретируется как ОДНО
 *                 32-битное Q16.16-число) / len            (file 0x135F8)
 *        vx = (to_x−from_x) * scale;  vy = (to_y−from_y) * scale
 *      т.е. (speed_hi:speed_lo) — это ЦЕЛЕВАЯ СКОРОСТЬ снаряда в Q16.16,
 *      а НЕ «долететь за n тиков». Раньше здесь ошибочно стоял вызов
 *      obj_aim(...,4) — та функция (cs:0x11D6:0x00DD, «доехать ровно за
 *      n тиков», используется gold-ball'ами) взята не из того места и
 *      даёт скорость, растущую с расстоянием (на большой дистанции —
 *      мгновенное попадание). Подтверждено байт-в-байт сверкой ДВУХ
 *      других вызывающих того же spawn_projectile→0x0006 с УЖЕ
 *      проверенными скоростями: тип 14 (file 0x14368+0xFC4..0xFF1)
 *      толкает `mov dx,2 / mov ax,0x2000` = 2.125 (Q_2_0+0x2000) — ровно
 *      документированная скорость её выстрела; тип 32 (file
 *      0x168B4+0x1CA4) толкает `mov dx,2 / xor ax,ax` = 2.0 (Q_2_0) —
 *      ровно её документированная скорость. Для типа 20 (file
 *      0x14C80+0x827/0x857) толкается `mov dx,4 / xor ax,ax` = 4.0
 *      (Q_4_0) — отсюда QV(4,0) ниже. Экран (~160 ед.) снаряд при этой
 *      скорости пересекает за ~40 тиков — правдоподобно, не мгновенно.
 *
 * ── Пер-сегментное разрушение (2026-09, ДОРАЗОБРАНО) ───────────────────
 * State 5 («задели», file 0x155FC..0x15666, local 0x97C..0x9E6) —
 * реакция на КОНТАКТ. Пары боксов собирает отдельный проход (file
 * 0x2CB0/0x2D18): при пересечении ОБА объекта получают hit_flag(+0xDD)=1,
 * hit_idx(+0xDF)=индекс партнёра, hit_box_self(+0xE3)=номер СВОЕГО
 * задетого бокса (порт: не заведён отдельным полем — не требуется),
 * hit_box_partner(+0xE1)=номер бокса партнёра. Для b.obj_type==9 (наш
 * случай) generic pair-loop (file 0xFB68, таблица по b.obj_type, индекс 9
 * → file 0xFBCB) ставит ТОЛЬКО b.state_byte=5 и ПОЛНОСТЬЮ ПРОПУСКАЕТ
 * обмен hit_points (см. уже существующее исключение типа 36 в
 * ai_check_player_hit, куда теперь добавлен и тип 20) — этот путь для
 * колонны физически недостижим НЕЗАВИСИМО от значения hp; hp=250/тик
 * (см. выше) — не единственная причина «неубиваемости» генериком.
 * State5 разбирает, кто и куда задел (сравнение partner.[+0xB2] с
 * player.[+0xB2] — идентификация «это тело Карнова»):
 *   Карнов телом, hit_box_self==0 (голова)  → cs:0xE62:0x6FE — урон
 *     Карнову (НЕ портирован — не про выстрелы, вне объёма задачи);
 *     с колонной ничего.
 *   Карнов телом, hit_box_self!=0 (тело)    → просто вернуться в THINK
 *     (state_byte=2, ai_result|=8); эффекта нет ни для кого.
 *   иначе (единственный другой a.obj_type, доходящий сюда, — 2 = снаряд
 *   игрока, table t1 @0xFB3A: только a.obj_type∈{0,2} входят в этот
 *   pair-loop) → ПОПАДАНИЕ СНАРЯДА: `push hit_box_self; call
 *   cs:0x1348:0x638` (file 0x152B8, ЛОКАЛЬНЫЙ хелпер только для типа 20).
 * ai_head_column_hit_box() ниже — байт-точный порт этого хелпера:
 *   self.st_aux(+0xEB) — БИТОВОЕ ПОЛЕ, 2 бита на КАЖДЫЙ box (до 8×2=16
 *   бит) — счётчик попаданий 0..3 персонально для box[idx].
 *   1-й/2-й удар (счётчик 0→1, 1→2): box.sprite = 0xAF+счётчик (176/177),
 *     НО если box.sprite СЕЙЧАС РОВНО 0xAD(173, «непрострелянная голова
 *     покоя») — пропустить смену спрайта (file 0x15308: сравнение именно
 *     с 0xAD, не с 0xAE «поза выстрела» — итого при попадании ровно в
 *     кадр выстрела голова МОЖЕТ на 1 тик получить «повреждённый» спрайт
 *     176/177 — байт-точный, а не сглаженный, квирк оригинала).
 *   3-й удар (счётчик 2→3, МАКС): box.sprite=0 (порт: sprite==0 всюду
 *     значит «не рисуется» — sprite_find(0) не находит ни одного
 *     реального спрайта, см. blit_sprite_world/ai_render), коллизия
 *     бокса тем самым тоже выключена (наш новый per-part hit-test в
 *     ai_player_fireball пропускает parts[i].sprite==0), +400 очков
 *     (add_score(0, BCD 0x400), file 0x1532E) — ЗА КАЖДЫЙ уничтоженный
 *     сегмент отдельно, включая саму голову.
 *   4-й+ удар: счётчик уже насыщен на 3, no-op (file 0x152F4: `ja` —
 *     пропуск дальнейшей эскалации, урон впустую).
 * Голова (box0) гасится ТЕМ ЖЕ путём после 3 попаданий; THINK уже требует
 * `parts[0].sprite != 0`, чтобы стрелять (условие было в коде и ДО этой
 * правки) — таким образом «ослеплённая» голова автоматически замолкает
 * навсегда.
 * ИСПРАВЛЕНО (2026-09, повторный разбор): предыдущий пассаж здесь
 * утверждал, что «в байтах НЕТ кода, который двигал бы box0 или уменьшал
 * num_bboxes» — это было ОШИБКОЙ, пропустившей вызов местного хелпера
 * cs:0x1348:0x59E (file 0x1521E) прямо в хвосте THINK (см. ниже) — именно
 * он и есть механика «столб укорачивается, верхние секции опускаются на
 * место убитых».
 *
 * Хвост THINK (state 2, file 0x15504..0x155FA, local 0x884..0x97A) —
 * ПОЛНОСТЬЮ передизассемблирован (ранее считался «не разобран до конца», а
 * при повторном разборе оказалось, что первый вызов в этом хвосте —
 * ГЛАВНАЯ пропущенная деталь). Каждый тик THINK (безусловно, до всякой
 * проверки видимости) вызывается:
 *   cs:0x1348:0x59E (file 0x1521E, `ai_head_column_settle()` ниже) —
 *     РЕКУРСИВНАЯ просадка секций: box[idx].dy стремится к target_y,
 *     сдвигаясь не быстрее 6 px/тик; «низ» box — top+20, либо просто
 *     top, если box.sprite==0 (мёртвый box не имеет объёма для расчёта
 *     низа — из-за этого его dy утягивается на лишние 20px вниз, «сквозь
 *     пол» стопки). Затем РЕКУРСИЯ на idx-1 (секция ВЫШЕ, ближе к голове)
 *     с заданием встать своим низом ровно на новый верх box[idx] —
 *     БЕЗ зазора. Вызывается изначально с idx=num_bboxes-1 (нижний слот,
 *     ВСЕГДА фиксированный, не «последний живой»), target_y=y_world+
 *     num_bboxes*20 (file 0x15531: cs:0x1348:0x8B1). Итог: убитый сегмент
 *     проваливается на свою высоту НИЖЕ штатного места (визуально скрыт —
 *     sprite==0), а ВСЕ секции над ним, включая саму голову (idx==0),
 *     плавно (по 6px/тик) опускаются на 20px, закрывая брешь — байт-точно
 *     то самое «столб укорачивается снизу, верхние секции спускаются на
 *     место убитых», о котором сообщил игрок. num_bboxes при этом НЕ
 *     уменьшается (dead box просто держит нулевой sprite и «утонувший»
 *     dy) — умозаключение прежнего пассажа о «(dx,dy) фиксированы навсегда»
 *     было неверным именно в части dy; dx действительно не меняется.
 *   cs:0xD4B:0x2AC (file 0xEF5C) — сумма box[i].flag(+0x2E) по всем
 *     боксам; результат сравнивается со снимком в self.phase(+0xED).
 *   если снимок сменился, ИЛИ была просадка в этот тик (флаг из 0x59E),
 *     ИЛИ self.aux(+0xEF)!=0 — вызываются cs:0x6E:0x59A (file 0x247A,
 *     пересчёт экранных box.x0..y1 из x_world/y_world+dx/dy и таблиц
 *     метрик спрайта DS:0x0D08/0x0FAC — порту не нужен: рендер и
 *     хит-тест и так читают dx/dy живьём каждый кадр, см.
 *     blit_sprite_world/ai_player_fireball) и cs:0xD4B:0x2DB (file
 *     0xEF8B) — функция с ДВОЙНЫМ вложенным циклом по строкам/колонкам
 *     карты вокруг нижнего сегмента, которая зовёт тайловые хелперы
 *     (0xECEE/0xECB3) — судя по сигнатуре (запись в массив карты, не в
 *     объект), это ШТАМПОВКА ТВЁРДОГО ГРУНТА под текущим основанием
 *     колонны (перепривязка декорации карты к новой высоте столба). Не
 *     портировано: порт получает геометрию уровня статически из
 *     LEVELn.BIN; сама видимая просадка сегментов (главная жалоба)
 *     портирована через ai_head_column_settle() ниже и от тайловой
 *     штамповки не зависит — реального расхождения без неё не обнаружено.
 * «Видимость на экране» в порту — только по горизонтали (g_camera_left/
 * VIEW_W), как и у типа 23: вертикальная позиция камеры в ai.c не
 * отслеживается (защита порта). */
/* Каскадная просадка секций колонны (cs:0x1348:0x59E, file 0x1521E) — см.
 * разбор в комментарии к ai_head_column выше. Рекурсия идёт от нижнего
 * фиксированного слота (idx=num_parts-1) к голове (idx=0): box[idx]
 * стремится своим низом (top+20, либо просто top, если box.sprite==0 —
 * «мёртвый» box без объёма) достичь target_y не быстрее чем на 6 px/тик;
 * затем box[idx-1] получает задание встать своим низом ровно на новый верх
 * box[idx] — без зазора. Возврат — «было ли движение» (self.aux(+0xEF) в
 * оригинале); порт эту булеву развязку не использует (тайловая штамповка
 * cs:0xD4B:0x2DB не портирована), но сохраняет байт-точную рекурсию. */
static bool ai_head_column_settle(ai_obj_t *o, int idx, int target_y)
{
    if (idx < 0) return false;
    int top = o->y + o->parts[idx].dy;
    int bottom = top + (o->parts[idx].sprite != 0 ? 20 : 0);
    int diff = target_y - bottom;
    if (diff >= 6) diff = 6;               /* file 0x15200: клэмп максимум 6 px/тик */
    if (diff != 0) {
        o->parts[idx].dy = (s16)(o->parts[idx].dy + diff);
        ai_head_column_settle(o, idx - 1, top + diff);
        return true;
    }
    return ai_head_column_settle(o, idx - 1, top);
}

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
        o->st_misc = 0;                               /* +0xE9: таймер готов сразу */
    }
    o->hp = 250;                                       /* file 0x15429: каждый тик */
    o->anim_phase = (frame >> 3) & 1;

    if (o->st_misc > 0) --o->st_misc;
    if (o->parts[0].sprite != 0) o->parts[0].sprite = 173;   /* поза выстрела гаснет */

    bool visible = (o->x + 16 > g_camera_left) && (o->x < g_camera_left + VIEW_W);
    if (visible && o->parts[0].sprite != 0 && o->st_misc == 0) {
        int sx = o->x + o->parts[0].dx, sy = o->y + o->parts[0].dy;
        int py = physics_get_top_y();
        int targets_x[2] = { physics_get_left_x() - 40, physics_get_left_x() + 10 };
        o->parts[0].sprite = 174;
        for (int k = 0; k < 2; ++k) {
            int slot = alloc_obj_slot();
            if (slot < 0) continue;
            ai_obj_t *b = &g_obj[slot];
            *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT, .subtype = 3,
                              .x = sx, .y = sy, .hp = 10, .level_idx = -1,
                              .facing = (targets_x[k] >= sx) ? 1 : -1, .custom = 1 };
            obj_phys_begin(b);
            /* Манхэттен-нормированный вектор × ФИКСИРОВАННАЯ скорость 4.0
             * (cs:0x11D6:0x0006, см. комментарий к обработчику выше) —
             * НЕ obj_aim(). vxq интегрируется в «движковых» X-единицах
             * (1 ед.=2px, obj_integrate потом удваивает — см. её комментарий
             * и obj_phys_begin), а vyq — в пикселях 1:1 (Y не удваивается).
             * Поэтому tx делится на 2 (как player_units()/obj_units() у
             * типов 14/32), а ty — нет. targets_x[]/sx уже в реальных
             * пикселях порта (±40/±10 px = байт-точные ±20/±5 «родных»
             * x_world-единиц ×2), так что деление на 2 здесь и даёт ту же
             * величину, что в оригинале вычисляется напрямую в x_world. */
            {
                int tx = (targets_x[k] - sx) / 2, ty = py - sy;
                int len = (tx < 0 ? -tx : tx) + (ty < 0 ? -ty : ty);
                if (len < 2) len = 2;           /* file 0x135E2: клэмп минимум 2 */
                b->vxq = ((s32)tx * Q_4_0) / len;
                b->vyq = ((s32)ty * Q_4_0) / len;
            }
        }
        o->st_misc = 150 + (int)(rng_next() & 31);     /* 150..181 тиков */
    }

    /* file 0x15531: вызывается КАЖДЫЙ тик, безусловно (до/после проверки
     * видимости не важно — сама функция видимость не проверяет). */
    ai_head_column_settle(o, o->num_parts - 1, o->y + o->num_parts * 20);
}

/* Пер-сегментный урон колонны голов (cs:0x1348:0x638, file 0x152B8) — см.
 * подробный разбор state5 в комментарии к ai_head_column выше. Вызывается
 * из ai_player_fireball при попадании снаряда В КОНКРЕТНЫЙ box (не через
 * общий hp-пул — тот для obj_type==9 генериком вообще не задействуется). */
static void ai_head_column_hit_box(ai_obj_t *o, int idx)
{
    if (idx < 0 || idx >= o->num_parts) return;
    int shift = idx * 2;
    int cnt = (o->st_aux >> shift) & 3;      /* self.st_aux(+0xEB): 2 бита/box */
    if (cnt >= 3) return;                    /* file 0x152F4: уже разрушен — no-op */
    ++cnt;
    if (cnt < 3) {
        /* file 0x15308: сравнение именно с 0xAD(173) — «непрострелянная
         * голова покоя» без промежуточных «повреждённых» кадров; если
         * попадание пришлось на тик firing-pose (174), эта проверка НЕ
         * защищает — байт-точный квирк оригинала, не сглаживаем. */
        if (o->parts[idx].sprite != 173)
            o->parts[idx].sprite = (u16)(0xAF + cnt);   /* 1-й/2-й удар: 176/177 */
    } else {
        o->parts[idx].sprite = 0;    /* 3-й удар: сегмент уничтожен — невидим/
                                       * небьющийся (sprite==0 всюду в порту
                                       * значит «нет спрайта», см. sprite_find) */
        g_score += 400;              /* file 0x1532E: add_score(0, BCD 0x400) —
                                       * за КАЖДЫЙ уничтоженный сегмент отдельно */
    }
    o->st_aux = (int)((o->st_aux & ~(3 << shift)) | (cnt << shift));
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
 * TICK этих типов ПЕРЕДЕКОМПИЛИРОВАН (2026-09, по запросу code-review
 * MULTIPART/STATIC группы). Итог по каждому типу — см. комментарий перед
 * ai_multipart(). Прежняя «волна» сегментов была изобретением порта и к
 * тому же интегрировала собственное dy, из-за чего тело уползало за экран.
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

/* ── Повторная декомпиляция TICK по типам (2026-09) ─────────────────────
 *
 * Тип 21 (handler cs:0x1348:0x1153, file 0x15DD3): state2 (THINK) —
 * ДЕЙСТВИТЕЛЬНО пустой переход на epilogue (file 0x15EF9), без единого
 * ai_result-бита — прежнее «объект статичен» ПОДТВЕРЖДЕНО. INIT
 * дополнительно впечатывает силуэт колонны в тайловую сетку коллизий
 * (6 вызовов cs:0xD4B:0x0003(col,row,val=3), file 0x15F14..0x15F3B),
 * state4/6 (ОБЩИЙ код, file 0x15EF6) снимает её обратно (val=2) — NOT
 * PORTED (нужна запись в level-tile-attr из ai.c, инфраструктуры для
 * рантайм-мутации тайлов пока нет; та же проблема у типов 30/50 ниже).
 * ПОРТИРОВАНО: state5 (file 0x15ED4) — косметика, hit_points<50 →
 * box0.sprite 178(0xB2)→179(0xB3) один раз, назад не возвращается.
 * Ни state4, ни state5, ни state6 НЕ вызывают add_score/die_puff (в
 * отличие от типов 33/34/50 ниже) — смерть типа 21 в оригинале
 * беззвучна и без очков; SCORE_BCD[21] намеренно не заведён.
 *
 * Тип 33 (handler cs:0x151A:0x00CD, file 0x16A6D): «схлопывание».
 * ПЕРЕДЕКОМПИЛИРОВАНО (2026-09-24, жалоба игрока: «раскладывается от
 * касания, а должно — от взрыва Бомбы»). Прежний пассаж выше описывал
 * сам HURT (state4, file 0x16C10: если misc(+0xE9)&~1==0 → misc=2,
 * state_byte=2), но не то, ЧЕМ он реально взводится — из-за этого порт
 * считал ЛЮБОЙ удар (включая простое касание телом) достаточным.
 * На самом деле вход в state4 решает ОБЩАЯ цепочка THINKCYCLE
 * (cs:0xD4B:0xE03, file 0xFAB3) — ТА ЖЕ, что уже раздекомпилирована для
 * типа 36/Рю (см. выше, file 0xFC18 и далее). У типа 33 obj_type=9
 * (EnemySpawn.subtype=9, LEVEL3.BIN idx6, hp=95), и диспетчер по
 * a.obj_type для b.obj_type==9 ведёт себя по-разному в зависимости от
 * того, ЧТО ударило:
 *   a.obj_type==0 (тело Карнова, простое касание) → таблица file 0xFB68
 *     индекс 9 → file 0xFBCB: ставит ТОЛЬКО b.state_byte=5, БЕЗ обмена
 *     hit_points — state4 (и коллапс) отсюда НИКОГДА не достижим. Порт
 *     ошибочно пускал касание через симметричный обмен hit_points
 *     (ai_check_player_hit), из-за чего (а) касание запускало коллапс и
 *     (б) Карнов терял ~95 HP колонны при касании — обе половины бага
 *     правятся добавлением type 33 в исключения ai_check_player_hit
 *     (как уже сделано для 20/36).
 *   a.obj_type==2 (снаряд Карнова ИЛИ вспышка Bomb — она тоже obj_type=2,
 *     см. её INIT cs:0xFFA:0x0A93, file 0x12251) → таблица file 0xFC06
 *     индекс 9 → file 0xFC18: ставит b.state_byte=5 И ПРОВАЛИВАЕТСЯ в
 *     обычный симметричный обмен (file 0xFC21: b.hp -= a.hp_orig);
 *     ТОЛЬКО если получившийся b.hp <= 0, file 0xFC50 перезаписывает
 *     b.state_byte на 4 в этом же кадре — вот настоящий вход в
 *     HURT/коллапс. Файербол Карнова несёт hit_points=10 (cs:0xFFA:0x0009,
 *     file 0x117C2), у колонны hp=95 — одного выстрела мало (нужно
 *     около 10 попаданий: state2/misc0-1 залочивает hit_points на
 *     снимок КАЖДЫЙ тик, file 0x16CCB, но после первого же удара
 *     state_byte застревает на 5 и THINK для этого объекта больше не
 *     вызывается — снимок не обновляется, урон копится). Вспышка Bomb
 *     несёт hit_points=100 (file 0x12291) — валит колонну ОДНИМ
 *     попаданием (95-100<0 сразу). Порт воспроизводит это явной
 *     постепенной моделью в ai_notify_enemy_hit (case 33) и в
 *     ai_bomb_explode/ai_clear_enemies (тот же путь с уроном 100 вместо
 *     прямого снятия alive) — см. там.
 * Собственно коллапс (THINK-подфазы 2/3, file 0x16C40..0x16C80): каждый
 * тик все сегменты, кроме головы, с dy != 0 получают dy += 2 (пиксели —
 * Y не масштабируется) и dx += 2 В ЕДИНИЦАХ ДВИЖКА (file 0x16C80:
 * `add word[bx+2],2` — то же поле box.dx, что и x_world, тоже требует
 * ×2 для пикселей), т.е. += 4 px. Порт хранит PARTS_33.dx уже в пикселях
 * (см. комментарий к таблице), поэтому инкремент здесь тоже должен быть
 * +4, а не +2 — прежний +2 давал вдвое меньший разлёт (макс. ~64px) и
 * не перекрывал 112-пиксельную яму LEVEL3 (теперь — до ~132px на самом
 * глубоком сегменте). Голова становится спрайтом 232 (0xE8). Когда все
 * dy == 0 — коллапс завершён (phase 4, one-shot, затем phase>4 — THINK
 * навсегда no-op). С началом коллапса box0.hitflag снимается (file
 * 0x16BB3) — объект становится НЕУБИВАЕМ, просто застывшая груда
 * обломков (см. is_damageable_obj: `type==33 → st_phase<2`).
 *
 * Тип 34 (handler cs:0x151A:0x02A6, file 0x16C46): «ждать игрока в
 * ~100 px» — предыдущая формулировка была НЕВЕРНОЙ вдвое: реальный порог
 * (file 0x16CCA) — `|player.x_world − self.x_world| < 0x64` (100 ЕДИНИЦ
 * движка = 200 px). После срабатывания включается «волна» — голова
 * (box0) раз в ~30 тиков пишет новое значение в кольцевой буфер
 * DS:0xD97E/0xD982 (20 записей) по таблицам DS:0x1BF0/0x1BFE/0x1C0C и
 * хелперу cs:0x1C92:0xE, а 5 тел (box 1..5) читают из этого буфера свои
 * y-смещения с задержкой (классический snake-follow). Таблицы волны и
 * хелпер НЕ раздекомпилированы до конца (нет уверенности в форме кривой)
 * — портирован только подтверждённый порог активации; сама волна
 * оставлена как явный TODO, а не придумана.
 *
 * ДОБАВЛЕНО (повторный code-review): у типа 34 порог активации был
 * единственной портированной механикой THINK — но настоящий «геймплей»
 * лежит в STATE 4 (HURT, file 0x16DE7..0x16E5D), который вообще не
 * зависит от misc/порога и выполняется на КАЖДЫЙ удар:
 *   hit_points = снимок (self.phase(+0xED) = hp при спавне — залочен,
 *     обычная генерик-смерть по hp<=0 сюда НЕ применима);
 *   box[phase_ctr].sprite = 0 (текущий верхний сегмент прячется, начиная
 *     с индекса 5 — самый нижний слой 233 — и далее вверх к 0 = 238);
 *   если phase_ctr != 0: --phase_ctr (открыть хитбокс следующего, ниже
 *     лежащего сегмента), state_byte = 2 (вернуться в THINK);
 *   иначе (все 6 сегментов сняты): state_byte = 6 (смерть) → в конце,
 *     ОБЩИЙ для обеих веток add_score(0x110); а по ветке смерти —
 *     ЕЩЁ РАЗ add_score(0x110) (110 очков дважды) и die_puff() (file
 *     0x16E52..0x16E5D). Порт не имеет per-type HURT-хука на «любой
 *     удар», поэтому механика вынесена в ai_notify_enemy_hit (см. ниже,
 *     case type==34) — hp никогда не уходит в generic-смерть напрямую.
 *
 * Тип 39 (handler cs:0x161F:0x0002, file 0x179F2): подтверждён почти
 * полностью верным — единственная неточность: направление прокрутки
 * кадров (259..262) — это не всегда «+1»: INIT считает
 * `misc = (y_world % 10 != 0) ? 3 : 1` (file 0x17A46) ОДИН РАЗ при спавне,
 * и весь THINK крутит `sprite = 259 + ((sprite−259+misc)&3)`; misc=3
 * (≡ −1 mod 4) даёт обратный цикл для объектов, чей y_world не выровнен
 * по тайловой сетке. Никакого движения/реакции на игрока в THINK нет.
 *
 * Тип 50 (handler cs:0x175E:0x000F, file 0x18DEF): «триггер по позиции
 * игрока» — реален, но НЕ дистанция до игрока, а level-geometry cleanup:
 * (file 0x18EDB) если `player.x_world > 0x2D0` (720 ед. = 1440 px) И
 * `player.vy > 0` (игрок падает/спускается) — self.hit_points = −30,
 * state_byte принудительно 4 (HURT) на следующий тик; HURT/DELETE (file
 * 0x18F3A) снимают тайловую разметку и просто исчезают — БЕЗ add_score/
 * die_puff (это не обычная смерть от урона, а бесшумная уборка моста за
 * пройденной точкой). Порт не читает знак vy напрямую (physics.c его не
 * экспортирует) — аппроксимируем «спускается» по росту top_y между
 * тиками (st_ctr хранит предыдущий top_y для ЭТОГО объекта). Также
 * портирован найденный попутно hp-порог (state5, file 0x18F18):
 * hit_points < 50 → голова box0.sprite меняется 0x132(306)→0x133(307).
 */
static void ai_multipart(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    if (o->num_parts == 0) {
        switch (o->type) {
        case 21: parts_apply(o, PARTS_21, 7); break;
        case 33: parts_apply(o, PARTS_33, 7);
                 o->st_aux   = o->hp;    /* +0xEB: снимок hp */
                 o->st_phase = 0;        /* +0xE9: misc/подфаза коллапса */
                 break;
        case 34: parts_apply(o, PARTS_34, 6);
                 o->st_misc  = 0;        /* 0 = ждём приближения игрока */
                 o->st_phase = 5;        /* +0xEB: индекс верхнего живого
                                          * сегмента (5..0), см. ai_notify_
                                          * enemy_hit case 34 — снимается по
                                          * одному на каждый удар. */
                 o->st_aux   = o->hp;    /* +0xED: снимок hp (в оригинале
                                          * это ТОЖЕ поле «phase», не
                                          * связано с подфазой). */
                 break;
        case 39: parts_apply(o, PARTS_39, 6);
                 /* file 0x17A46: направление один раз при спавне. */
                 o->st_misc = (o->y % 10 != 0) ? 3 : 1;
                 break;
        case 50: parts_apply(o, PARTS_50, 7);
                 o->st_aux = physics_get_top_y();  /* для детекции спуска игрока
                                                     * (st_ctr занят общим +0xC7
                                                     * frame_ctr, инкрементируется
                                                     * каждый кадр диспетчером). */
                 break;
        default: parts_apply(o, PARTS_33, 4); break;
        }
    }

    switch (o->type) {
    case 21:
        /* file 0x160F0 (state5): hit_points<50 → голова box0.sprite
         * 178(0xB2)→179(0xB3) один раз, назад не возвращается. Раньше
         * не портировано (см. комментарий у PARTS_21). */
        if (o->hp < 50 && o->parts[0].sprite != 179) o->parts[0].sprite = 179;
        break;
    case 33: {
        int si = o->st_phase;
        if (si <= 4) {
            switch (si) {
            case 0:
                /* file 0x16CCB: пока не было НАСТОЯЩЕГО попадания снаряда/
                 * бомбы (ai_notify_enemy_hit/ai_bomb_explode), hit_points
                 * каждый тик восстанавливается на снимок — касание сюда
                 * вообще не заходит (ai_check_player_hit исключает type 33
                 * из symmetric-обмена, см. коммент к ai_multipart выше). */
                o->hp = o->st_aux;
                break;
            case 1:
                /* «Разбужена» неубойным попаданием (hp>0 после удара) — в
                 * оригинале state_byte застрял на 5, THINK больше не
                 * перезапускает hp на снимок; просто ждём накопления урона. */
                break;
            case 2:
            case 3: {
                o->st_phase |= 1;
                bool changed = false;
                o->parts[0].sprite = 232;                 /* коллапс-поза головы */
                for (int i = 1; i < o->num_parts; ++i) {
                    if (o->parts[i].dy != 0) {
                        o->parts[i].dy = (s16)(o->parts[i].dy + 2);
                        /* file 0x16C80: инкремент box.dx — В ЕДИНИЦАХ
                         * ДВИЖКА (+2 = +4px), а не в пикселях; PARTS_33.dx
                         * в порту уже переведён в пиксели, поэтому и шаг
                         * должен быть +4 (см. комментарий к ai_multipart). */
                        o->parts[i].dx = (s16)(o->parts[i].dx + 4);
                        changed = true;
                    }
                }
                if (!changed) o->st_phase = 4;            /* коллапс завершён */
                break;
            }
            default: break;                               /* phase 4: one-shot, дальше no-op */
            }
        }
        break;
    }
    case 34: {
        if (!o->st_misc) {
            int d = physics_get_left_x() - o->x;
            if (d < 0) d = -d;
            if (d < 200) o->st_misc = 1;   /* разбужен; волна тела — TODO (см. коммент) */
        }
        break;
    }
    case 39:
        /* Единственная портированная TICK-логика: у type 39 каждый бокс
         * прокручивает кадр в диапазоне 0x103..0x106 (file 0x17ABB),
         * направление задаёт misc из INIT (+1 либо −1 mod 4). */
        if ((frame & 3) == 0) {
            int step = o->st_misc ? o->st_misc : 1;
            for (int i = 0; i < o->num_parts; ++i) {
                u16 f = (u16)((o->parts[i].sprite - 259 + step) & 3);
                o->parts[i].sprite = (u16)(259 + f);
            }
        }
        break;
    case 50: {
        int py = physics_get_top_y();
        bool descending = py > o->st_aux;
        o->st_aux = py;
        if (physics_get_left_x() > 1440 && descending) {
            o->alive = false;      /* бесшумная уборка — без score/puff */
            return;
        }
        if (o->hp < 50 && o->parts[0].sprite != 307) o->parts[0].sprite = 307;
        break;
    }
    default: break;
    }
    o->anim_phase = (frame >> 3) & 1;
}



/* Type 30 — растущая цепь (ai_type30, handler cs:0x1348:0x12CF, file
 * 0x15F4F — НЕ file 0x15FB5, это внутримодульное смещение). num_bboxes
 * начинается с 0 и растёт во времени; спрайты чередуются 0x05 / 0x98 (152).
 *
 * Повторная декомпиляция (code-review 2026-09) подтвердила рост цепи (см.
 * ниже) и нашла ДВЕ непортированные механики за пределами THINK-роста:
 *  1. INIT (file 0x15F79..0x1608D) точно как у типов 20/21/50 впечатывает
 *     силуэт в тайловую сетку коллизий (12 клеток: 2 колонки — x_world и
 *     x_world+152 ед. — по 6 строк вниз, cs:0xD4B:0x0003(col,row,val=3));
 *     state6 (file 0x160FC..0x161AA) восстанавливает те же 12 клеток через
 *     cs:0xD4B:0x3E(col,row)→lookup[id]→cs:0xD4B:0x3 (т.е. не жёсткое
 *     значение, а «настоящий» атрибут исходного тайла под перекраской).
 *     NOT PORTED — нужна рантайм-мутация level_tile_attr, инфраструктуры
 *     нет (см. тот же пробел у типов 20/21/50).
 *  2. THINK (file 0x160D5..0x160F9, ПОСЛЕ роста) читает общий глобал
 *     [DS:0xD97A] (сам обнуляет его в СВОЁМ ЖЕ INIT, file 0x15F79), OR'ит
 *     в phase(+0xED) (залипающий флаг «когда-либо был не 0»), и если
 *     залипший флаг взведён И [0xD97A] сейчас не 0 → state_byte=6 (смерть,
 *     ведёт к пункту 1). UNRESOLVED: кто ещё пишет [0xD97A] — не найдено
 *     (сканы E8/9A по кандидатным сегментам не выполнялись из-за объёма
 *     задачи); похоже на внешний уровневый триггер («выключить цепь»), НЕ
 *     портирован — в порту цепь никогда не убирается этим путём, только
 *     generic hp<=0. */
static void ai_grow_chain(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)karnov_input;
    /* INIT file 0x15F79: спрайт КОНСТАНТНЫЙ 0xDA (218); чередуются
     * x_offset 152 и 5 (единицы → 304 и 10 px), dy = (i>>1)*20.
     * C++-реконструкция принимала 0x98/0x05 за sprite_id — отсюда была
     * «цепь из 152/5». TICK (file 0x1608E): +2 бокса каждые 11 тиков —
     * ПОДТВЕРЖДЕНО повторной декомпиляцией, числа совпали в точности. */
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

/* Type 36 — ★ Рю, финальный босс (handler cs:0x151A:0x061B, file 0x16FBB,
 * до cs:0x151A:0x08AF — дальше начинается обработчик типа 35).
 *
 * ЭТА ВЕРСИЯ (2026-09, повторный проход): полностью раздекомпилирован
 * cs:0xD4B:0x2DB, проведён исчерпывающий скан ВСЕГО KARNOV.EXE (все 8
 * регистровых форм mov-инструкций) на запись misc(+0xE9)=2, чтение и
 * запись phase(+0xED), запись state_byte(+0xB6)=6 у типа 36, и найден
 * реальный механизм конца уровня 9. Итог см. в блоке «ЧТО ЭТО ЗНАЧИТ»
 * ниже — он МЕНЯЕТ трактовку "настоящего триггера конца боя": его нет,
 * потому что Рю в оригинале в принципе не умирает.
 *
 * ПОДТВЕРЖДЕНО по листингу (без изменений с прошлого прохода):
 *  INIT (file 0x16FBB..0x17058): x_world &= ~7 (сетка 8 px), num_bboxes=2,
 *  box0=голова 0xF5 (dx=+2 ед.=+4px, dy=0, hitflag=0 — ГОЛОВА НЕ бьётся),
 *  box1=хвост 0xF9 (dx=0, dy=0 буквально, hitflag=1 — бьётся хвост);
 *  self.phase_ctr(+0xEB) = y_world + h(0xF5) + h(0xF9) (уровень «покоя»).
 *  Порт хардкодит box1.dy=20 (высота головы) — в ASM это отдельное поле
 *  +0x36, куда именно копируется в box1.dy, не прослежено.
 *
 *  cs:0xD4B:0x2DB (file 0xEF8B) РАЗДЕКОМПИЛИРОВАН ПОЛНОСТЬЮ (2026-09).
 *  Сигнатура: stamp_body_column(far self, y_val, phase_ctr, flag=1).
 *  Вызывает update_bboxes(self) (cs:0x6E:0x59A, file 0x0247A — пересчёт
 *  x0/y0/x1/y1 боксов из x_world/y_world/dx/dy), затем через таблицы
 *  DS:0xAF22 (пиксель→ряд карты) и DS:0xBB5E (пиксель-колонка box0.x0 >>
 *  tile_shift → индекс колонки) вычисляет ЕДИНСТВЕННУЮ колонку карты
 *  (колонка box0.x0) и диапазон рядов [row(y_val)..row(phase_ctr)).
 *  Для верхнего ряда столбца читает исходный тайл-ГРАФИКУ (helper
 *  cs:0xD4B:0x3E, file 0xECEE — читает байт [row_ptr[row]+col*2]),
 *  переводит через таблицу DS:0xABC0 в код категории и записывает его
 *  в АТРИБУТ-байт [row_ptr[row]+col*2+1] через cs:0xD4B:0x3 (file
 *  0xECB3 — тот же байт, что читает tile_at_cell cs:0xD4B:0x21). Затем
 *  для ВСЕХ рядов от row(y_val) до row(phase_ctr) записывает готовую
 *  категорию: 4 — верхний ряд, 3 — все нижеследующие (обе входят в
 *  «твёрдые» категории по jump-table cs:0x151A:0x0839 типа 35, где
 *  0/3/4/7 = «твёрдая земля», 1/2/5/6 = нет). Иными словами: функция
 *  ВПЕЧАТЫВАЕТ тело Рю КАК ТВЁРДУЮ ГЕОМЕТРИЮ УРОВНЯ в одну колонку
 *  тайловой карты — не косметика, а реальное препятствие для игрока.
 *  ПОРТ ЭТО НЕ ВОСПРОИЗВОДИТ: `level_tile_attr[]` в порту индексируется
 *  ПО ГРАФИКЕ тайла глобально на весь уровень (karnov_c/src/level.c:22),
 *  а не по конкретной ЯЧЕЙКЕ карты, как в оригинале — точечный стемпинг
 *  одной колонки без порчи всех других тайлов с той же графикой ВО ВСЁМ
 *  уровне текущей моделью данных не выразить. Это требует переделки
 *  level.c (per-cell overrides), т.е. выходит за рамки типа 36.
 *
 *  Вызывается ДВАЖДЫ: из INIT (state1, y_val=y_world+box0.dy=y_world
 *  спауна) и из THINK при misc==2 (тем же y_val/phase_ctr — при первом
 *  входе это ТОЖДЕСТВЕННЫЙ повторный стемпинг), А ЗАТЕМ КАЖДЫЙ ТИК, пока
 *  misc==3 (см. ниже) — то есть столбец «закрывается» сверху по мере
 *  падения головы, но диапазон всегда завершается на ФИКСИРОВАННОМ
 *  phase_ctr, заданном один раз при спауне.
 *
 *  THINK (state 2) реагирует ТОЛЬКО на misc(+0xE9) == 2 или 3, иначе —
 *  no-op КАЖДЫЙ тик (без внешнего триггера Рю ничего не делает):
 *    misc=2 — ОДНОРАЗОВЫЙ вход, ПАДАЕТ СРАЗУ В ТОТ ЖЕ ТИК В misc=3
 *    (это не «следующий тик», а fallthrough в самом дизасме, file
 *    0x0722→0x0732 без jmp между ними): num_bboxes=1 (хвост СКРЫВАЕТСЯ
 *    и обратно НИКОГДА не показывается — во всём обработчике 36 есть
 *    РОВНО два места, пишущих num_bboxes: INIT=2 и это misc=2 вход=1,
 *    восстановления нет), vy=+1.0, re-stamp;
 *    misc=3 — падение продолжается (re-stamp КАЖДЫЙ тик), пока
 *    y_world+h(box0.sprite) < phase_ctr; по достижении — vy=0, misc=0,
 *    финализация update_bboxes(self) (cs:0x6E:0x52B — здесь это НЕ та же
 *    функция, что 0x59A; расхождение вызовов не критично для порта).
 *  ⇒ Рю падает («садится», пряча хвост НАВСЕГДА) РОВНО ОДИН РАЗ — при
 *  ПЕРВОМ попадании, после которого misc больше никогда не станет 2,
 *  ЕСЛИ он уже находится в покое (см. ниже про state4).
 *
 *  ИСЧЕРПЫВАЮЩИЙ СКАН misc(+0xE9)=2: во всём KARNOV.EXE найдено 35 мест
 *  вида `mov word[es:bx+0xe9],2`; при разбиении по «владельцу адреса»
 *  (largest handler start ≤ address) РОВНО ОДНО принадлежит типу 36 —
 *  это САМ state4 (file 0x17182 = его собственный `mov …,2`). Ни одна
 *  другая функция в бинарнике не устанавливает misc=2 объекту типа 36.
 *  Значит «внешнего скрипта», который бы стартовал падение отдельно от
 *  боевого попадания, НЕ СУЩЕСТВУЕТ — единственный способ инициировать
 *  «посадку» — это попадание (state4).
 *
 *  State 4 (получил урон) НЕ вызывает add_score/die_puff: ставит misc=2
 *  и state_byte=2 напрямую — то есть «сесть» (спрятать хвост, чуть
 *  просесть до phase_ctr) как реакцию на попадание, БЕЗ ПРОВЕРКИ hp.
 *  Ни в одном состоянии 0..6 этого обработчика НЕТ add_score/die_puff/
 *  ai_result|=1(удаление) — state6 (удаление) присутствует лишь как
 *  голая заглушка `ai_result|=1`, и НИЧТО в бинарнике никогда не пишет
 *  Рю (36) state_byte=6 (проверено сканом всех записей state_byte=6 по
 *  владельцу адреса — их нет для типа 36). Рю НИКОГДА не удаляется
 *  собственным кодом.
 *
 *  ⇒ ГЕНЕРИК-ЦЕПОЧКА ПАРНОГО УРОНА (THINKCYCLE, file 0xFAB3, сегмент
 *  тот же cs:0xD4B) решает, какое из состояний 4/5 реально применяется:
 *  для объекта с obj_type∈{7,9} (Рю в LEVEL9.BIN — record[2]: type=36,
 *  subtype=9, ЦЕЛИКОМ НУЛЕВОЙ: cur=(0,0), range=(0,0), spawn=(0,0),
 *  trigger=0 — как и у type-8 sky-цепи, спаунится НЕ через нормальный
 *  check_triggers (range=0 ⇒ строгое «<» никогда не выполняется), а
 *  отдельным, не найденным в этом проходе кодом) при ударе снарядом
 *  игрока (a.obj_type==2) generic-код (file 0xFC18) СНАЧАЛА
 *  безусловно ставит b.state_byte=5, обменивает hit_points
 *  (b.hp -= a.hp_orig; a.hp -= b.hp_orig — тот же симметричный обмен,
 *  что и во всех столкновениях), и ЕСЛИ b.hp>0 после обмена — этим и
 *  ограничивается (state5 исполнится на следующем ai_dispatch). ЕСЛИ ЖЕ
 *  b.hp<=0 — file 0xFC49..0xFC5A ПЕРЕЗАПИСЫВАЕТ b.state_byte НА 4 (в
 *  этом же кадре, до диспетчеризации — state5 в такой кадр НЕ
 *  исполняется) И ИГРАЕТ generic play_sound(1) («враг убит»), НИКАК
 *  иначе не помечая объект: НЕТ award_score, нет ai_result|=1, объект
 *  остаётся alive. Далее (file 0xFC61) код проверяет, не является ли
 *  убитый объект ГЛОБАЛЬНЫМ goal-индексом DS:0xD959 — но на уровне 9 в
 *  DS:0xD959 попадает индекс ОТДЕЛЬНОГО объекта type=56 (LEVEL9.BIN
 *  record[142]), а НЕ Рю, так что для Рю эта ветка тоже не срабатывает.
 *  ⇒ ПОДТВЕРЖДЕНО: у Рю НЕТ понятия «смерть» вообще. Удар «насмерть»
 *  (hp<=0) отличается от обычного ТОЛЬКО тем, что state4 запускается
 *  вместо state5 (тот же «сесть», без add_score/died) и один раз играет
 *  SFX «kill», а НЕ фактическим удалением/очками/концом уровня.
 *
 *  State 5 смотрит obj_type ПАРТНЁРА по хиту ([+0xDF]→запись в DS:0x2824,
 *  +0xE5=obj_type): если == 2 (снаряд игрока, гарантировано generic-кодом
 *  выше — эта ветка вообще недостижима иначе), увеличивает phase(+0xED)
 *  — счётчик попаданий «в слабое место»; box1.sprite меняется на 0xFA
 *  (250) — «раненый хвост». ИСЧЕРПЫВАЮЩИЙ СКАН чтения phase(+0xED) (все
 *  8 регистровых форм mov reg,[es:bx+0xed] по ВСЕМУ EXE) даёт 15 мест
 *  суммарно, из них РОВНО ОДНО принадлежит типу 36 — сама эта строка
 *  state5 (запись, не сравнение). НИ ОДНО место во всём бинарнике не
 *  СРАВНИВАЕТ phase(+0xED) с порогом. Счётчик попаданий НИКЕМ не
 *  читается — это мёртвая переменная (или недостающий вырезанный
 *  контент), а не скрытый счётчик побед.
 *
 *  Функции cs:0x151A:0x07FA (движение к полу через tile_at + place_on_
 *  ground_center/set_motion) и cs:0x151A:0x0870 (прицельный снаряд через
 *  cs:0x11D6:0x139) физически лежат МЕЖДУ обработчиками 36 и 35 в одном
 *  сегменте, но вызываются ТОЛЬКО из кода типа 35 (проверено сканом
 *  E8-вызовов в диапазоне 0x633..0x7F9 — их нет): периодическая стрельба
 *  шарами — НЕ часть обработчика типа 36.
 *
 *  ЧТО ЭТО ЗНАЧИТ ДЛЯ ПОРТА: Рю — «стена с лицом», не убиваемый босс в
 *  привычном смысле. Настоящее «прохождение» уровня 9 в оригинале не
 *  завязано на смерть Рю вообще (реальный триггер конца — отдельный
 *  таймерный объект type56 у ДАЛЬНЕГО края арены, независимый от Рю;
 *  порт этого сейчас не реализует и не должен — это вне типа 36).
 *  Раз честного «конца боя» у самого Рю нет, порт реализует РОВНО то,
 *  что подтверждено: удары ДО исчерпания hp дают state5-реакцию
 *  (счётчик++, хвост «ранен»), удар, обнуляющий hp, даёт ОДНОРАЗОВУЮ
 *  «посадку» (прячет хвост навсегда, играет generic kill-SFX) БЕЗ
 *  award_score/despawn/is_boss — Рю остаётся вечно на карте, вечно
 *  бьётся дальше (hp может уходить в минус бесконечно, как в оригинале:
 *  ничто не защищает от повторного входа в эту же ветку). Порт НЕ
 *  придумывает второй, «настоящий» конец боя — это и есть честный,
 *  подтверждённый листингом результат. Тайл-стемпинг (реальная твёрдая
 *  геометрия) сознательно не портирован — см. блок выше. */
static void ai_ryu(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)frame; (void)karnov_input;
    if (o->num_parts == 0) {
        /* INIT file 0x16FFF/0x17017/0x17058: x_world &= ~7 (сетка 8px);
         * голова 0xF5 при dx=+2 ед. (=+4 px), dy=0; хвост 0xF9 при dx=0,
         * dy=h(0xF5)=20 — РОВНО ПОД головой. */
        o->x = ((o->x / 2) & ~7) * 2;
        o->num_parts = 2;
        o->parts[0].sprite = 245; o->parts[0].dx = 4; o->parts[0].dy = 0;
        o->parts[1].sprite = 249; o->parts[1].dx = 0; o->parts[1].dy = 20;
        o->st_misc  = 0;   /* misc(+0xE9): 0/1 = покой (no-op), только 2/3 активны */
        o->st_phase = 0;   /* phase(+0xED): счётчик попаданий «в слабое место» (нигде не читается) */
        /* phase_ctr(+0xEB) = y_world + h(0xF5) + h(0xF9) — уровень «покоя» */
        const sprite_t *s0 = sprite_find(245), *s1 = sprite_find(249);
        o->st_aux = o->y + (s0 ? s0->h : 20) + (s1 ? s1->h : 20);
        /* facing нигде в обработчике не трогается (нет ai_result|=2 ни
         * в INIT, ни в THINK) — Рю никогда не поворачивается, в отличие
         * от прежнего стенд-ина, который считал его лицом к игроку. */
        obj_phys_begin(o);   /* нужен Q16.16-интегратор для vy=+1.0 при падении */
    }

    /* THINK: no-op, если misc не 2 и не 3 (file 0x16FD9..0x1671E). */
    if (o->st_misc == 2) {
        /* Одноразовый вход ПАДАЕТ В misc=3 в ТОМ ЖЕ тике (fallthrough
         * в дизасме, без jmp между блоками file 0x0722/0x0732). */
        o->num_parts = 1;         /* хвост скрыт — и уже НИКОГДА не восстановится */
        o->vyq = QV(1, 0);        /* vy = +1.0 */
        o->st_misc = 3;
    }
    if (o->st_misc == 3) {
        const sprite_t *s0 = sprite_find(o->parts[0].sprite);
        int h0 = s0 ? s0->h : 20;
        if (o->y + h0 >= o->st_aux) {
            o->vyq = 0;
            o->st_misc = 0;
        }
        /* иначе — падение продолжается, vy=+1.0 уже выставлена, ждём
         * следующего тика (Q16.16-интегратор двигает o->y сам). */
    }
}





/* Отдельный объект-«столб арены», найденный и раздекомпилированный
 * 2026-09 по адресу cs:0x175E:0x3A9 (file 0x19189, ЛЕЖИТ ПЕРЕД
 * обработчиком типа 51 в том же сегменте, вне таблицы DS:0x10FE —
 * поднимается через init_active_object напрямую из state4/state6
 * родителя, а не через primary-type диспетчер).
 *
 * INIT (file 0x191B3): tick_period=0xA(10); num_bboxes=8; для i=0..7:
 * box[i].sprite=0x13D(317), dx=0, dy=i*10, hitflag=0 (ВСЕ 8 боксов
 * неуязвимы). Координаты объекта — АБСОЛЮТНЫЕ ХАРДКОД, не унаследованы
 * от родителя: x_world=0x300(768 ед.=1536px), y_world=0x136(310px).
 * Финал INIT — cs:0x6E:0x52B(self) (тот же хелпер пересчёта общего
 * bbox, что и у Рю). THINK (file 0x19228, throttl tick_period=10) зовёт
 * нерасшифрованный cs:0x175E:0x0366(2) и обнуляет неопознанное поле
 * box+0x10 на каждом боксе — видимого эффекта на геометрию/спрайты нет,
 * НЕ портировано (нет уверенности в смысле). State 6 НЕ удаляет объект
 * (просто state_byte=2, возврат в THINK) — столб перманентен.
 * cs:0x175E:0x0366/0x01A3 (звук/эффект тряски?) — UNRESOLVED. */
static void ai_arena_pillar(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)frame; (void)karnov_input;
    if (o->num_parts == 0) {
        o->x = 1536;             /* file 0x191E1: x_world=0x300 ед. → ×2 px */
        o->y = 310;              /* file 0x191E7: y_world=0x136 px */
        o->num_parts = 8;
        for (int i = 0; i < 8; ++i) {
            o->parts[i].sprite = 317;
            o->parts[i].dx = 0;
            o->parts[i].dy = (s16)(i * 10);
        }
        o->facing = 1;
    }
}

/* Type 51 — статический элемент арены (ai_type51): 8 частей, sprite 0x135
 * (309), y выровнен по 10, sub_timer_max=0xFF (вечный).
 *
 * ИСПРАВЛЕНО (2026-09): настоящее 8-сегментное тело арены (столб из
 * спрайтов 0x13D=317 с шагом 10) — ОТДЕЛЬНЫЙ объект по адресу
 * cs:0x175E:0x3A9 (см. ai_arena_pillar выше), который оригинал поднимает
 * через init_active_object ПРЯМО в state4 (HURT) И в state6 (DELETE)
 * родителя (file 0x1951C/0x19519 — одна и та же ветка на оба состояния),
 * с абсолютными хардкод-координатами, НЕ зависящими от позиции/повторности
 * родителя. Спаун портирован в ai_notify_enemy_hit (там, где реально
 * происходит переход в hp≤0 — см. `if (o->type == 51)` ниже), а не здесь:
 * при hit_points=1 (обычное значение из уровня, trigger=0 → hp=1) удар и
 * смерть происходят в ОДНОМ кадре, и TICK-функция этого объекта никогда
 * не видит промежуточного «ранен, но жив» состояния.
 * UNRESOLVED: что мешает оригиналу спаунить дубликаты столба при
 * повторных попаданиях (если бы hp был >1) — guard в state4/state6 не
 * найден; также не подтверждено, начисляет ли оригинал generic
 * add_score/die_puff при этом переходе (в самом state4 их нет — см.
 * комментарий у ai_arena_pillar) — порт использует обычный generic-death
 * путь (тот же, что и у любого другого врага) как наименее рискованное
 * приближение. */
static void ai_arena_static(ai_obj_t *o, u16 frame, u8 karnov_input)
{
    (void)frame; (void)karnov_input;
    if (o->num_parts == 0) {
        /* INIT (file 0x192A0): num_bboxes=8, но заполняется только
         * bbox[0] = спрайт 0x135 (309) в (0,0), плюс y выравнивается
         * по 10. Прежняя сетка 4×2 из 309 была изобретением порта. */
        o->y -= o->y % 10;
        o->num_parts = 1;
        o->parts[0].sprite = 309;
        o->parts[0].dx = 0;
        o->parts[0].dy = 0;
    }
}

/* Тип 41 — проверено заново (2026-09), см. запрос code-review для типов
 * 14/32/36/38/40/41. Обработчик РЕАЛЬНО существует и не заглушка в
 * оригинале: cs:0x161F:0x020A, file 0x17BFA, states 0/1/2/4/6.
 * INIT: obj_type=3 (снаряд), hit_points=0x64(100), box0.hitflag=1,
 * tick_period=2, state_list=DS:0x1CBE. THINK доигрывает анимацию
 * (state_list DS:0x1CF2 после кадра 0), и когда frame_idx достигает
 * [DS:0x1CCE]−1, зовёт cs:0x161F:0x00FC — та порождает 5 ДОЧЕРНИХ
 * объектов С ТЕМ ЖЕ обработчиком (0x020A:0x161F), т.е. type 41
 * самовоспроизводится очередью «осколков» с прицельной скоростью
 * (через тот же векторный хелпер cs:0x11D6:0x0006, что и у 0x139) и
 * ay=+0.172 — похоже на разлёт обломков какого-то объекта.
 *
 * Проверено эмпирически: ни в одном из LEVEL0.BIN..LEVEL9.BIN (все 10
 * уровней, скрипт по формату spawn_oracle.load_records) нет записи с
 * type==41 — прежнее утверждение подтверждено.
 * Проверено по коду: кто создаёт ПЕРВЫЙ (не самопорождённый) экземпляр
 * типа 41 — НЕ НАЙДЕНО. Исчерпывающий скан всего KARNOV.EXE (байтовый
 * шаблон `mov ax,0x020A` — единственный ОПЕРАТОР, задающий handler_off
 * для этого хендлера при вызове init_active_object; и отдельно скан
 * дальних вызовов 9A на 0x161F:0x020A) даёт РОВНО ОДНО совпадение —
 * это сам самоспаун из cs:0x161F:0x00FC, вызываемый ТОЛЬКО изнутри
 * THINK этого же типа 41. Значит без внешнего «зажигания» первого
 * экземпляра весь этот код в оригинале НЕДОСТИЖИМ — судя по всему,
 * вырезанная или незаконченная механика (осколки босса?), которую
 * ничто в шиппед-игре не запускает. ai_stub остаётся верным портом
 * НАБЛЮДАЕМОГО поведения (тип 41 никогда не встречается в игре). */
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
    [7]  = ai_type07,          /* свиток конца уровня */
    [8]  = ai_type08,          /* падающий «с неба» (см. выше);
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
    [29] = ai_type29,          /* прыгающий стрелок */
    [30] = ai_grow_chain,
    [31] = ai_type31,          /* лев */
    [32] = ai_type32,          /* босс LEVEL2 */
    [33] = ai_multipart,
    [34] = ai_multipart,            /* червь с HP-индикатором */
    [35] = ai_type35,          /* прыгающий голем */
    [36] = ai_ryu,             /* ★ Рю */
    [37] = ai_type37,          /* стреляющий ползун */
    [38] = ai_type38,          /* босс LEVEL3 */
    [39] = ai_multipart,
    [40] = ai_type40,          /* босс LEVEL4 */
    [41] = ai_stub,            /* реален (cs:0x161F:0x020A), но НЕДОСТИЖИМ —
                                  см. комментарий перед таблицей */
    [42] = ai_type42,          /* периодическая ловушка */
    [43] = ai_type43,          /* «крестовый» летун */
    [44] = ai_type44,          /* дрейфующий летун */
    [45] = ai_type45,          /* дрейфующая искра */
    [46] = ai_type46,          /* ныряющая турель */
    [47] = ai_type47,          /* турель */
    [48] = ai_type48,
    [49] = ai_type49,          /* «щит» босса */
    [50] = ai_multipart,
    [51] = ai_arena_static,
    [52] = ai_type52,          /* «пробуждающийся» блок */
    [53] = ai_type53,          /* падающий ползун */
    [54] = ai_type54,          /* стационарный плевун */
    [55] = ai_type55,          /* патрульный */
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
        case TYPE_STONE_THROWER:   return ai_stone_thrower;
        case TYPE_ROCK_PROJECTILE: return ai_rock_projectile;
        case TYPE_PLAYER_FIREBALL: return ai_player_fireball;
        case TYPE_GOLD_BALL:       return ai_gold_ball;
        case TYPE_TRAP_SEGMENT:    return ai_trap_segment;
        case TYPE_BOMB_BLAST:      return ai_bomb_blast;
        case TYPE_ENEMY_SHOT:      return ai_enemy_shot;
        case TYPE_ARENA_PILLAR:    return ai_arena_pillar;
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
    g_score = 0;

    g_rng_seed = 0x4B41u + level_num;   /* детерминизм демо: сид от уровня */
    g_ai_level_num = level_num;

    /* Sky-spawner активен только на уровнях, где есть type-8 запись
     * (её range=(0,0) — она никогда не спавнится сама, это маркер). */
    s_sky_enabled = false;
    for (u16 i = 0; i < level_num_enemies; ++i)
        if (level_enemies[i].type == 8) { s_sky_enabled = true; break; }
    s_ice_last_x_units = 0;
    s_ice_ctr = 0;

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
        /* НАЙДЕН И ИСПРАВЛЕН БАГ (2026-09): метатель на заставке «висел» над
         * полом. Причина — двойное применение подъёма. Раньше здесь sy
         * вручную выставлялся в «113 (ноги) − высота спрайта», подогнанное
         * под скриншот DOSBox — но НИЖЕ по коду для type=13 УЖЕ применяется
         * байт-верифицированный generic PLACE_LIFT (DS:0x1002=26, см.
         * type_place_lift), который вычитает те же 26 px ЕЩЁ РАЗ из уже
         * скорректированного sy — отсюда сдвиг вверх (полёт над полом).
         * Раз generic PLACE_LIFT для типа 13 уже байт-точен (та же формула,
         * что и для настоящих врагов типа 13 в LEVEL1), достаточно оставить
         * sy как есть из записи LEVEL0.BIN (110) — конверсию сделает общий
         * код ниже. Итоговый top = 110−26 = 84 (при sprite h≈27 → ноги на
         * y≈111 — практически совпадает со старым «113 на глаз», подтверждая,
         * что generic-механизм и есть настоящий алгоритм). */
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
        if (r->subtype == 1 || r->subtype == 7) ai_kill_ice_chain();
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

/* ─── Пары «объект-объект» (проход по боксам, file 0x2CB0/0x2D72) ─────
 *
 * Оригинал сравнивает боксы всех активных объектов, пропуская те, у
 * которых flag(+0x2E) == 0, и при пересечении пишет в ОБА объекта
 * +0xDD = 1 и +0xDF = индекс партнёра. На это опираются типы 49 (щит
 * гибнет вслед за тем, кого коснулся) и 52 (просыпается от касания).
 * Здесь тот же результат прямым перебором: живых объектов на кадр
 * немного (десятки), поэтому квадратичный проход недорог. */
static void ai_collect_pairs(void)
{
    for (int i = 0; i < g_num; ++i) g_obj[i].hit_flag = false;
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *a = &g_obj[i];
        if (!a->alive || a->invisible || a->is_dying) continue;
        int aw = obj_width(a), ah = (a->cur_h > 0) ? a->cur_h : obj_height(a);
        for (int j = i + 1; j < g_num; ++j) {
            ai_obj_t *b = &g_obj[j];
            if (!b->alive || b->invisible || b->is_dying) continue;
            int bw = obj_width(b), bh = (b->cur_h > 0) ? b->cur_h : obj_height(b);
            if (a->x + aw < b->x || b->x + bw < a->x) continue;
            if (a->y + ah < b->y || b->y + bh < a->y) continue;
            a->hit_flag = true; a->has_partner = true; a->hit_idx = j;
            b->hit_flag = true; b->has_partner = true; b->hit_idx = i;
        }
    }
}

/* ─── Pair-loop: касание врага бьёт Карнова ───────────────────────────
 *
 * Порт ветки THINKCYCLE, где a.obj_type = 0 (маркер Карнова), а
 * b.obj_type = 1 (враг): file 0xFB7A —
 *     if (player_state != 9) player.state_byte = 4;   // Карнов ранен
 *     if (god_mode) { b.hit_flag = 0; }               // чит ZEEK
 * Сами пары оригинал собирает отдельным проходом по боксам (file
 * 0x2CB0/0x2D18) в список DS:0x2722; здесь пары перебираются напрямую —
 * Карнов против каждого живого враждебного объекта.
 *
 * Урон засчитывается только объектам, которые в оригинале имеют
 * box0.hitflag = 1 (is_damageable_obj) или являются снарядами врага. */
void ai_check_player_hit(int kx, int ky, int kw, int kh)
{
    int px0 = kx - kw / 2, px1 = kx + kw / 2;
    int py0 = ky - kh,     py1 = ky;
    /* Тип 49 (щит, phase!=0 — только LEVEL9 x>0x44C) гибнет от ПРОСТОГО
     * касания игроком, не от урона — file 0x18D81, см. ai_type49. Тип 52
     * («пробуждающийся» блок) точно так же просыпается от касания
     * игроком, а не от любого партнёра — file 0x1910B, см. ai_type52.
     * Ни то, ни другое не «дамажащая» пара, поэтому регистрируется
     * отдельно от hostile-цикла ниже (тот прерывается на первом найденном
     * враге через return). */
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *o = &g_obj[i];
        if (!o->alive) continue;
        if (!((o->type == 49 && o->st_phase) || o->type == 52)) continue;
        int h = (o->cur_h > 0) ? o->cur_h : obj_height(o);
        int w = obj_width(o);
        if (px1 < o->x || px0 > o->x + w) continue;
        if (py1 < o->y || py0 > o->y + h) continue;
        o->hit_flag = true; o->has_partner = true; o->hit_idx = HIT_IDX_PLAYER;
    }
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *o = &g_obj[i];
        if (!o->alive || o->invisible) continue;
        if (o->type == TYPE_PLAYER_FIREBALL || o->type == TYPE_BOMB_BLAST) continue;
        if (o->is_scroll || o->type == 4 || o->type == 5 || o->type == 6) continue;
        /* Тип 36 (Рю, obj_type=9): generic pair-loop таблицы для
         * a.obj_type=0 (маркер игрока) x b.obj_type=9 (file 0xFBCB)
         * ставит ТОЛЬКО b.state_byte=5 (косметика — партнёр не снаряд,
         * проверка obj_type==2 в state5 проваливается, эффекта нет), но
         * НЕ вызывает обмен hit_points вообще, в отличие от b∈{1,3,7}
         * (ветка 0xFB8A). Простое касание Рю телом НЕ должно ранить ни
         * игрока, ни Рю — урон наносят только снаряды (ai_player_fireball
         * → ai_notify_enemy_hit).
         * Тип 20 (колонна голов, ТОЖЕ obj_type=9) — ТА ЖЕ таблица, тот же
         * вывод: перепроверено байт-в-байт при разборе её собственного
         * state5 (см. ai_head_column) — касание телом уходит на state5,
         * который либо ранит Карнова через отдельный, не общий, хелпер
         * (только удар именно по голове, box0), либо не делает вообще
         * ничего (удар по телу). Без этого исключения общий symmetric-obмен
         * ниже отнимал у Карнова урон = ТЕКУЩИЙ hp колонны (~250, см.
         * ai_head_column: hit_points принудительно 250 каждый тик) при
         * ЛЮБОМ касании столба телом — второй, независимый от скорости
         * снаряда, источник «мгновенной смерти» на LEVEL2.
         * Тип 33 (дерево у ямы LEVEL3, ТОЖЕ obj_type=9) — ТА ЖЕ таблица
         * (file 0xFB68 индекс 9 → file 0xFBCB), тот же вывод: касание
         * телом уходит на голый state_byte=5 без обмена hit_points, а
         * значит НИКОГДА не может запустить «схлопывание» (см. подробный
         * разбор в ai_multipart) и НИКОГДА не должно отнимать у Карнова
         * ~95 HP колонны, как отнимал symmetric-обмен ниже до этой
         * правки (2026-09-24). */
        if (o->type == 36 || o->type == 20 || o->type == 33) continue;
        bool hostile = (o->type == TYPE_ENEMY_SHOT) || is_damageable_obj(o) || o->is_boss;
        /* (враждебность = наличие активного бокса, как +0x2E в оригинале) */
        if (!hostile) continue;
        int h = (o->cur_h > 0) ? o->cur_h : obj_height(o);
        int w = obj_width(o);
        if (px1 < o->x || px0 > o->x + w) continue;
        if (py1 < o->y || py0 > o->y + h) continue;
        /* Симметричный обмен уроном (file 0xF81E): игрок теряет HP врага,
         * враг — HP игрока, оба с клампом в ноль. */
        int ehp = (o->hp > 0) ? o->hp : 1;
        int php = physics_hp();
        physics_hit(ehp);
        o->hp -= php;
        if (o->hp <= 0 && o->type != TYPE_ENEMY_SHOT) ai_notify_enemy_hit(i, g_ai_frame);
        return;
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
        /* Свиток конца уровня (тип 7) до появления ещё не помечен is_scroll,
         * но subtype = 9 (нечётный) — деспавн снял бы саму возможность
         * закончить уровень. ПРОВЕРЕНО: в оригинале off-screen-деспавн
         * (file 0xFD09-0xFD99, часть той же функции, что и pair-loop)
         * исключает из выбраковки ТОЛЬКО слот босса — сравнением со
         * [DS:0xD959] (file 0xFD76: `cmp ax,[0xd959]; jz 0xfd99`), где ax —
         * слот, на который сейчас указывает текущая spawn-запись.
         * [DS:0xD94F]/[DS:0xD951] — это ДРУГОЙ механизм: сырой указатель,
         * который свиток пишет сам на себя при своём INIT (file 0x1282C),
         * чтобы pair-loop при смерти босса (file 0xFC85) нашёл его и взвёл
         * +0xED. К деспавну он отношения не имеет — оригинал НЕ защищает
         * ожидающий/невзведённый свиток от выбраковки по дальности.
         * Строка ниже — намеренная защита порта: убийство босса бомбой/
         * Clapper'ом издалека (ai_clear_enemies) может оставить игрока
         * дальше 250 px от ещё не заспаунившегося свитка, и общий проход
         * снёс бы его прежде, чем он появится. */
        if (o->type == 7) continue;
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
    /* KARNOV_DBG_SPAWN_RYU=1 — тестовый спаун типа 36 (Рю) рядом с
     * игроком на кадре 0. Оригинал сам его никогда не создаёт (запись
     * LEVEL9.BIN[2] целиком нулевая, а выделенного спаунера, в отличие
     * от sky-цепи, в KARNOV.EXE не найдено — см. комментарий перед
     * обработчиками типов 4..57). Хук нужен ТОЛЬКО для ручной проверки
     * ai_ryu/ai_notify_enemy_hit до тех пор, пока настоящий спаунер не
     * найден; не влияет на обычную игру. */
    if (frame == 0 && getenv("KARNOV_DBG_SPAWN_RYU")) {
        int slot = alloc_obj_slot();
        if (slot >= 0) {
            g_obj[slot] = (ai_obj_t){
                .alive = true, .type = 36, .subtype = 9,
                .x = physics_get_left_x() + 60, .y = 300,
                .hp = 50, .facing = -1, .level_idx = -1,
            };
            fprintf(stderr, "DBG: тестовый спаун Рю на (%d,%d)\n",
                    g_obj[slot].x, g_obj[slot].y);
        }
    }
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
    /* KARNOV_DBG_HITCOL=<box_idx> — временный тестовый хук: раз в 40 кадров
     * бьёт указанный box первого живого типа-20 объекта (для смоук-теста
     * просадки колонны без реального выстрела игрока). */
    { const char *hc = getenv("KARNOV_DBG_HITCOL");
      if (hc && frame % 40 == 0 && frame > 0) {
        int box = atoi(hc);
        for (int i = 0; i < g_num; ++i) {
            if (g_obj[i].alive && g_obj[i].type == 20) {
                ai_head_column_hit_box(&g_obj[i], box);
                break;
            }
        }
      } }
    /* KARNOV_DBG_TYPE=<n> — трассировка объектов одного типа рядом с
     * координатами игрока в конвенции движка (левый край / верх спрайта). */
    { const char *dt = getenv("KARNOV_DBG_TYPE");
      if (dt) { int want = atoi(dt);
        for (int i = 0; i < g_num; ++i) {
            if (!g_obj[i].alive || g_obj[i].type != (u16)want) continue;
            fprintf(stderr, "f=%u t%d x=%d y=%d hp=%d puff=%d ph=%d misc=%d aux=%d st=%d "
                    "| karnov left=%d top=%d feet=%d",
                    frame, want, g_obj[i].x, g_obj[i].y, g_obj[i].hp,
                    g_obj[i].puff_frames,
                    g_obj[i].st_phase, g_obj[i].st_misc, g_obj[i].st_aux,
                    g_obj[i].anim_state, physics_get_left_x(), physics_get_top_y(),
                    physics_get_y());
            for (int p = 0; p < g_obj[i].num_parts; ++p)
                fprintf(stderr, " part%d(spr=%d,dy=%d)", p,
                        g_obj[i].parts[p].sprite, g_obj[i].parts[p].dy);
            fprintf(stderr, "\n");
        } } }
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) continue;
        ai_obj_t *o = &g_obj[i];
        o->cur_h = obj_height(o);
        if (o->puff_frames > 0 && --o->puff_frames == 0 && o->is_dying) {
            o->alive = false;                 /* вспышка догорела */
            continue;
        }
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

    ai_collect_pairs();          /* +0xDD/+0xDF для типов 49 и 52 */

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
            arm_real_scroll();
            fprintf(stderr, "DBG: boss убит на (%d,%d), кадр %u\n", g_boss_x, g_boss_y, frame);
            break;
        }
      } }

    /* Страховка проходимости — для ЛЮБОГО свитка, и настоящего (тип 7), и
     * синтетического: если он лёг там, куда Карнов не может дойти (на
     * LEVEL4 «гусеница» висит на 126 px выше него, и свиток ложился на
     * верхний ярус — уровень было не закончить), через 400 кадров после
     * ПОЯВЛЕНИЯ свитка переносим его к самому Карнову. */
    bool scroll_exists = false;
    if (!g_scroll_picked) {
        for (int i = 0; i < g_num; ++i) {
            ai_obj_t *sc = &g_obj[i];
            if (!sc->alive || !sc->is_scroll) continue;
            scroll_exists = true;
            if ((u16)(frame - sc->spawn_frame) > 400) {
                int h = sc->cur_h > 0 ? sc->cur_h : obj_height(sc);
                if (sc->y + h < physics_get_y() - 20) {
                    sc->x = physics_get_left_x();
                    sc->y = physics_get_y() - h;
                    sc->custom = 1;             /* уже «приземлился» */
                }
            }
            break;
        }
    }

    /* Синтетический свиток нужен лишь там, где в данных нет записи
     * типа 7 (настоящего свитка) — иначе работает он сам. */
    bool real_scroll = false;
    for (int i = 0; i < g_num; ++i)
        if (g_obj[i].alive && g_obj[i].type == 7) { real_scroll = true; break; }
    if (g_boss_dead && !g_scroll_picked && !real_scroll) {
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
 *   #147..148 — Летящая зелёная птица (2 фазы) → тип 10 (LEVEL1 «орлы»,
 *                ai_type10) и type 16 (горизонтальный летун)
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
        case   7:                                  /* свиток конца уровня #144 */
            return 0x90;
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
    return type == 16;
}

void ai_render(bool mode_cga, int scroll_x, int scroll_y)
{
  g_camera_top = scroll_y;
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < g_num; ++i) {
        if (!g_obj[i].alive) continue;
        /* Скрытые предметы (misc_state ≥ 11 в оригинале) видны, пока идёт
         * таймер Glasses — file 0x12507 читает [0xD8B8]. */
        if (g_obj[i].invisible &&
            !((g_obj[i].type == 4 || g_obj[i].type == 6) && inv_glasses_active()))
            continue;
        /* Тип 7 — свиток: до появления он невидим (флаг invisible). */
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
        if (g_obj[i].puff_frames > 0) {
            static const u16 PUFF[4] = { 4, 5, 6, 6 };
            sid = PUFF[(8 - g_obj[i].puff_frames) >> 1];
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
/* ─── Очки за убийство (add_score, cs:0x41D:0x04D7) ────────────────────
 *
 * Оригинал держит счёт 8-разрядной BCD-строкой в DS:0xD930 и после
 * начисления взводит флаг перерисовки [0xD938]. Константы в
 * обработчиках — тоже BCD, то есть 0x2600 читается как 2600 очков.
 * Таблица извлечена автоматическим сканом всех вызовов add_score с
 * привязкой к владельцу-обработчику; 0 = тип очков не начисляет либо
 * его ветка смерти не найдена. */
static const u16 SCORE_BCD[58] = {
    [8] = 0x110, [10] = 0x110, [11] = 0x1400, [12] = 0x1000, [13] = 0x400,
    [14] = 0x2600, [15] = 0x1800, [16] = 0x110, [17] = 0x400, [18] = 0x1000,
    [22] = 0x500, [23] = 0x500, [24] = 0x7000, [25] = 0x110, [26] = 0x400,
    [27] = 0x400, [28] = 0x400, [29] = 0x400, [31] = 0x7000, [32] = 0x7000,
    [34] = 0x110, [35] = 0x800, [37] = 0x12000 & 0xFFFF, [40] = 0x7000,
    [43] = 0x1800, [44] = 0x1800, [45] = 0x100, [46] = 0x1000, [47] = 0x1000,
    [48] = 0x1000, [53] = 0x1000, [54] = 0x1800,
};

/* BCD-константа → десятичное число очков (0x2600 → 2600). */
static u32 bcd_to_dec(u32 v)
{
    u32 r = 0, mul = 1;
    while (v) { r += (v & 0xF) * mul; mul *= 10; v >>= 4; }
    return r;
}

u32 ai_score(void) { return g_score; }

static void award_score(u16 type)
{
    u32 bcd = (type == 37) ? 0x12000u
            : (type < 58)  ? SCORE_BCD[type] : 0u;
    if (type == 38) bcd = 0x20000u;          /* босс LEVEL3 */
    g_score += bcd_to_dec(bcd);
}

/* Взвести настоящий свиток (тип 7): в оригинале его phase(+0xED)
 * поднимает гибель goal-объекта, после чего свиток ждёт подхода игрока. */
static void arm_real_scroll(void)
{
    for (int k = 0; k < g_num; ++k)
        if (g_obj[k].alive && g_obj[k].type == 7) g_obj[k].st_phase = 1;
}

/* Общий путь урона по типу 33 (см. подробный разбор в ai_multipart и в
 * ai_notify_enemy_hit): и файербол, и вспышка Bomb идут через ОДНУ и ту
 * же generic-ветку (file 0xFC18/0xFC21/0xFC50, a.obj_type==2 ×
 * b.obj_type==9) — разница только в dmg (10 у выстрела, 100 у Bomb).
 * Используется из ai_notify_enemy_hit (выстрел) и ai_clear_enemies
 * (Bomb) вместо прямого снятия alive, чтобы колонна проигрывала
 * настоящий коллапс, а не исчезала мгновенно. */
#define BOMB_BLAST_DAMAGE 100   /* file 0x12291: hit_points вспышки Bomb */
static void ai_tree33_take_hit(ai_obj_t *o, int dmg)
{
    if (o->st_phase >= 2) return;                  /* уже коллапсирует — неуязвим */
    o->hp -= dmg;
    if (o->hp <= 0) {
        o->st_phase = 2;
        audio_play_sfx(1);   /* file 0xFC50: play_sound(1) — общий для всех b.hp<=0 */
    } else {
        o->st_phase = 1;
    }
}

void ai_notify_enemy_hit(int slot, u16 frame)
{
    if (slot < 0 || slot >= g_num) return;
    ai_obj_t *o = &g_obj[slot];
    if (!o->alive) return;
    /* Урон = PLAYER_FIREBALL_DAMAGE (пул HP врага уменьшается на урон
     * снаряда, как `b->hit_points -= a->hit_points` в оригинале). */
    if (o->is_decor) return;              /* декорацию уже не добить */
    /* Тип 33 (file 0xFC18/0xFC21/0xFC50 — generic pair-loop для
     * a.obj_type==2 × b.obj_type==9, см. подробный разбор в ai_multipart):
     * снаряд/бомба ДЕЙСТВИТЕЛЬНО обменивается hit_points с колонной (в
     * отличие от простого касания — см. ai_check_player_hit), и коллапс
     * (state4) стартует, только когда получившийся hit_points <= 0.
     * Файербол несёт PLAYER_FIREBALL_DAMAGE=10 (file 0x117C2) против
     * hp=95 — нужно около 10 попаданий; Bomb-вспышка (ai_bomb_explode)
     * бьёт этим же путём уроном 100 (file 0x12291) и валит с одного
     * раза. Пока результат > 0 — st_phase=1 («разбужена»): в
     * ai_multipart THINK для этой фазы больше не восстанавливает hp на
     * снимок (в оригинале — state_byte застрял на 5, THINK не
     * вызывается), т.е. накопленный урон не сгорает между попаданиями. */
    if (o->type == 33) {
        ai_tree33_take_hit(o, PLAYER_FIREBALL_DAMAGE);
        return;
    }
    /* Тип 36 (Рю, финальный босс) — см. подробный разбор в ai_ryu.
     * Исчерпывающий скан THINKCYCLE (file 0xFC18..0xFC96) и всего
     * KARNOV.EXE (misc=2 writes, phase(+0xED) reads, state_byte=6
     * writes) подтвердил: у Рю НЕТ generic-смерти. Попадание снаряда
     * игрока (a.obj_type==2, гарантировано вызовом отсюда — эта функция
     * вызывается только из ai_player_fireball) идёт по generic-обмену
     * hit_points (file 0xFC21..0xFC47); дальше расходится:
     *   hp ещё > 0  → state5 (file 0x151A:0x07A0): счётчик phase(+0xED)
     *                 ++ (нигде не читается — не влияет на исход, но
     *                 воспроизведён байт-точно), хвост → спрайт 0xFA
     *                 («ранен»). НЕТ award_score/despawn.
     *   hp <= 0     → state4 ПЕРЕЗАПИСЫВАЕТ state5 в этом же кадре
     *                 (file 0xFC49..0xFC5A): играет generic play_sound(1)
     *                 («враг убит»), затем state4 самого типа 36 (file
     *                 0x151A:0x07DE) ставит misc=2 — «сесть» (см.
     *                 ai_ryu), БЕЗ award_score/die_puff/is_boss/despawn.
     * Рю остаётся alive навсегда; повторные попадания после этого молча
     * повторяют ту же ветку (hp уходит в минус, как и в оригинале —
     * там тоже ничего не мешает повторному входу). */
    if (o->type == 36) {
        o->hp -= PLAYER_FIREBALL_DAMAGE;
        if (o->hp > 0) {
            o->st_phase++;                       /* phase(+0xED)++ (мёртвая переменная, честно) */
            if (o->num_parts > 1) o->parts[1].sprite = 250;   /* 0xFA — «раненый хвост» */
            return;
        }
        audio_play_sfx(1);                       /* generic kill SFX, file 0xFC5A — Рю НЕ умирает */
        o->st_misc = 2;                          /* «сесть» — та же реакция, что и на обычный удар */
        return;
    }
    /* Тип 34 (file 0x16DE7): КАЖДЫЙ удар снимает один сегмент сверху вниз
     * (st_phase — индекс верхнего живого бокса, 5..0, см. INIT в
     * ai_multipart) и начисляет 110 очков (SCORE_BCD[34]=0x110); hp,
     * как и у типа 33, залочен на снимке — обычная hp<=0-смерть тут не
     * применяется. Последний (0-й) сегмент — добивающий: очки
     * начисляются ЕЩЁ РАЗ (в оригинале — общий add_score в конце THINK
     * ПЛЮС отдельный в ветке смерти) и объект уходит в generic die_puff. */
    if (o->type == 34) {
        int idx = o->st_phase;
        if (idx >= 0 && idx < o->num_parts) o->parts[idx].sprite = 0;
        award_score(o->type);
        audio_play_sfx(1);
        if (idx > 0) {
            o->st_phase = idx - 1;
            o->hp = o->st_aux;
            o->puff_frames = 8;
        } else {
            award_score(o->type);
            o->is_dying = true;
            o->puff_frames = 8;
        }
        return;
    }
    o->hp -= PLAYER_FIREBALL_DAMAGE;
    if (o->hp > 0) {
        /* Раненый, но живой враг в оригинале не продолжает как ни в чём
         * не бывало: die_puff (cs:0xD4B:0x4A5) подменяет ему обработчик
         * на «пуф» cs:0xD4B:0x3C0, тот проигрывает 4-кадровую вспышку
         * (state_list DS:0x1440 = #4,#5,#6,#6 либо DS:0x1462 = #6,#7,#8,#8
         * по флагу +0xED) и ВОЗВРАЩАЕТ исходный обработчик. В порту это
         * короткая вспышка: 8 кадров рисуем кадры пуфа поверх врага. */
        o->puff_frames = 8;
        return;
    }
    /* Тип 22 в состоянии 4 (cs:0x1348:0x0D24, file 0x159A4) не просто
     * гибнет: помимо очков он вызывает spawn_projectile(x0=box0.x0+2,
     * y0=box0.y0+0x19, x1=player.x_world, y1=player.y_world+0xa, kind=2,
     * speed=QV(8,0), flag=1) (file 0x159A4-0x159C8) и рождает ещё один
     * объект.
     * speed=8.0 байт-верифицировано (`mov dx,8 / xor ax,ax` перед push,
     * file 0x159AD/0x159B2). НО flag=1 (первый push, самый глубокий
     * аргумент) — это не просто «признак», а управляет веткой в самом
     * spawn_projectile (cs:0x11D6:0x139, file 0x13699+0x1A4..0x1B1):
     * при flag != 0 переданный y1 ПОДМЕНЯЕТСЯ на y0 (self.y_world) ПЕРЕД
     * вызовом Манхэттен-нормализатора (cs:0x11D6:0x006) — т.е. цель по Y
     * искусственно совпадает с источником и вертикальная дельта равна
     * НУЛЮ. Снаряд летит СТРОГО ГОРИЗОНТАЛЬНО (в сторону игрока по X) на
     * полной скорости 8.0, а не «наведённый» по Y. Сверено с типом 8
     * (file 0x12B00, тройной выстрел, flag=0 передан явно — там y1
     * остаётся настоящей целью, что и подтверждает флаг именно как этот
     * переключатель, а не случайное совпадение). Прежний порт ошибочно
     * нормализовал по (tx,ty) с реальным ty — баг того же рода, что и
     * ранее найденный у типа 20 (наведение вместо фиксированной формулы). */
    if (o->type == 22) {
        int slot = alloc_obj_slot();
        if (slot >= 0) {
            ai_obj_t *b = &g_obj[slot];
            *b = (ai_obj_t){ .alive = true, .type = TYPE_ENEMY_SHOT, .subtype = 3,
                             .x = o->x + 4, .y = o->y + 0x19, .hp = 10,
                             .level_idx = -1,
                             .facing = (player_units() > obj_units(o)) ? 1 : -1 };
            obj_phys_begin(b);
            int tx = player_units() - obj_units(o);
            b->vxq = (tx >= 0) ? QV(8, 0) : -QV(8, 0);
            b->vyq = 0;
        }
        /* Второй объект (обработчик cs:0x1348:0x0BB2): obj_type = 5, то
         * есть ПРЕДМЕТ. Он рождается в (x, y + 5) с vy = +1.5 и спрайтом
         * #1, падает до уровня, записанного в +0xEB родителя минус 10
         * (тип 22 сохранил там строку земли под собой), и на приземлении
         * превращается в спрайт 0x30 = #48 — «сокровище» из панели
         * OPTIONS. То есть убитый страж роняет добычу. */
        int islot = alloc_obj_slot();
        if (islot >= 0) {
            g_obj[islot] = (ai_obj_t){
                .alive = true, .type = 5, .subtype = 5, .hp = 1,
                .level_idx = -1, .facing = 1,
                .x = o->x, .y = o->y + 5,
                .item_sprite = 48,          /* #48 — сокровище (K-Mark) */
            };
        }
    }
    /* Состояние 4 у части типов не убивает объект: тип 54 переходит в
     * obj_type = 9, box0.hitflag = 0 и аним-состояние 2, то есть
     * остаётся на карте неуязвимой декорацией (file 0x1953B, 0x030B). */
    if (o->type == 54) {
        award_score(o->type);
        o->is_decor = true;
        o->hp = 0;
        o->pending_state = 2;
        o->anim_state = 2;
        o->puff_frames = 8;
        audio_play_sfx(1);
        return;
    }
    /* Тип 55 (патрульный) на «смертельный» удар (file 0x19697, cs:0x17EC:
     * 0x187) НЕ умирает вовсе: hit_points принудительно = 0x64 (100, вне
     * зависимости от исходного hp записи), state_byte возвращается в 2
     * (THINK) и misc(+0xE9) = 2 — объект сразу уходит в «отдых» и как ни
     * в чём не бывало продолжает патрулировать с полным hp. Ни очков, ни
     * die_puff, ни удаления здесь нет — это самовосстанавливающийся враг,
     * которого штатным огнём убить нельзя. */
    if (o->type == 55) {
        o->hp = 0x64;
        o->st_misc = 2;
        return;
    }
    /* Тип 51 (декоративный элемент арены) на гибель поднимает ОТДЕЛЬНЫЙ
     * объект-«столб» cs:0x175E:0x3A9 (queue_state, см. ai_arena_pillar) —
     * 8 сегментов sprite 0x13D(317), абсолютные координаты (1536,310),
     * неуязвим. См. подробный комментарий у ai_arena_static/ai_arena_pillar. */
    if (o->type == 51) {
        int slot = alloc_obj_slot();
        if (slot >= 0) {
            g_obj[slot] = (ai_obj_t){ .alive = true, .type = TYPE_ARENA_PILLAR,
                                       .subtype = 1, .level_idx = -1, .hp = 1,
                                       .facing = 1 };
        }
    }
    /* Смерть тоже проходит через вспышку: оригинал ставит state 4,
     * обработчик начисляет очки и зовёт die_puff, и только состояние 6
     * снимает объект (ai_result |= 1). Полный автомат 4/5/6 в порту не
     * реализован — здесь эквивалент по видимому эффекту. */
    o->is_dying = true;
    o->puff_frames = 8;
    award_score(o->type);
    audio_play_sfx(1);   /* враг убит — play_sound(1), KARNOV.EXE @ 0xFC5A */
    /* Запись врага остаётся flags=1 (one-shot): убитый не возвращается,
     * пока rearm-цикл после смерти игрока не перевзведёт его. */
    if (o->is_boss) {
        g_boss_dead       = true;
        g_boss_dead_frame = frame;
        g_boss_x          = o->x;
        g_boss_y          = o->y;
        arm_real_scroll();
    }
}

/* Уничтожить всех живых damageable-врагов на экране (Bomb / Clapper).
 * exclude_boss=true (Clapper) пропускает boss'а; false (Bomb) убивает всех.
 * Логика зеркалит ai_notify_enemy_hit: снимаем alive (записи остаются
 * flags=1 — one-shot), отмечаем boss_dead.
 *
 * Тип 33 — ИСКЛЮЧЕНИЕ (см. ai_tree33_take_hit): в оригинале вспышка Bomb
 * не «убивает» колонну напрямую, а бьёт её ТЕМ ЖЕ generic pair-loop'ом,
 * что и файербол (a.obj_type==2 × b.obj_type==9, file 0xFC18), просто с
 * hit_points=100 вместо 10 — этого достаточно, чтобы hp=95 сразу ушёл в
 * минус и стартовал настоящий коллапс (state4), а не мгновенное
 * исчезновение объекта. */
void ai_clear_enemies(bool exclude_boss)
{
    for (int i = 0; i < g_num; ++i) {
        ai_obj_t *o = &g_obj[i];
        if (!o->alive) continue;
        if (o->type == 33) { ai_tree33_take_hit(o, BOMB_BLAST_DAMAGE); continue; }
        if (!is_damageable_obj(o) && !o->is_boss) continue;
        if (exclude_boss && o->is_boss) continue;
        o->alive = false;
        if (o->is_boss) {
            g_boss_dead       = true;
            g_boss_dead_frame = 0;
            g_boss_x          = o->x;
            g_boss_y          = o->y;
            arm_real_scroll();
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

bool ai_spawn_player_fireball(int x, int y, int facing, u16 frame)
{
    /* Порт fire() (cs:0xFFA:0x1888, file 0x13028) и spawn_player_shot
     * (cs:0xFFA:0x17AF, file 0x12F4F):
     *   • при 15 живых снарядах выстрел не происходит вовсе (file 0x1303B) —
     *     и, что важно, fire() в этом случае НЕ трогает cooldown [0xD8B2]
     *     (перезарядка не взводится — прыжок на 0x307D минует и
     *     play_sound, и запись cooldown) — вызывающая сторона (physics.c)
     *     обязана проверять возврат этой функции, а не взводить cooldown
     *     безусловно;
     *   • звук — play_sound(0) (file 0x13045), НЕ 7;
     *   • число снарядов = firepower (1..3, [DS:0xD949]), и ветки таблицы
     *     file 0x12FB5 «проваливаются» СВЕРХУ ВНИЗ, спавня РАЗНЫЕ объекты
     *     с разными обработчиками — то самое «раздвоение/растроение»:
     *       fp=3 → верхний (y−2, cs:0x00FF) + нижний (y+2, cs:0x0183)
     *              + прямой (y, cs:0x00C2)
     *       fp=2 → нижний + прямой      fp=1 → только прямой
     *   • точка вылета Y = верх спрайта Карнова + 7 (+16 если сидит) —
     *     байт-подтверждено (file 0x12F86..0x12F9F);
     *   • точка вылета X — ПЕРЕРАЗОБРАНО заново (2026-09, file
     *     0x12F55..0x12FA3): вовсе не симметричный «+4 единицы» для обоих
     *     направлений, как считалось раньше. Код читает [DS:0xD8B0]
     *     (facing, тот же регистр что выбирает муззл-спрайт 105/113 в
     *     конце этой функции), индексирует ИМЕННО ТУ ЖЕ velocity-таблицу
     *     DS:0x1348, что и WALK_SPEED (см. physics.c): вправо → запись
     *     (+2.0 Q16.16), влево → (−2.0), удваивает (тот же ×2 lshift, что
     *     у ходьбы) и прибавляет к x_world игрока ФЛАТ-константу +4.0 (px,
     *     этот пайплайн уже в реальных пикселях — см. WALK_SPEED-комментарий
     *     в physics.c про то же ×2). Итог: вправо x = x_world + 8 px,
     *     влево x = x_world + 0 px — АСИММЕТРИЧНО, что объясняется разными
     *     позициями руки Карнова в левом/правом артах. Перенесено в
     *     physics_tick() (см. `s_facing > 0 ? 8 : 0`);
     *   • состояние анимации выбирается по firepower при СПАВНЕ
     *     (cs:0xFFA:0x0009: [0xD88C] = firepower > 1). */
    if (player_shots_alive() >= 15) return false;

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
    return true;
}
