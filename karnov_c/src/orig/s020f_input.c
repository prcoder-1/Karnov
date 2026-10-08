/* Модуль 0x020F (file 0x38F0) — ввод: клавиатура BIOS, джойстик,
 * горячие клавиши, демо.
 *
 * Клавиатурное управление «залипает»: направление задаёт цифровой блок
 * (1..9, 5 — стоп), огонь — пробел или X, опция — Z; отпускание клавиш
 * не отслеживается. read_key держит включёнными Caps/Num/Scroll Lock,
 * поэтому буквы приходят заглавными, а цифровой блок — цифрами. Цифры
 * верхнего ряда (скан-код ≤ 0x46) переключают настройки. */
#include "orig/fn.h"
#include "orig/hw.h"
#include "orig/todo.h"

/* @020F:000A file:038FA read_key — 0, если клавиш нет; скан-код — в inp_scan */
s16 read_key(void)
{
    s16 si = (s8)hw_peekb(0, 0x417);
    si |= 0x70;
    hw_pokeb(0, 0x417, (u8)si);
    if (rtl_bioskey(1) == 0)
        return 0;
    si = rtl_bioskey(0);
    inp_scan = si >> 8;
    return si & 0xFF;
}

/* @020F:005C file:0394C read_joystick — биты как у клавиатуры */
static s16 read_joystick(void)
{
    s16 si = 0, x, y, btn;
    joy_data = joy_read();
    x = joy_data[0];
    y = joy_data[1];
    btn = joy_data[4];
    if (x <= joy_x_lo)
        si |= 2;
    else if (x >= joy_x_hi)
        si |= 1;
    if (y <= joy_y_lo)
        si |= 8;
    else if (y >= joy_y_hi)
        si |= 4;
    si |= btn << 4;
    return si;
}

/* @020F:00DA file:039CA read_keyboard — kb_mask по последней клавише */
static u8 read_keyboard(void)
{
    if (player_state == 1 && (kb_mask & 8))
        kb_mask &= 0xFFF7;
    if (inp_key >= '1' && inp_key <= '9' && inp_scan <= 0x46)
        return (u8)kb_mask;
    switch (inp_key) {
    case '1':
        kb_mask = (kb_mask & 0xFFF6) | 6;
        inp_prev_mask &= 0xFE;
        break;
    case '2':
        kb_mask = (kb_mask & 0xFFF7) | 4;
        inp_prev_mask &= 0xFC;
        break;
    case '3':
        kb_mask = (kb_mask & 0xFFF5) | 5;
        inp_prev_mask &= 0xFD;
        break;
    case '4':
        kb_mask = (kb_mask & 0xFFFE) | 2;
        inp_prev_mask &= 0xFC;
        break;
    case '5':
        kb_mask = 0;
        break;
    case '6':
        kb_mask = (kb_mask & 0xFFFD) | 1;
        inp_prev_mask &= 0xFC;
        break;
    case '7':
        if (player_state != 1)
            kb_mask = (kb_mask & 0xFFFA) | 0xA;
        inp_prev_mask &= 0xFE;
        break;
    case '8':
        if (player_state != 1)
            kb_mask = (kb_mask & 0xFFFB) | 8;
        inp_prev_mask &= 0xFC;
        break;
    case '9':
        if (player_state != 1)
            kb_mask = (kb_mask & 0xFFF9) | 9;
        inp_prev_mask &= 0xFD;
        break;
    case ' ': case 'X':
        kb_mask |= 0x10;
        break;
    case 'Z':
        kb_mask |= 0x20;
        break;
    default:
        kb_mask &= 0xF;
        break;
    }
    return (u8)kb_mask;
}

/* @020F:022B file:03B1B joystick_calibrate — центр по 4 замерам */
static void joystick_calibrate(void)
{
    s16 sx, sy, k;
    joy_data = joy_read();
    delay(2);
    joy_data = joy_read();
    delay(2);
    joy_data = joy_read();
    delay(2);
    sx = sy = 0;
    for (k = 0; k < 4; k++) {
        joy_data = joy_read();
        delay(2);
        sx += joy_data[0];
        sy += joy_data[1];
    }
    sx /= 4;
    sy /= 4;
    joy_x_hi = sx / 2;
    joy_y_hi = sy / 2;
    joy_x_lo = sx - joy_x_hi;
    joy_y_lo = sy - joy_y_hi;
    joy_x_hi += sx;
    joy_y_hi += sy;
}

