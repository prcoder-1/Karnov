/* game.h — главный игровой цикл и состояния. */
#ifndef KARNOV_GAME_H
#define KARNOV_GAME_H

#include "karnov.h"

namespace karnov {

/* Этапы кадра. Имена сохранены из строк KARNOV.EXE (DS:0x318..0x389). */
void update_locs();        /* UPDATELOCS    — DS:0x318 */
void post_hits();          /* POSTHITS      — DS:0x323 */
void do_controls();         /* DOCONTROLS    — DS:0x32C */
void check_triggers();      /* CHECKTRIGGERS — DS:0x337 */
void pre_karnov();          /* PRE-KARNOV    — DS:0x345 */
/* THINKCYCLE определён в think.h/think.cpp (cs:0xD4B:0xE03, file 0x0FAB3). */
void post_karnov();         /* POST-KARNOV   — DS:0x35B */
/* scroll_view определён в scroll.h (cs:0x15F:0x296). */
void draw_cycle();          /* DRAWCYCLE     — DS:0x372 */
/* scroll_update определён в scroll.h (cs:0x15F:0x195). */
void erase_cycle();         /* ERASECYCLE    — DS:0x389 */

/* Обновить всех игроков. cs:0x6E:0x2D0 (cs:0x21B0). */
void update_players();

/* Главный цикл: вернуть, когда игрок выбрал Quit. cs:0x4Cxx — обрамляющая
 * функция, вызывающая bk_init, level_init, sprite_init. */
int run_game();

/* Стартовый init-сценарий (вызов из main): соответствует функции по
 * cs:0x4BE0, которая проверяет bk_init/level_init/sprite_init и печатает
 * соответствующие *_failed-сообщения. */
int init_all_subsystems();

/* Запросить уровень и установить переменные current_level / max_level_reached.
 * Возвращает LL_OK (0) при успехе или код ошибки load_level() (см. level.h). */
int start_level(u16 level_number);

/* Конец игры: «THE END Player %1d» / «Game Over Player %1d». */
void show_end_screen(u16 player_index);

} /* namespace karnov */

#endif
