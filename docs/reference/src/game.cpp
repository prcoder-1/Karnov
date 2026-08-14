/* game.cpp — главный игровой цикл (etap-этапы кадра).
 *
 * В KARNOV.EXE этапы кадра — это отдельные функции, имена которых хранятся
 * в DS как строки и используются в отладочном «текущая фаза» (вероятно,
 * для отображения профиля или отладочной информации в углу экрана).
 *
 * Имена этапов (DS:0x318..0x389):
 *   UPDATELOCS, POSTHITS, DOCONTROLS, CHECKTRIGGERS, PRE-KARNOV,
 *   THINKCYCLE, POST-KARNOV, SCROLLVIEW, DRAWCYCLE, SCROLLUPDATE, ERASECYCLE
 *
 * Инициализация: cs:0x4BE0..0x4C58 — функция, которая вызывает
 *   bk_init()      cs:0x15F:0x646  → "bkinit failed!"      (DS:0x3A8)
 *   level_init()   cs:0x365:0x00B  → "level init failed!"  (DS:0x3B8)
 *   sprite_init()  cs:0xBFB:0x004  → "SpriteInit failed!"  (DS:0x3CC)
 */
#include "game.h"
#include "globals.h"
#include "input.h"
#include "demo.h"
#include "level.h"
#include "sprite.h"
#include "video.h"
#include "think.h"
#include "scroll.h"
#include <cstdio>

namespace karnov {

/* ─── Этапы кадра ───────────────────────────────────────────────────── */

/* KARNOV.EXE: одна из cs:0x?? функций (точное соответствие — по строке). */
void update_locs()     { /* TODO: обновление координат всех движущихся объектов */ }
void post_hits()       { /* TODO: пост-обработка попаданий */ }
void do_controls()      { process_input(); }
void check_triggers()   { /* TODO: проверка триггеров уровня */ }
void pre_karnov()       { /* TODO: подготовка позиции/состояния Карнова */ }
/* think_cycle определён в think.cpp (cs:0xD4B:0xE03, file 0x0FAB3). */
void post_karnov()      { /* TODO: пост-обновление Карнова */ }
/* scroll_view и scroll_update определены в scroll.cpp
 * (cs:0x15F:0x296 и cs:0x15F:0x195). */
void draw_cycle()       { /* TODO: DRAWCYCLE */ }
void erase_cycle()      { /* TODO: ERASECYCLE */ }

/* KARNOV.EXE: cs:0x6E:0x2D0 (file 0x21B0).
 * Итерирует по массивам игроков [DS:0x2694] (флаги) и [DS:0x2594]
 * (указатели на структуры) и вызывает per-player update.
 */
void update_players() {
    static u16  player_flags[MAX_PLAYERS] = {0, 0}; /* DS:0x2694 */
    static u8*  player_data [MAX_PLAYERS] = {nullptr, nullptr}; /* DS:0x2594 */

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (player_flags[i] != 0) {
            /* TODO: cs:0x2106 — per_player_update(player_data[i]); */
        }
    }
}

/* KARNOV.EXE: cs:0x4BE0 — init_all_subsystems.
 *
 *  ax = system_kind                 ; cs:0x7FB0
 *  [0xCDEE] = ax
 *  [0xCDF0] = cs:0x27D:0x0F()       ; что-то про экран/таймер
 *  [0x124]  = cs:0x1F3:0x07()       ; видеопамять?
 *  cs:0xCB9:0x4C()                  ; настройка ?
 *  if (cs:0x15F:0x646() != 0):      ; bk_init
 *      printf("bkinit failed!\n")
 *  if (cs:0x365:0x00B() != 0):      ; level_init (видимо, выделение буферов)
 *      printf("level init failed!\n")
 *  cs:0x6E:0xD1()                   ; еще одна init
 *  if (cs:0xBFB:0x04() != 0):       ; sprite_init
 *      printf("SpriteInit failed!\n")
 */
