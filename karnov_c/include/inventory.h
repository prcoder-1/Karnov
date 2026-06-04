/* inventory.h — Karnov OPTIONS-инвентарь (упрощённый).
 *
 * Оригинал KARNOV.EXE: 10 предметов (Boots, Bomb, Ladder, Boomerang, Clapper,
 * Glasses, Swimming Mask, Wings, Shield, K-Mark), хранящихся в очереди
 * options-slots. Здесь реализованы только Ladder и Wings — остальное вне
 * скопа. Каждый предмет одно-слотовый: повторный pickup просто перезаписывает
 * флаг (как в оригинале при заполненной очереди отдельных слотов).
 *
 * Активация: один общий OPT-key. Приоритет wings → ladder (если wings есть и
 * полёт не активен, OPT включает полёт; иначе кладёт лестницу). Это упрощение
 * настоящего «cycle-select», но даёт детерминированное поведение под наш
 * single-button input.
 */
#ifndef KARNOV_INVENTORY_H
#define KARNOV_INVENTORY_H

#include "karnov.h"

typedef enum {
    ITEM_NONE = 0,
    ITEM_LADDER,
    ITEM_WINGS,
} item_kind_t;

/* Сброс инвентаря и состояния полёта. Вызывается из physics_reset. */
void inv_reset(void);

/* Положить предмет в инвентарь (вызывается из ai_try_pickup). */
void inv_give_ladder(void);
void inv_give_wings(void);

/* Вернуть лестницу в инвентарь (Karnov слез с неё к низу — лестница
 * сложилась обратно). */
void inv_take_back_ladder(void);

bool inv_has_ladder(void);
bool inv_has_wings(void);
bool inv_flying(void);             /* wings активны и таймер > 0 */
u16  inv_flying_remaining(void);   /* frames до окончания полёта */

/* Попытка использовать инвентарь по нажатию OPT.
 * Приоритет: wings (если есть и не летит) → ladder.
 * Возвращает что именно использовано, чтобы physics_tick применил эффект. */
item_kind_t inv_consume_for_opt(void);

/* Тик каждого кадра — декрементирует fly-timer. */
void inv_tick(void);

/* Принудительно прервать полёт (например, если Karnov ушёл за край).
 * Wings всё равно расходованы — это не возврат. */
void inv_cancel_flying(void);

#endif
