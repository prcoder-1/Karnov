/* Модуль 0x04E4 (file 0x6640) — партия: игроки, главный цикл, конец игры.
 *
 * Состояние текущего игрока — блок DS:D8AA..D94E (0xA5 байт); при смене
 * игрока он копируется (fmemcpy) в player_rec[cur_player] и обратно. */
#include <stdio.h>
#include <string.h>
#include "orig/fn.h"
#include "orig/port.h"
#include "orig/todo.h"
#include "orig/hw.h"

#define PLAYER_BLOCK 0xD8AA
#define PLAYER_SIZE  0xA5

static void player_save(void)
{
    ds_copy_out(PLAYER_BLOCK, PLAYER_SIZE, &player_rec[cur_player * PLAYER_SIZE]);
}

static void player_load(void)
{
    ds_copy_in(PLAYER_BLOCK, PLAYER_SIZE, &player_rec[cur_player * PLAYER_SIZE]);
}

/* @04E4:000B file:0664B draw_text — по центру 40-символьной строки */
void draw_text(const char *s, s16 row)
{
    bios_set_cursor(row, (s16)(0x14 - ((s16)strlen(s) >> 1)));
    con_puts(s);
}

/* @04E4:003F file:0667F start_level — блок игрока, загрузка уровня,
 * границы мира и камеры по видеорежиму */
static void start_level(void)
{
    player_load();
    if (demo_mode == 0) {
        clear_screen();
        sprintf(msg_buf, s_ready_player, cur_player + 1);
        draw_text(msg_buf, 10);
    }
    enter_level();
    switch (level_mode) {
    case 0: case 2:
        world_rect[0] = 0;
        world_rect[1] = 0;
        world_rect[2] = map_cols * 4;
        world_rect[3] = map_rows * 10;
        cam_rect[0] = -20;
        cam_rect[1] = -30;
        cam_rect[2] = (map_cols + 9) * 4;
        cam_rect[3] = (map_rows + 3) * 10;
        break;
    case 3:
        world_rect[0] = 0;
        world_rect[1] = 0;
        world_rect[2] = map_cols * 8;
        world_rect[3] = map_rows * 10;
        cam_rect[0] = -40;
        cam_rect[1] = -30;
        cam_rect[2] = (map_cols + 9) * 8;
        cam_rect[3] = (map_rows + 3) * 10;
        break;
    case 1:
        world_rect[0] = 0;
        world_rect[1] = 0;
        world_rect[2] = map_cols * 2;
        world_rect[3] = map_rows * 10;
        cam_rect[0] = -10;
        cam_rect[1] = -30;
        cam_rect[2] = (map_cols + 9) * 2;
        cam_rect[3] = (map_rows + 3) * 10;
        break;
    }
    frame_no = 0;
    init_object_pool();
    level_clear_state();
    level_enter();
}

/* @04E4:018D file:067CD end_life — после выхода из main_loop: смерть
 * (player_state 6/7) или демо — жизнь долой; иначе — следующий уровень.
 * Кончились жизни или пройден уровень 9 — экран конца игры. */
void end_life(void)
{
    s16 swap = 0, prev_level;
    play_music(SND_STOP, 0x64, 0);
    prev_level = current_level;
    if (player_state == 6 || player_state == 7 || demo_mode != 0) {
        if (demo_mode == 0)
            lives--;
        if (lives == 0) {
            game_over_screen();
            if (--players_left != 0)
                swap++;
        }
        if (demo_mode != 0) {
            plr_D8C8[2] = 10;
            plr_D8C8[1] = 10;
            plr_D8C8[0] = 10;
        }
        player_save();
        if (players_left == 2 && demo_mode == 0)
            swap++;
        if (swap)
            cur_player = 1 - cur_player;
        player_load();
        if (demo_mode != 0)
            demo_ptr->pos = 0;
    } else {
        current_level++;
        plr_D8C8[2] = 10;
        plr_D8C8[1] = 10;
        plr_D8C8[0] = 10;
        if (current_level > 9) {
            game_over_screen();
            if (--players_left != 0)
                swap++;
        }
        player_save();
        if (swap)
            cur_player = 1 - cur_player;
        player_load();
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
        frame_total++;
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
        play_ticks += (s32)(timer_ticks - t);
    } while (in_game != 0);
}

/* @04E4:03EB file:06A2B new_game — записи игроков: 5 жизней, уровень 1
 * (в демо — 0), огневая мощь 1, очки 0 */
static void new_game(void)
{
    s16 si;
    u8 *r;
    video_reset();
    num_players = 1;
    if (demo_mode != 0) {
        draw_text(s_copyright, 0x12);
        draw_text(s_adaptation, 0x13);
    } else {
        num_players += two_players;
    }
    for (si = 0, r = player_rec; si < num_players; si++, r += PLAYER_SIZE) {
        r[2] = 5; r[3] = 0;
        r[0] = demo_mode != 0 ? 0 : 1; r[1] = 0;
        r[0x9F] = 1; r[0xA0] = 0;
        r[0x22] = 10; r[0x23] = 0;
        r[0x20] = 10; r[0x21] = 0;
        r[0x1E] = 10; r[0x1F] = 0;
        items_reset(r);
        bcd_set(r + 0x86, 8, 0);
        r[0x8E] = 1; r[0x8F] = 0;
        bcd_set(r + 0x95, 2, 0);
        r[0x97] = 1; r[0x98] = 0;
    }
    cur_player = 0;
    players_left = num_players;
    game_D564 = 1;
    timer_ticks = 0;
    frame_total = 0;
}

/* @04E4:04FA file:06B3A game_over_screen — «THE END» с музыкой после
 * уровня 9, иначе «Game Over»; затем таблица рекордов */
void game_over_screen(void)
{
    clear_screen();
    if (current_level > 9) {
        sprintf(msg_buf, s_the_end, cur_player + 1);
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
        sprintf(msg_buf, s_game_over, cur_player + 1);
        draw_text(msg_buf, 10);
    }
    delay(200);
scores:
    hiscore_enter(score_bcd);
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
    game_0CDD6 = 0;
    set_music_enabled(opt_music);
    set_sfx_enabled(opt_sfx);
    two_players = 0;
    demo_buf[0].count = 0;
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