int init_all_subsystems() {
    /* TODO: cs:0x7FB0 — определение типа машины (PC/XT/AT/PS2 и т. п.). */

    /* bk_init: видимо, инициализация фона/планов. */
    /* if (bk_init() != 0) { printf("bkinit failed!\n"); } */

    /* level_init: выделение буферов под уровень. */
    /* if (level_buffers_init() != 0) { printf("level init failed!\n"); } */

    if (sprite_init() != 0) {
        std::printf("SpriteInit failed!\n");
        return 1;
    }
    return 0;
}

/* KARNOV.EXE: запуск конкретного уровня. */
int start_level(u16 level_number) {
    current_level   = level_number;
    current_frame   = 0;
    if (level_number > max_level_reached) max_level_reached = level_number;

    int rc = load_level(level_number);
    if (rc != LL_OK) {
        std::printf("level load failed for %u (rc=%d)\n",
                    (unsigned)level_number, rc);
        return rc;
    }

    /* В аттракт-режиме загружаем демо. */
    if (!in_play) {
        load_demo(level_number);
    }
    return LL_OK;
}

/* KARNOV.EXE: cs:0x6A40 — show_end_screen.
 *
 *   if (current_level == FINAL_LEVEL):
 *       draw "THE END  Player %1d" (DS:0xE47)
 *   else:
 *       draw "Game Over Player %1d" (DS:0xE5B)
 */
void show_end_screen(u16 player_index) {
    char buf[64];
    if (current_level == FINAL_LEVEL) {
        std::snprintf(buf, sizeof(buf), "THE END  Player %1u",
                      (unsigned)(player_index + 1));
    } else {
        std::snprintf(buf, sizeof(buf), "Game Over Player %1u",
                      (unsigned)(player_index + 1));
    }
    draw_text(12, buf);
}

/* Главный кадр. Соответствует последовательности вызовов из основной
 * tick-функции (имя из DS:0x318..0x389). */
static void tick_frame() {
    update_locs();
    post_hits();
    do_controls();           /* читает ввод (или демо, см. ниже) */
    check_triggers();
    pre_karnov();
    update_players();
    think_cycle();
    post_karnov();
    scroll_view();
    draw_cycle();
    scroll_update();
    erase_cycle();
    ++current_frame;
}

/* KARNOV.EXE: основной цикл (находится внутри обрамляющей функции при
 * cs:0x4C5A, которая включает «Ready Player», старт уровня и поплеер-цикл).
 *
 * Здесь — упрощённая высокоуровневая структура:
 *   for (level = 0; level < NUM_LEVELS; ++level):
 *       start_level(level)
 *       пока not is_demo_at_end || игрок жив:
 *           tick_frame()
 *           if in_play == 0 and demo: tick_demo(demo_play)
 *           if quit_requested: break
 *       if dead: show_end_screen() and restart
 */
int run_game() {
    if (init_all_subsystems() != 0) return 1;

    splash_screen();

    /* Аттракт-режим: проигрываем демо для уровней 0..2 по кругу.
     * Если ни один из аттракт-уровней не загружается, прекращаем —
     * иначе получится бесконечный спам «level load failed». */
    for (;;) {
        int loaded_in_pass = 0;
        for (int demo_level = 0; demo_level < 3; ++demo_level) {
            in_play = 0;
            if (start_level(static_cast<u16>(demo_level)) != LL_OK) {
                continue;
            }
            ++loaded_in_pass;
            while (!is_demo_at_end(demo_play)) {
                tick_frame();
                if (tick_demo(demo_play)) break;
                if (in_play) goto playing;   /* пользователь начал игру */
            }
        }
        if (loaded_in_pass == 0) {
            std::printf("attract mode: no demo level could be loaded — aborting\n");
            return 1;
        }
        continue;
    playing:
        /* TODO: основной режим — читаем настоящий ввод, переходим
         * по уровням, ведём подсчёт очков, обрабатываем смерти. */
        for (u16 lvl = 0; lvl < NUM_LEVELS; ++lvl) {
            start_level(lvl);
            while (in_play) {
                tick_frame();
            }
        }
        show_end_screen(0);
    }
    /* return 0; — недостижимо */
}

} /* namespace karnov */
