/* inventory.c — реализация Karnov OPTIONS (полная 10-предметная модель).
 *
 * См. inventory.h для обзора, порядка предметов и управления.
 */
#include "inventory.h"
#include "ai.h"     /* ai_clear_enemies для Bomb/Clapper */

/* WINGS_DURATION_FRAMES = 825 ≈ 30 s @ 27.5 FPS.
 * Эмпирическое значение (~30 s соответствует «short power-up»-ощущению из
 * обзоров оригинала). Точное число тиков ещё НЕ байт-верифицировано из
 * KARNOV.EXE. TODO: уточнить из reverse'а handler'а ai_type03. */
#define WINGS_DURATION_FRAMES  825

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

void inv_reset(void)
{
    for (int i = 0; i < ITEM_NUM; ++i) s_count[i] = 0;
    s_current     = -1;
    s_boots_timer = 0;
    s_fly_timer   = 0;
    s_shield_hits = 0;
}

void inv_give(int id)
{
    if (id < 0 || id >= ITEM_NUM) return;
    if (s_count[id] < 0xFF) ++s_count[id];
    if (s_current < 0) s_current = id;
}

void inv_give_by_sprite(u16 sprite)
{
    if (sprite >= 39 && sprite <= 48) inv_give((int)sprite - 39);
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
        case ITEM_CLAPPER:
            /* Уничтожить всех врагов на экране, КРОМЕ boss'а. */
            ai_clear_enemies(true);
            break;
        case ITEM_WINGS:
            /* Полёт запускается здесь (таймер), но детач-лестницы/флаги
             * s_flying выставляет physics.c по возвращённому id. */
            s_fly_timer = WINGS_DURATION_FRAMES;
            break;
        case ITEM_SHIELD:
            s_shield_hits = SHIELD_HITS;
            break;
        case ITEM_FIRE:
        case ITEM_BOOMERANG:
        case ITEM_GLASSES:
        case ITEM_MASK:
            /* TODO: пассивные / пока не смоделированные эффекты — no-op. */
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
    if (s_boots_timer > 0) --s_boots_timer;
    if (s_fly_timer   > 0) --s_fly_timer;
}

/* ─── Compat-обёртки ──────────────────────────────────────────────────── */
void inv_give_ladder(void)      { inv_give(ITEM_LADDER); }
void inv_give_wings(void)       { inv_give(ITEM_WINGS);  }
void inv_take_back_ladder(void) { inv_give(ITEM_LADDER); }
bool inv_has_ladder(void)       { return s_count[ITEM_LADDER] > 0; }
bool inv_has_wings(void)        { return s_count[ITEM_WINGS]  > 0; }
bool inv_flying(void)           { return s_fly_timer > 0; }
u16  inv_flying_remaining(void) { return s_fly_timer; }
void inv_cancel_flying(void)    { s_fly_timer = 0; }