/* @020F:02FB file:03BEB DOCONTROLS — фаза 3. При проигрывании демо маску
 * задаёт demo_play; клавиша Enter (или кнопка джойстика) прерывает демо. */
void DOCONTROLS(void)
{
    s16 si;
    inp_key = read_key();
    inp_prev_mask = inp_mask;
    inp_prev_fire = inp_fire;
    inp_prev_option = inp_option;
    if (level_loading == 0) {
        if (game_D564 & 4) {
            if (demo_play(demo_ptr) != 0) {
                if (demo_mode != 0)
                    end_life();
                else
                    game_D564 = 1;
            }
            if (demo_mode != 0 && opt_joystick != 0 && (read_joystick() & 0x30))
                push_key(0x0D);
        } else {
            inp_mask = opt_joystick != 0 ? (u8)read_joystick() : read_keyboard();
        }
    }
    if (inp_mask != inp_prev_mask) {
        inp_option = inp_mask & 0x20;
        inp_fire = inp_mask & 0x10;
        inp_dir = inp_mask & 0xF;
        si = inp_dir & 3;
        if (si != 0)
            player_dir = si;
    }
    inp_dir_changed = (inp_mask & 0xF) != (inp_prev_mask & 0xF);
    push_key((s8)inp_key);
    while (rtl_bioskey(1) != 0)
        rtl_bioskey(0);
}

/* @020F:0416 file:03D06 push_key — горячие клавиши */
void push_key(s16 key)
{
    s16 sfx, mus;
    s8 k = (s8)key;
    if (k != 0 && k != 0x1A && k != 5 && k != 0xB)
        cheat_pos = 0;
    switch (k) {
    case 'L': case 'l':
        if (god_mode == 0)
            break;
        do
            k = (s8)read_key();
        while (k == 0);
        if (k <= '9' && k >= '0') {
            player_ptr->y_world = 0;
            player_ptr->x_world = 0;
            current_level = k - '0';
            karnov_die(6, 4);
        }
        break;
    case 'Q': case 'q':
        clear_screen();
        draw_text(s_exit_to_dos, 10);
        k = (s8)rtl_bioskey(0);
        if (k == 'y' || k == 'Y') {
            quit_game = 1;
            in_game = 0;
            players_left = 0;
        }
        break;
    case 0x1B:
        if (demo_mode != 0)
            goto toggle_players;
        sfx = get_sfx_enabled();
        mus = get_music_enabled();
        set_sfx_enabled(0);
        set_music_enabled(0);
        do
            k = (s8)read_key();
        while (k != 0x1B);
        set_sfx_enabled(sfx);
        set_music_enabled(mus);
        /* fallthrough */
    case '1':
toggle_players:
        if (inp_scan > 0x46 || demo_mode == 0)
            break;
        two_players = two_players == 0 ? 1 : 0;
        hud_options();
        break;
    case '2':
        if (inp_scan > 0x46)
            break;
        opt_joystick ^= 1;
        hud_options();
        if (opt_joystick != 0)
            joystick_calibrate();
        break;
    case '3':
        if (inp_scan > 0x46)
            break;
        opt_music ^= 1;
        set_music_enabled(opt_music);
        hud_options();
        break;
    case '4':
        if (inp_scan > 0x46)
            break;
        opt_sfx ^= 1;
        set_sfx_enabled(opt_sfx);
        play_sfx(SND_STOP, 1000);
        hud_options();
        break;
    case 0x0D:
        if (demo_mode == 0)
            break;
        attract_on = 0;
        end_life();
        attract_on = 1;
        demo_mode = 0;
        break;
    case 0x1A:
        if (cheat_pos == 0)
            cheat_pos++;
        else
            cheat_pos = 0;
        break;
    case 5:
        if (cheat_pos == 1 || cheat_pos == 2)
            cheat_pos++;
        else
            cheat_pos = 0;
        break;
    case 0xB:
        if (cheat_pos == 3)
            god_mode ^= 1;
        play_sound(6);
        cheat_pos = 0;
        break;
    }
}

/* @020F:06B7 file:03FA7 demo_set_attract — on: загрузить LEVELn.MOV,
 * иначе пустое демо; включить проигрывание */
void demo_set_attract(s16 on)
{
    demo_ptr = &demo_buf[0];
    if (on != 0)
        load_demo();
    else
        demo_ptr->pos = 0;
    game_D564 |= 4;
    respawn_D8CC = 0;
    respawn_y = 0;
    respawn_x = 0;
}
