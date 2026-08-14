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
 * Ничего не спаунит: все спауны (включая LEVEL0 splash) происходят через
 * ai_check_triggers по близости игрока — как в оригинале. */
void ai_init_from_level(u16 level_num);

/* CHECKTRIGGERS (KARNOV.EXE cs:0x365:0x1C1, file 0x5011) — дословный порт.
 * Вызывается каждый кадр. Спаун срабатывает по близости ИГРОКА:
 * |player_x − cur_x| < range_x и |player_y − cur_y| < range_y (строго),
 * в окне [camera_left, player_x + 320px] по sorted-таблице записей.
 * flags записи — one-shot защёлка; центрального деспавна/респавна нет. */
void ai_check_triggers(int player_x_px, int player_y_px, int camera_left_px,
                       u16 frame);

/* Rearm после смерти игрока (file 0x1015A): снять все активные объекты и
 * перевзвести записи по правилу flags = (cur_x + range_x < player_x)?1:0.
 * player_x_px — позиция Karnov'а ПОСЛЕ респауна (в пикселях). */
void ai_respawn_rearm(int player_x_px);

/* Прокачивает все живые AI-объекты по своим хендлерам. */
void ai_tick(u16 frame, u8 karnov_input);

/* Рендерит всех живых врагов (поверх тайл-карты). scroll_x/y — offset мира. */
void ai_render(bool mode_cga, int scroll_x, int scroll_y);
void ai_render_overlay(bool mode_cga, int scroll_x, int scroll_y);

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
int  ai_scroll_target_x(void);   /* фикс. X (позиция boss'а) для авто-ходьбы к свитку */
bool ai_scroll_spawned(void);    /* свиток уже заспавнен и падает */
bool ai_scroll_onscreen(int cam_x, int cam_y, int view_w, int view_h); /* свиток виден на экране */
void ai_notify_enemy_hit(int slot, u16 frame);

/* Уничтожить всех живых damageable-врагов на экране (Clapper).
 * exclude_boss=true пропускает boss'а (Clapper); false убивает всех. */
void ai_clear_enemies(bool exclude_boss);

/* Bomb-предмет: спавн анимированной вспышки (#25..#28) в позиции Karnov'а +
 * ai_clear_enemies(false) (убивает всех, включая boss'а). Вызывается из
 * inv_use() (inventory.c). */
void ai_bomb_explode(void);

#endif
