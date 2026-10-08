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
/* Сменить точку респауна после смерти (checkpoint из ObjectRegion),
 * не сбрасывая текущее состояние. */
void physics_set_spawn_point(int x, int y);
/* Меняет world-bounds для физики (clamp по X, ground/ceiling по Y).
 * По умолчанию: min_x=8, max_x=312, ground=115, ceiling=75 (LEVEL0 splash).
 * Для широкого LEVEL1+ game.c вызывает с min_x=8, max_x=level_w_px-8,
 * ground_y=240, ceiling_y=10. */
void physics_set_world_bounds(int min_x, int max_x, int ground_y, int ceiling_y);
/* Камера (мировой Y верха вьюпорта) — обновляется game.c каждый кадр.
 * Нужна death_tick для байт-точного порога «скрыть тело за экраном»
 * ([0xCC94]+0x64, см. physics.c). LEVEL0-буквы KARNOV и LEVEL1
 * dragon-platform больше не требуют отдельных setter'ов — они уже solid
 * по данным уровня (attr=0), find_floor_y находит их generic-сканом. */
void physics_set_scroll_y(int scroll_y);
void physics_tick(u8 input_state);
int  physics_get_x(void);
int  physics_get_y(void);
int  physics_get_ground_y(void);
/* Мировые координаты игрока в конвенции объектов движка: x_world = ЛЕВЫЙ
 * край спрайта, y_world = ВЕРХ спрайта (пиксели). Оригинал кэширует ровно
 * их в [DS:0xD8BC]/[DS:0xD8BE] (file 0x10312), и AI-обработчики врагов
 * сравниваются именно с этими значениями. physics_get_x/y возвращают
 * внутреннюю конвенцию порта (центр тела / подошва) — для AI не годятся. */
int  physics_get_left_x(void);
int  physics_get_top_y(void);
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

/* Death state. Karnov входит в death-animation при контакте с водой
 * (level_is_water_at у ног). По окончании анимации respawn'ится в точке
 * physics_reset.
 *
 * physics_just_died() — однократный edge на кадре старта анимации;
 *   используется game.c чтобы запустить death-music (track 5).
 * physics_is_dying() — true пока проигрывается анимация. */
bool physics_just_died(void);
bool physics_is_dying(void);
void physics_set_invuln(bool on);   /* victory-секвенция: Karnov неуязвим */
bool physics_is_invuln(void);
/* Урон Карнову от касания врага (pair-loop, file 0xF81E): у игрока 5 HP
 * (file 0x101CE), при контакте вычитается HP врага; ноль — смерть.
 * Игнорируется во время i-frames, неуязвимости и анимации гибели. */
void physics_hit(int enemy_hp);
int  physics_hp(void);
/* Безусловная гибель от касания ГОЛОВЫ колонны (тип 20) — байт-точный
 * порт cs:0xE62:0x6FE (die_hit = set_player_anim(6,4)), вызываемого
 * напрямую из state5 колонны, МИМО physics_hit()/HP-обмена (см. ai.c,
 * ai_check_player_hit). НЕ играет SFX2 — в оригинале этот путь его не
 * задевает (см. комментарий в physics.c). */
void physics_kill_by_head_touch(void);
void physics_set_sit_scroll(bool on); /* level-end: поза «сидит со свитком» #0x60-0x62 */

/* Рендер Karnov-спрайта в текущей позе. M3+: использует sprite cache,
 * иначе fallback плейсхолдер. scroll_x/y — offset мирового пространства. */
void physics_render(u8 input_state, u16 frame, bool mode_cga, int scroll_x, int scroll_y);

#endif
