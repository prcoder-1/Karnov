/* inventory.h — Karnov OPTIONS-инвентарь (полная 10-предметная модель).
 *
 * Оригинал KARNOV.EXE: 10 предметов (OPTIONS), item_id = icon_sprite - 39
 * (disassembly-verified):
 *   0=Ladder, 1=Boots, 2=Bomb, 3=Fire(passive), 4=Boomerang,
 *   5=Clapper, 6=Glasses, 7=Mask, 8=Wings, 9=Shield.
 * Каждый предмет STOCKABLE — хранится счётчик count[id]. Иконка предмета —
 * sprite #(39 + item_id) (спрайты #39..48).
 *
 * Управление (оригинал): один общий OPT-key (INP_OPT = Z). При OPT+LEFT/RIGHT
 * — CYCLE выбранного предмета; при OPT без направления — USE выбранного.
 *
 * Wrapper-функции inv_has_ladder/inv_has_wings/inv_flying/... сохранены для
 * совместимости со старыми вызовами в physics.c / ai.c / game.c.
 */
#ifndef KARNOV_INVENTORY_H
#define KARNOV_INVENTORY_H

#include "karnov.h"

/* Порядок предметов — из таблицы «использовать» DS:0x0C30 и обработчика
 * подбора option_pickup (cs:0x41D:0x9BE, file 0x638E). Подбираемых id
 * ОДИННАДЦАТЬ (0..10, спрайты #39..#49): рандом предмета в оригинале —
 * `rand() % 11` (file 0x12477), ротация выбора — тоже mod 11 (file 0x633D).
 *
 *   0 LADDER(#39)   спавн объекта-лестницы, не расходуется
 *   1 BOOTS(#40)    таймер [0xD8B4] = 250 кадров, двойной прыжок
 *   2 BOMB(#41)     спавн объекта-взрыва
 *   3 FIRE(#42)     «30 огненных шаров»: запас [0xD943], оружие [0xD947]=1
 *   4 BOOMERANG(#43) оружие [0xD947]=2, cooldown до возврата
 *   5 GLASSES(#44)  таймер [0xD8B8]=150 — показывает скрытые предметы
 *   6 WINGS(#45)    таймер [0xD8B6]=200 + переинициализация игрока
 *   7 SHIELD(#46)   в EXE обработчик — пустой стаб
 *   8 MASK(#47)     акваланг: только в воде ([0xD8BA]=1)
 *   9 KMARK(#48)    спрайт — буква «K»; копится, даёт жизнь; «использовать» нельзя
 *  10 SUPERFIRE(#49) ШАР: firepower++ (макс 3) — раздвоение/растроение
 *                   выстрела; «использовать» нельзя, эффект при подборе
 *
 * Прежний порядок в порту был сдвинут с id5 (Clapper/Glasses/Mask/Wings/
 * Shield), а 11-го предмета не было вовсе. */
typedef enum {
    ITEM_LADDER = 0,
    ITEM_BOOTS,
    ITEM_BOMB,
    ITEM_FIRE,
    ITEM_BOOMERANG,
    ITEM_GLASSES,
    ITEM_WINGS,
    ITEM_SHIELD,
    ITEM_MASK,
    ITEM_KMARK,
    ITEM_SUPERFIRE,
    ITEM_NUM = 11
} item_id_t;

/* Сброс инвентаря и состояния полёта. Вызывается из physics_reset. */
void inv_reset(void);

/* Добавить предмет в инвентарь. K-Mark считается отдельно (kmarks). */
void inv_give(int id);

/* Подобрать предмет по sprite_id: #39..48 → inv_give(sprite-39). */
void inv_give_by_sprite(u16 sprite);

int  inv_count(int id);      /* count[id] */
int  inv_current(void);      /* индекс выбранного предмета, -1 если пусто */
int  inv_distinct(void);     /* число типов предметов с count>0 */

/* Переключить выбор на следующий (dir>0) / предыдущий (dir<0) предмет
 * с count>0, wrapping mod 10. Если ничего нет — current=-1. */
void inv_cycle(int dir);

/* Использовать выбранный предмет. Возвращает item_id, либо -1 если нечего.
 * Применяет НЕ-физические эффекты внутри; для LADDER/WINGS только возвращает
 * id — физику (постановку/полёт) выполняет physics.c. */
int  inv_use(void);

bool inv_boots_active(void); /* boots_timer > 0 */

/* Сила огня 1..3 ([DS:0xD949] оригинала): столько снарядов вылетает за
 * выстрел. Растёт подбором «шара» (#49, id 10) до 3, сбрасывается в 1
 * при (ре)спавне Карнова (file 0x104DE). */
int  inv_firepower(void);

/* Показывать ли скрытые предметы (таймер Glasses, [DS:0xD8B8]). */
bool inv_glasses_active(void);

/* Тик каждого кадра — декрементирует boots_timer и fly_timer. */
void inv_tick(void);

/* ─── Compat-обёртки для существующих вызовов ─────────────────────────── */
void inv_give_ladder(void);      /* → inv_give(ITEM_LADDER) */
void inv_give_wings(void);       /* → inv_give(ITEM_WINGS)  */
void inv_take_back_ladder(void); /* → inv_give(ITEM_LADDER) */
bool inv_has_ladder(void);       /* count[ITEM_LADDER] > 0  */
bool inv_has_wings(void);        /* count[ITEM_WINGS]  > 0  */
bool inv_flying(void);           /* fly_timer > 0 */
u16  inv_flying_remaining(void); /* fly_timer */
void inv_cancel_flying(void);    /* fly_timer = 0 */

#endif
