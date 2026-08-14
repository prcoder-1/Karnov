/* inventory.c — реализация Karnov OPTIONS (полная 10-предметная модель).
 *
 * См. inventory.h для обзора, порядка предметов и управления.
 */
#include "inventory.h"
#include "ai.h"     /* ai_clear_enemies для Bomb/Clapper */

/* Таймеры — из обработчиков подбора/использования KARNOV.EXE:
 *   Wings   [0xD8B6] = 200 кадров (file 0x629B) — было 825 «на глаз»;
 *   Glasses [0xD8B8] = 150 кадров (file 0x628D);
 *   Boots   [0xD8B4] = 250 кадров (file 0x61D5). */
#define WINGS_DURATION_FRAMES  200
#define GLASSES_DURATION_FRAMES 150

/* KARNOV.EXE sets boots timer [0xd8b4]=0xFA=250 frames (file 0x61D5);
 * movement handler doubles vy while it's >0. */
#define BOOTS_DURATION_FRAMES  250

/* Shield поглощает 5 попаданий врага. */
#define SHIELD_HITS         5

static u8  s_count[ITEM_NUM];  /* счётчик каждого предмета */
static int s_current;          /* выбранный индекс, -1 если пусто */
static u16 s_boots_timer;      /* frames remaining; 0 = boots неактивны */
static u16 s_fly_timer;        /* frames remaining; 0 = не летит */
static u8  s_shield_hits;      /* оставшиеся поглощения щита */
static u16 s_glasses_timer;    /* frames remaining; >0 — видны скрытые предметы */
static int s_firepower;        /* 1..3 — снарядов за выстрел ([0xD949]) */
static int s_kmarks;           /* накоплено K-Mark'ов */

void inv_reset(void)
{
    for (int i = 0; i < ITEM_NUM; ++i) s_count[i] = 0;
    s_current     = -1;
    s_boots_timer = 0;
    s_fly_timer   = 0;
    s_shield_hits = 0;
    s_glasses_timer = 0;
    /* Сила огня сбрасывается вместе с (ре)спавном Карнова — file 0x104DE
     * (`mov word [0xD949],1`), а inv_reset зовётся из physics_reset. */
    s_firepower   = 1;
    s_kmarks      = 0;
}

void inv_give(int id)
{
    if (id < 0 || id >= ITEM_NUM) return;

    /* Два предмета в оригинале НЕ кладутся в набор «используемых» (у них
     * нет обработчика в таблице DS:0x0C30) — эффект наступает сразу при
     * подборе, в option_pickup (file 0x638E). */
    if (id == ITEM_SUPERFIRE) {                 /* file 0x6452 */
        if (s_firepower < 3) ++s_firepower;
        return;
    }
    if (id == ITEM_KMARK) {                     /* file 0x63FE */
        ++s_kmarks;
        return;
    }

    if (s_count[id] < 0xFF) ++s_count[id];
    if (s_current < 0) s_current = id;
}

void inv_give_by_sprite(u16 sprite)
{
    /* item_id = sprite − 0x27 (file 0x1250D), спрайты #39..#49. */
    if (sprite >= 39 && sprite < 39 + ITEM_NUM) inv_give((int)sprite - 39);
}

int inv_count(int id)
{
    if (id < 0 || id >= ITEM_NUM) return 0;
    return s_count[id];
}

int inv_current(void) { return s_current; }

int inv_distinct(void)
{
    int n = 0;
    for (int i = 0; i < ITEM_NUM; ++i) if (s_count[i] > 0) ++n;
    return n;
}

void inv_cycle(int dir)
{
    if (dir == 0) dir = 1;
    int step = (dir > 0) ? 1 : -1;
    int start = (s_current < 0) ? 0 : s_current;
    for (int k = 1; k <= ITEM_NUM; ++k) {
        int id = ((start + step * k) % ITEM_NUM + ITEM_NUM) % ITEM_NUM;
        if (s_count[id] > 0) { s_current = id; return; }
    }
    s_current = -1;   /* инвентарь пуст */
}

int inv_use(void)
{
    if (s_current < 0 || s_count[s_current] == 0) return -1;
    int id = s_current;

    switch (id) {
        case ITEM_LADDER:
            /* Постановка лестницы — физика в physics.c по возвращённому id. */
            break;
        case ITEM_BOOTS:
            s_boots_timer = BOOTS_DURATION_FRAMES;
            break;
        case ITEM_BOMB:
            /* Спавн анимированной вспышки + уничтожение всех врагов
             * (включая boss'а). ai_bomb_explode сам зовёт ai_clear_enemies. */
            ai_bomb_explode();
            break;
        case ITEM_SHIELD:
            /* id7: в EXE обработчик — пустой стаб (file 0x62C1), так что
             * точной семантики нет. В порту за ним оставлены обе игровые
             * функции: щит на 5 попаданий и «хлопок» — убить всех, кроме
             * боссa. Уточнить, когда разберём pair-loop урона игроку. */
            s_shield_hits = SHIELD_HITS;
            ai_clear_enemies(true);
            break;
        case ITEM_WINGS:
            /* Полёт запускается здесь (таймер), но детач-лестницы/флаги
             * s_flying выставляет physics.c по возвращённому id. */
            s_fly_timer = WINGS_DURATION_FRAMES;
            break;
        case ITEM_GLASSES:
            /* Таймер, на время которого видны скрытые предметы (file 0x628D;
             * читается при спавне предмета — file 0x12507). */
            s_glasses_timer = GLASSES_DURATION_FRAMES;
            break;
        case ITEM_FIRE:
        case ITEM_BOOMERANG:
        case ITEM_MASK:
            /* TODO: оружейные опции (запас 30 шаров / бумеранг, [0xD947])
             * и акваланг — не портированы. */
            break;
        default:
            break;
    }

    /* Расход предмета. Если счётчик обнулился — сдвигаем выбор дальше. */
    if (s_count[id] > 0) --s_count[id];
    if (s_count[id] == 0) inv_cycle(+1);
    return id;
}

bool inv_boots_active(void) { return s_boots_timer > 0; }

void inv_tick(void)
{
    if (s_boots_timer   > 0) --s_boots_timer;
    if (s_fly_timer     > 0) --s_fly_timer;
    if (s_glasses_timer > 0) --s_glasses_timer;
}

int  inv_firepower(void)      { return s_firepower; }
bool inv_glasses_active(void) { return s_glasses_timer > 0; }

/* ─── Compat-обёртки ──────────────────────────────────────────────────── */
void inv_give_ladder(void)      { inv_give(ITEM_LADDER); }
void inv_give_wings(void)       { inv_give(ITEM_WINGS);  }
void inv_take_back_ladder(void) { inv_give(ITEM_LADDER); }
bool inv_has_ladder(void)       { return s_count[ITEM_LADDER] > 0; }
bool inv_has_wings(void)        { return s_count[ITEM_WINGS]  > 0; }
bool inv_flying(void)           { return s_fly_timer > 0; }
u16  inv_flying_remaining(void) { return s_fly_timer; }
void inv_cancel_flying(void)    { s_fly_timer = 0; }
