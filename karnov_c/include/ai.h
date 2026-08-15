/* ai.h — таблица обработчиков AI (порт AI_HANDLERS из DS:0x10FE).
 *
 * Покрыты все типы 4..57 и псевдотипы порта. Объектная конвенция
 * координат: x — пиксели (= 2 × x_world), y — ВЕРХ спрайта (KARNOV.md §3.3).
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
 * flags записи — one-shot защёлка; отставшие враги снимаются отдельно,
 * в ai_despawn_behind (порт off-screen despawn'а из THINKCYCLE). */
/* Pair-loop порта: касание враждебного объекта наносит Карнову удар
 * (physics_hit). Бокс игрока задаётся центром/подошвой и размерами. */
void ai_check_player_hit(int kx, int ky, int kw, int kh);

void ai_check_triggers(int player_x_px, int player_y_px, int camera_left_px,
                       u16 frame);

/* Off-screen despawn (THINKCYCLE фаза B, file 0xFD6A): объекты с нечётным
 * subtype, отставшие от игрока более чем на 250 px и не являющиеся
 * goal-объектом, снимаются. Вызывается из ai_tick. */
void ai_despawn_behind(int player_x_px);

/* Rearm после смерти игрока (file 0x1015A): снять все активные объекты и
 * перевзвести записи по правилу flags = (cur_x + range_x < player_x)?1:0.
 * player_x_px — позиция Karnov'а ПОСЛЕ респауна (в пикселях). */
void ai_respawn_rearm(int player_x_px);

/* Прокачивает все живые AI-объекты по своим хендлерам. */
void ai_tick(u16 frame, u8 karnov_input);

/* Рендерит всех живых врагов (поверх тайл-карты). scroll_x/y — offset мира. */
void ai_render(bool mode_cga, int scroll_x, int scroll_y);
void ai_render_overlay(bool mode_cga, int scroll_x, int scroll_y);

/* Пересечение Karnov-bbox (cx — центр, cy — ноги) с предметами в мире
 * (типы 4/5/6, sprite_id хранится в item_sprite при спавне) и со
 * свитком-наградой (is_scroll). Подобранный предмет уходит в инвентарь
 * по формуле item_id = sprite − 39 (полная 10-предметная модель,
 * inventory.h). */
void ai_try_pickup(int kx, int ky);

/* Спаунит Karnov-fireball в позиции (x, y) с горизонтальной скоростью
 * vx (px/frame). Вызывается из physics_tick на FIRE press (с rate-limit).
 * Снаряд летит по прямой, гасится при выходе за экран или столкновении
 * с врагом. Урон = PLAYER_FIREBALL_DAMAGE (10) из пула HP врага.
 *
 * facing: -1 → влево, +1 → вправо. */
void ai_spawn_player_fireball(int x, int y, int facing, u16 frame);

/* Boss kill / scroll-pickup query (level end transition). */
int  ai_debug_count_scrolls(void);
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
