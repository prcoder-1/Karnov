/* Модуль 0x04E4 (file 0x6640) — партия: игроки, главный цикл, конец игры.
 * Состояние текущего игрока (plr) при смене игрока копируется в
 * player_rec[cur_player] и обратно. */
#include <stdio.h>
#include <string.h>
#include "fsopen.h"
#include "orig/fn.h"
#include "orig/port.h"
#include "orig/hw.h"

/* @04E4:000B file:0664B draw_text — по центру 40-символьной строки */
void draw_text(const char *s, s16 row)
{
    bios_set_cursor(row, (s16)(0x14 - ((s16)strlen(s) >> 1)));
    con_puts(s);
}

/* Пока файла нет — просить диск и ждать клавишу (порт: общий шаблон
 * загрузчиков оригинала). */
void wait_for_file(const char *name, const char *msg)
{
    FILE *fp;
    while ((fp = karnov_fopen(name, "rb")) == NULL) {
        clear_screen();
        draw_text(msg, 10);
        draw_text("any key when ready", 12);
        while (read_key() == 0)
            ;
        clear_screen();
    }
    fclose(fp);
}

/* @04E4:003F file:0667F start_level — блок игрока, загрузка уровня,
 * границы мира и камеры (в колонках буфера, cw на тайл) */
static void start_level(void)
{
    s16 cw;
    plr = player_rec[cur_player];
    if (demo_mode == 0) {
        clear_screen();
        sprintf(msg_buf, "Ready Player %1d", cur_player + 1);
        draw_text(msg_buf, 10);
    }
    enter_level();
    cw = 8 >> tile_shift;
    world_rect[0] = 0;
    world_rect[1] = 0;
    world_rect[2] = map_cols * cw;
    world_rect[3] = map_rows * 10;
    cam_rect[0] = -5 * cw;
    cam_rect[1] = -30;
    cam_rect[2] = (map_cols + 9) * cw;
    cam_rect[3] = (map_rows + 3) * 10;
    frame_no = 0;
    init_object_pool();
    level_clear_state();
    level_enter();
}

/* @04E4:018D file:067CD end_life — после выхода из main_loop: смерть
 * (plr.player_state 6/7) или демо — жизнь долой; иначе — следующий уровень.
 * Кончились жизни или пройден уровень 9 — экран конца игры. */
void end_life(void)
{
    s16 swap = 0, prev_level;
    play_music(SND_STOP, 0x64, 0);
    prev_level = plr.current_level;
    if (plr.player_state == 6 || plr.player_state == 7 || demo_mode != 0) {
        if (demo_mode == 0)
            plr.lives--;
        if (plr.lives == 0) {
            game_over_screen();
            if (--players_left != 0)
                swap++;
        }
        if (demo_mode != 0) {
            plr.respawn_D8CC = 10;
            plr.respawn_y = 10;
            plr.respawn_x = 10;
        }
        player_rec[cur_player] = plr;
        if (players_left == 2 && demo_mode == 0)
            swap++;
        if (swap)
            cur_player = 1 - cur_player;
        plr = player_rec[cur_player];
        if (demo_mode != 0)
            demo_ptr->pos = 0;
    } else {
        plr.current_level++;
        plr.respawn_D8CC = 10;
        plr.respawn_y = 10;
        plr.respawn_x = 10;
        if (plr.current_level > 9) {
            game_over_screen();
            if (--players_left != 0)
                swap++;
        }
        player_rec[cur_player] = plr;
        if (swap)
            cur_player = 1 - cur_player;
        plr = player_rec[cur_player];
    }
    free_all_objects();
    game_D564 = 1;
    in_game = 0;
    if (attract_on != 0 && demo_mode != 0 && prev_level == 0)
        show_hiscores(1000);
}

/* @04E4:0326 file:06966 main_loop — 11 фаз; кадр не короче 8 тиков PIT.
 * Время загрузки уровня в timer_ticks не засчитывается. */
void main_loop(void)
{
    u32 t;
    in_game = 1;
    t = timer_ticks;
    start_level();
    timer_ticks = t;
    do {
        frame_no++;
        port_frame_hook();
        t = timer_ticks;
        UPDATELOCS();
        POSTHITS();
        DOCONTROLS();
        CHECKTRIGGERS();
        PRE_KARNOV();
        THINKCYCLE();
        POST_KARNOV();
        SCROLLVIEW();
        if (in_game != 0) {
            DRAWCYCLE();
            SCROLLUPDATE();
            ERASECYCLE();
        }
        while ((s32)(timer_ticks - t) < 8)
            hw_wait_tick();
    } while (in_game != 0);
}

/* @04E4:03EB file:06A2B new_game — записи игроков: 5 жизней, уровень 1
 * (в демо — 0), огневая мощь 1, очки 0 */
static void new_game(void)
{
    s16 si;
    PlayerRec *r;
    video_reset();
    num_players = 1;
    if (demo_mode != 0) {
        draw_text("Karnov (C) 1987 Data East USA Inc.", 0x12);
        draw_text("Adaptation by Quicksilver Software Inc", 0x13);
    } else {
        num_players += two_players;
    }
    for (si = 0, r = player_rec; si < num_players; si++, r++) {
        r->lives = 5;
        r->current_level = demo_mode != 0 ? 0 : 1;
        r->firepower = 1;
        r->respawn_D8CC = 10;
        r->respawn_y = 10;
        r->respawn_x = 10;
        items_reset(r);
        bcd_set(r->score_bcd, 8, 0);
        r->score_dirty = 1;
        bcd_set(r->k_digits, 2, 0);
        r->k_dirty = 1;
    }
    cur_player = 0;
    players_left = num_players;
    game_D564 = 1;
    timer_ticks = 0;
}

/* @04E4:04FA file:06B3A game_over_screen — «THE END» с музыкой после
 * уровня 9, иначе «Game Over»; затем таблица рекордов */
void game_over_screen(void)
{
    clear_screen();
    if (plr.current_level > 9) {
        sprintf(msg_buf, "THE END  Player %1d", cur_player + 1);
        draw_text(msg_buf, 10);
        if (opt_sfx != 0) {
            play_music(SND_STOP, 0x1000, 0);
            delay(100);
            play_music(MUSIC(0x0791), 0x1000, 0);
            delay(5);
            while (music_done() == 0)
                delay(1);
            goto scores;
        }
    } else {
        sprintf(msg_buf, "Game Over Player %1d", cur_player + 1);
        draw_text(msg_buf, 10);
    }
    delay(200);
scores:
    hiscore_enter(plr.score_bcd);
}

/* @04E4:05D4 file:06C14 play_game */
static void play_game(void)
{
    demo_mode = 0;
    new_game();
    while (players_left > 0 && quit_game == 0)
        main_loop();
}

/* @04E4:05F7 file:06C37 attract — демо, пока его не прервут */
static void attract(void)
{
    if (attract_on == 0)
        return;
    demo_mode = 1;
    new_game();
    while (demo_mode != 0 && quit_game == 0)
        main_loop();
}

/* @04E4:0621 file:06C61 game_run */
void game_run(void)
{
    set_music_enabled(opt_music);
    set_sfx_enabled(opt_sfx);
    two_players = 0;
    demo_buf.count = 0;
    cheat_pos = 0;
    god_mode = 0;
    while (quit_game == 0) {
        if (quit_game == 0)
            attract();
        if (quit_game != 0)
            continue;
        play_game();
    }
}
