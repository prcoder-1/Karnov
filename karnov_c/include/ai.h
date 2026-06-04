/* ai.h — функционально-табличный AI-диспатчер.
 *
 * AI_HANDLERS[type] — указатель на функцию обработки врага type.
 * Реализованы: type 13 (Y-tracker), type 6 (бирд/options), type 512 (stone
 * man), TYPE_STONE_THROWER, TYPE_ROCK_PROJECTILE, TYPE_PLAYER_FIREBALL.
 * Остальные = ai_stub (no-op).
 */
#ifndef KARNOV_AI_H
#define KARNOV_AI_H

#include "karnov.h"

/* Инициализирует AI из level_enemies. Сбрасывает локальное состояние.
 * level_num позволяет добавлять splash-only декорации (stone-man +
 * катящиеся глыбы для LEVEL0 заставки).
 *
 * Для LEVEL0 (splash) всё спаунится upfront, как было раньше. Для LEVEL1+
 * враги остаются в `level_enemies[]` как pending и пуляются игроком в мир
 * через ai_spawn_pending() по мере прокрутки камеры. Это соответствует
 * оригинальной механике: «враг становится активным, когда его cur_x
 * попадает в поле зрения». */
void ai_init_from_level(u16 level_num);

/* Активирует pending-врагов из level_enemies, чьи cur_x попадают в
 * scroll_x..scroll_x+view_w+SPAWN_MARGIN. Вызывается каждый кадр после
 * update_scroll(). Внутри — sequential cursor, ничего не сортирует. */
void ai_spawn_pending(int scroll_x, int view_w, u16 current_frame);

/* Снимает alive=false с врагов, ушедших более чем 64 px за левый край
 * viewport. Слот свободен для следующего ai_spawn_pending. Не трогает
 * fireball'ы (сами despawn'ятся) и type 6 OPTIONS. */
void ai_despawn_offscreen(int scroll_x);

/* Прокачивает все живые AI-объекты по своим хендлерам. */
void ai_tick(u16 frame, u8 karnov_input);

/* Рендерит всех живых врагов (поверх тайл-карты). scroll_x/y — offset мира. */
void ai_render(bool mode_cga, int scroll_x, int scroll_y);

/* Проверяет пересечение Karnov-bbox (cx, cy — center x, feet y) с
 * наземными OPTIONS-предметами (type 6 с custom>0 — это showcase-иконки,
 * хранящие sprite_id предмета в поле custom). При пересечении предмет
 * удаляется из мира и кладётся в инвентарь (inv_give_*). Mapping
 * (user-verified 2026-05-29):
 *   custom == 39 → inv_give_ladder()
 *   custom == 45 → inv_give_wings()
 * Прочие #40 boots, #41 bomb, #42 fireball-ammo, #43 boomerang,
 * #44 mask, #47 aqualung, #48 treasure, #49 multi-shot — TODO
 * (см. project_inventory_items.md). */
void ai_try_pickup(int kx, int ky);

/* Спаунит Karnov-fireball в позиции (x, y) с горизонтальной скоростью
 * vx (px/frame). Вызывается из physics_tick на FIRE press (с rate-limit).
 * Снаряд летит по прямой, гасится при выходе за экран или столкновении
 * с врагом. На уроне врагу — сразу alive=false (1-hit kill для
 * упрощения; HP-логика не реализована).
 *
 * facing: -1 → влево, +1 → вправо. */
void ai_spawn_player_fireball(int x, int y, int facing, u16 frame);

/* Boss kill / scroll-pickup query (level end transition). */
bool ai_boss_dead(void);
bool ai_scroll_picked(void);
u16  ai_boss_dead_frame(void);
void ai_notify_enemy_hit(int slot, u16 frame);

#endif
