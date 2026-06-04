/* physics.h — состояние и физика Karnov.
 *
 * Константы (восстановлено из cs:0xFFA:0x1A1A — PRE-Karnov phase):
 *   GROUND_Y = 115, CEILING_Y = 75
 *   JUMP_VY_Q4 = -64 (Q4 = -4.0)
 *   GRAVITY_Q4 = +4 (Q4 = +0.25)
 *   WALK_SPEED = 1 px/frame
 *   Terminal velocity: [-96, +64] (Q4)
 */
#ifndef KARNOV_PHYSICS_H
#define KARNOV_PHYSICS_H

#include "karnov.h"

void physics_reset(int x, int y);
/* Меняет world-bounds для физики (clamp по X, ground/ceiling по Y).
 * По умолчанию: min_x=8, max_x=312, ground=115, ceiling=75 (LEVEL0 splash).
 * Для широкого LEVEL1+ game.c вызывает с min_x=8, max_x=level_w_px-8,
 * ground_y=240, ceiling_y=10. */
void physics_set_world_bounds(int min_x, int max_x, int ground_y, int ceiling_y);
/* Включает LEVEL0-only платформу «верх букв KARNOV» на y=45 для
 * x в [48..240]. См. physics.c::find_floor_y. Вызывается из game.c
 * при загрузке LEVEL0. */
void physics_set_level0_splash(bool enable);
/* LEVEL1 start area — slim каменная платформа под драконом (x∈[0..130]
 * y=260). Включается game.c при загрузке LEVEL1. */
void physics_set_level1_dragon_platform(bool enable);
void physics_tick(u8 input_state);
int  physics_get_x(void);
int  physics_get_y(void);
int  physics_get_ground_y(void);
bool physics_facing_left(void);

/* Ladder state. Karnov ставит лестницу нажатием INP_OPT (B) находясь на земле.
 * Возвращает true если лестница уже поставлена. ladder_x/top/bottom — её
 * мировые координаты. placed_frame — кадр в котором её поставили (для
 * анимации разворачивания). */
bool physics_ladder_placed(void);
int  physics_ladder_x(void);
int  physics_ladder_top(void);
int  physics_ladder_bot(void);
u16  physics_ladder_placed_frame(void);
bool physics_on_ladder(void);

/* Вызывается physics_tick'ом каждый кадр, чтобы внешний код мог
 * передавать «текущий кадр» (используется для placed_frame). */
void physics_set_current_frame(u16 frame);

/* Рендер Karnov-спрайта в текущей позе. M3+: использует sprite cache,
 * иначе fallback плейсхолдер. scroll_x/y — offset мирового пространства. */
void physics_render(u8 input_state, u16 frame, bool mode_cga, int scroll_x, int scroll_y);

#endif
