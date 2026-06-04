/* inventory.c — реализация Karnov OPTIONS (ladder + wings).
 *
 * См. inventory.h для обзора и приоритетов.
 */
#include "inventory.h"

/* WINGS_DURATION_FRAMES = 825 ≈ 30 s @ 27.5 FPS.
 * Это эмпирическое значение (~30 s соответствует «short power-up»-ощущению,
 * описанному в обзорах оригинала). Точное число тиков пока НЕ байт-
 * верифицировано из KARNOV.EXE — оригинал использует sub_timer wrap 0..6
 * на ai_type03 INIT (см. karnov_cpp/src/ai_scripts.cpp:496, +0xE9), но
 * прямой связи с «временем полёта в секундах» из этого ещё не выведено.
 * TODO: уточнить из reverse'а handler'а ai_type03. */
#define WINGS_DURATION_FRAMES  825

static bool s_has_ladder = false;
static bool s_has_wings  = false;
static u16  s_fly_timer  = 0;   /* frames remaining; 0 = не летит */

void inv_reset(void)
{
    s_has_ladder = false;
    s_has_wings  = false;
    s_fly_timer  = 0;
}

void inv_give_ladder(void)      { s_has_ladder = true; }
void inv_give_wings(void)       { s_has_wings  = true; }
void inv_take_back_ladder(void) { s_has_ladder = true; }

bool inv_has_ladder(void)        { return s_has_ladder; }
bool inv_has_wings(void)         { return s_has_wings;  }
bool inv_flying(void)            { return s_fly_timer > 0; }
u16  inv_flying_remaining(void)  { return s_fly_timer; }

item_kind_t inv_consume_for_opt(void)
{
    /* Wings имеют приоритет, но только если не находимся уже в полёте
     * (повторное нажатие во время полёта не даёт ничего). */
    if (s_has_wings && s_fly_timer == 0) {
        s_has_wings = false;
        s_fly_timer = WINGS_DURATION_FRAMES;
        return ITEM_WINGS;
    }
    if (s_has_ladder) {
        s_has_ladder = false;
        return ITEM_LADDER;
    }
    return ITEM_NONE;
}

void inv_tick(void)
{
    if (s_fly_timer > 0) --s_fly_timer;
}

void inv_cancel_flying(void)
{
    s_fly_timer = 0;
}
