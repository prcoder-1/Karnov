/* Модуль 0x041D (file 0x59D0) — HUD, десятичные счётчики, предметы.
 *
 * Счётчики (очки, таймер, «K», патроны) — неупакованные десятичные цифры,
 * младшая первой; значения для bcd_* передаются как BCD-long (0x2600 =
 * 2600). HUD рисуется asm-блиттером сразу на обе страницы EGA. */
#include "orig/fn.h"
#include "orig/todo.h"

/* @041D:0002 file:059D2 hud_put — спрайт HUD в (col, row); show = 0 —
 * стереть. В демо ничего не рисуется. */
void hud_put(s16 sprite, s16 col, s16 row, s16 show)
{
    u16 di;
    s16 si;
    if (demo_mode != 0)
        return;
    si = row;
    switch (level_mode) {
    case 0: case 2:
        di = si & 1 ? 0x2000 : 0;
        si &= 0xFFFE;
        di += (u16)(si * 40 + col);
        break;
    case 3:
        di = tandy_row_off[si & 3];
        si &= 0xFFFC;
        di += (u16)(si * 40 + col * 2);
        break;
    case 1:
        di = si & 1 ? 0x2000 : 0;
        di += (u16)(si * 40 + (col >> 1));
        if (show) {
            hud_blit(di, sprite, col & 1);
            hud_blit((u16)(di + 0x1F40), sprite, col & 1);
        } else {
            hud_erase(di, sprite, col & 1);
            hud_erase((u16)(di + 0x1F40), sprite, col & 1);
        }
        return;
    default:
        di = 0;     /* в оригинале не задаётся */
        break;
    }
    if (show)
        hud_blit(di, sprite, 0);
    else
        hud_erase(di, sprite, 0);
}

/* @041D:0129 file:05AF9 hud_item */
void hud_item(s16 i, s16 show)
{
    hud_put(i + 0x27, i * 4, 0x96, show);
}

/* @041D:014E file:05B1E hud_sel — рамка выбранного предмета */
void hud_sel(s16 i, s16 show)
{
    if (i >= 0)
        hud_put(0x33, i * 4, 0xA2, show);
}

/* @041D:0174 file:05B44 hud_life */
static void hud_life(s16 i, s16 show)
{
    hud_put(0x32, i * 4 + (cur_player != 0 ? 0x37 : 0), 0xAE, show);
}

/* @041D:01A6 file:05B76 hud_lives */
void hud_lives(void)
{
    s16 si;
    for (si = 0; si < 6; si++)
        hud_life(si, lives - 1 > si);
}

/* @041D:01CF file:05B9F hud_items */
void hud_items(void)
{
    s16 i;
    for (i = 0; i < 0xB; i++)
        hud_item(i, item_count[i] > 0);
}

/* @041D:0206 file:05BD6 items_reset — в записи игрока (раскладка блока D8AA) */
void items_reset(u8 *rec)
{
    s16 si;
    rec[0x4E] = 0; rec[0x4F] = 0;
    rec[0x50] = 0xFF; rec[0x51] = 0xFF;
    for (si = 0; si < 0xB; si++) {
        rec[0x52 + si * 2] = 0; rec[0x53 + si * 2] = 0;
        rec[0x68 + si * 2] = 0; rec[0x69 + si * 2] = 0;
    }
}

/* @041D:0244 file:05C14 hud_sel_clear */
void hud_sel_clear(void)
{
    s16 i;
    for (i = 0; i <= 8; i++)
        hud_sel(i, 0);
}

/* @041D:026A file:05C3A hud_player_tag — метка «1UP/2UP» */
static void hud_player_tag(s16 p, s16 show)
{
    hud_put(p + 0x34, (p != 0 ? 0x37 : 0) + (p != 0 ? -2 : 0x1C), 0xB8, show);
}

/* @041D:02A3 file:05C73 hud_player_tags_clear */
static void hud_player_tags_clear(void)
{
    s16 si;
    for (si = 0; si < 2; si++)
        hud_player_tag(si, 0);
}

/* @041D:02BF file:05C8F bcd_set — n цифр из BCD-long */
void bcd_set(u8 *p, s16 n, s32 v)
{
    s16 si;
    for (si = 0; si < n; si++) {
        p[si] = (u8)(v & 0xF);
        v >>= 4;
    }
}

/* @041D:02F4 file:05CC4 bcd_add */
void bcd_add(u8 *p, s16 n, s32 v)
{
    s16 si, more = 1, carry = 0;
    u8 d = 0, r;
    for (si = 0; si < n; si++) {
        if (more) {
            if (v == 0) {
                d = 0;
                more = 0;
            } else {
                d = (u8)(v & 0xF);
                v >>= 4;
            }
        }
        r = (u8)(p[si] + d + (u8)carry);
        carry = r > 9;
        if (carry)
            r -= 10;
        p[si] = r;
    }
}

/* @041D:037C file:05D4C bcd_sub — 1, если результат нулевой */
s16 bcd_sub(u8 *p, s16 n, s32 v)
{
    s16 si, more = 1, borrow = 0, any = 0, di;
    u8 d = 0;
    for (si = 0; si < n; si++) {
        if (more) {
            if (v == 0) {
                d = 0;
                more = 0;
            } else {
                d = (u8)(v & 0xF);
                v >>= 4;
            }
        }
        di = (s16)p[si] - d - borrow;
        borrow = di < 0;
        if (borrow)
            di += 10;
        p[si] = (u8)di;
        any |= (u8)di;
    }
    return any == 0;
}

/* @041D:041A file:05DEA hud_digits — без ведущих нулей */
static void hud_digits(u8 *p, s16 n, s16 col, s16 row)
{
    s16 lead = 0, si;
    for (si = n - 1; si >= 0; si--, col += 2) {
        lead |= p[si] != 0 || si == 0;
        hud_put(p[si] + 0x3E, col, row, lead);
    }
}

/* @041D:046E file:05E3E hud_blank_digits */
static void hud_blank_digits(s16 n, s16 col, s16 row)
{
    s16 si;
    for (si = 0; si < n; si++, col += 2)
        hud_put(0x3E, col, row, 0);
}

/* @041D:0498 file:05E68 hud_score */
void hud_score(u8 *score)
{
    if (demo_mode != 0 || score_dirty == 0)
        return;
    hud_digits(score, 8, (cur_player != 0 ? 0x37 : 0) + 8, 0xB8);
    score_dirty = 0;
}

/* @041D:04D7 file:05EA7 add_score — BCD-long (0x2600 = 2600 очков) */
void add_score(s32 v)
{
    bcd_add(score_bcd, 8, v);
    score_dirty = 1;
}

/* @041D:04F7 file:05EC7 hud_redraw — весь HUD (очки обоих игроков) */
void hud_redraw(void)
{
    s16 si = cur_player;
    if (demo_mode == 0) {
        for (cur_player = 0; two_players + 1 > cur_player; cur_player++) {
            hud_lives();
            score_dirty = 1;
            hud_score(&player_rec[cur_player * 0xA5 + 0x86]);
        }
        hud_player_tags_clear();
        hud_sel_clear();
        hud_sel(item_sel, 1);
        hud_blank_digits(2, 0xC, 0x8E);
        hud_put(0x30, 0x3E, 0x96, 1);
        hud_digits(k_digits, 2, 0x42, 0x96);
        hud_put(0x3D, 0x32, 0x96, 1);
        hud_digits(time_digits, 3, 0x36, 0x96);
    }
    hud_items();
    cur_player = si;
}

/* @041D:05D2 file:05FA2 hud_options — значки настроек (и в демо) */
void hud_options(void)
{
    s16 saved = demo_mode;
    demo_mode = 0;
    if (level_loading == 0) {
        hud_put(two_players + 0x38, 0x20, 0xB0, 1);
        hud_put(opt_joystick + 0x36, 0x24, 0xB0, 1);
        hud_put(opt_music != 0 ? 0x3A : 0x3C, 0x28, 0xB0, 1);
        hud_put(opt_sfx != 0 ? 0x3B : 0x3C, 0x2C, 0xB0, 1);
    }
    demo_mode = saved;
}

/* @041D:0671 file:06041 item_nop */
static void item_nop(void)
{
}

/* @041D:0676 file:06046 hud_ammo */
static void hud_ammo(void)
{
    if (ammo_dirty != 0) {
        if (ammo_digits[0] != 0 || ammo_digits[1] != 0)
            hud_digits(ammo_digits, 2, 0xC, 0x8E);
        else
            hud_blank_digits(2, 0xC, 0x8E);
    }
    ammo_dirty = 0;
}

/* @041D:06C3 file:06093 hud_kcount */
static void hud_kcount(void)
{
    if (k_dirty != 0)
        hud_digits(k_digits, 2, 0x42, 0x96);
    k_dirty = 0;
}

/* @041D:06ED file:060BD level_timer_tick — раз в 16 кадров; на нуле —
 * гибель; меньше 30 — музыка спешки и мигание значка */
static void level_timer_tick(void)
{
    if (frame_no & 0xF)
        return;
    if (player_state == 6)
        return;
    if (player_state != 9 && bcd_sub(time_digits, 3, 1))
        die_hit();
    hud_digits(time_digits, 3, 0x36, 0x96);
    if (hurry != 0) {
        hud_put(0x3D, 0x32, 0x96, (frame_no & 0x10) != 0);
    } else if (time_digits[2] == 0 && time_digits[1] < 3) {
        hurry = 1;
        play_music(MUSIC(0x0663), 0, 1);
    }
}

/* Функции применения предметов (таблица item_use_fn, DS:0x0C30):
 * 1 — предмет израсходован. */

/* @041D:079A file:0616A use_item0 — объект с обработчиком 0FFA:07CC */
s16 use_item0(void)
{
    ActiveObject *o;
    alloc_idx = alloc_object(1);
    o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
    if (o != NULL)
        queue_state(o, (farfn)ladder_item);
    return 0;
}

/* @041D:07F2 file:061C2 use_item1 */
s16 use_item1(void)
{
    if (player_state == 3)
        return 0;
    if (current_level == 9) {
        play_sound(9);
        return 0;
    }
    plr_D8B4 = 0xFA;
    return 1;
}

/* @041D:081D file:061ED use_item2 — объект с обработчиком 0FFA:0BCB */
s16 use_item2(void)
{
    ActiveObject *o;
    alloc_idx = alloc_object(0);
    o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
    if (o == NULL)
        return 0;
    queue_state(o, (farfn)bomb);
    return 1;
}

/* @041D:0879 file:06249 use_item3 — +30 патронов, оружие 1 */
s16 use_item3(void)
{
    bcd_add(ammo_digits, 2, 0x30);
    ammo_dirty = 1;
    weapon = 1;
    return 1;
}

/* @041D:08A4 file:06274 use_item4 — оружие 2 */
s16 use_item4(void)
{
    if (player_state == 3)
        return 0;
    weapon = 2;
    return 1;
}

/* @041D:08BD file:0628D use_item5 */
s16 use_item5(void)
{
    plr_D8B8 = 0x96;
    return 1;
}

/* @041D:08CB file:0629B use_item6 — полёт (k_fly) */
s16 use_item6(void)
{
    plr_D8B6 = 0xC8;
    queue_state(player_ptr, (farfn)k_fly);
    return 1;
}

/* @041D:08F1 file:062C1 use_item7 */
s16 use_item7(void)
{
    return 1;
}

/* @041D:08F9 file:062C9 use_item8 — только в состоянии игрока 3 */
s16 use_item8(void)
{
    if (player_state != 3)
        return 0;
    plr_D8BA = 1;
    return 1;
}

/* @041D:090F file:062DF item_usable */
static s16 item_usable(s16 n)
{
    /* item_next может спросить n = -1: в оригинале это слово перед
     * item_count, то есть plr_D8FC[10] */
    s16 cnt = n == -1 ? plr_D8FC[10] : item_count[n];
    return cnt > 0 && (n < 5 || item_active[n - 5] != 0);
}

/* @041D:093C file:0630C item_next — следующий применимый по кругу */
static s16 item_next(s16 dir)
{
    s16 step = dir != 0 ? 1 : -1, cur, si, di;
    if (items_owned != 0) {
        cur = step;
        for (si = 0; si < 0xB; si++, cur += step) {
            di = (s16)((cur + item_sel + 0xB) % 0xB);
            if (item_usable(di))
                return di;
        }
    }
    return -1;
}

/* @041D:0999 file:06369 item_select */
static void item_select(s16 n)
{
    hud_sel(item_sel, 0);
    hud_sel(n, 1);
    item_sel = n;
}

/* @041D:09BE file:0638E item_pickup — 9 — буква «K», 10 — огневая мощь */
void item_pickup(s16 n)
{
    s16 got = 0;
    if (item_count[n] == 0)
        items_owned++;
    switch (n) {
    case 0: case 1: case 4: case 5: case 6: case 7: case 8:
        item_count[n] = 1;
        got = 1;
        break;
    case 2:
        item_count[n]++;
        item_nop();
        got = 1;
        break;
    case 3:
        item_count[n]++;
        got = 1;
        break;
    case 9:
        bcd_add(k_digits, 2, 1);
        if (k_digits[0] == 0 && k_digits[1] == 3) {
            lives++;
            hud_lives();
            play_sound(6);
            bcd_set(k_digits, 2, 0);
        }
        k_dirty = 1;
        break;
    case 10:
        if (firepower < 3)
            firepower++;
        break;
    }
    if (got) {
        hud_item(n, 1);
        if (n < 5)
            item_select(n);
    }
}

/* @041D:0AB4 file:06484 item_consume */
void item_consume(s16 n)
{
    item_select(item_next(inp_dir & 1));
    if (--item_count[n] == 0) {
        items_owned--;
        hud_item(n, 0);
    }
    if (items_owned == 0)
        item_select(-1);
    if (n >= 5)
        item_active[n - 5] = 0;
}

/* @041D:0B13 file:064E3 item_use */
static void item_use(s16 n)
{
    if (n < 0)
        return;
    if (((s16 (*)(void))item_use_fn[n])() == 0)
        return;
    item_consume(n);
    if (n >= 5)
        item_set_active(n + 7, 0);
}

/* @041D:0B4F file:0651F item_set_active — k = предмет + 7 */
void item_set_active(s16 k, s16 on)
{
    s16 n = k - 7;
    item_active[k - 0xC] = on;
    if (on != 0) {
        if (item_count[n] != 0)
            item_select(n);
        return;
    }
    if (item_count[n] != 0)
        hud_item(n, 1);
    if (item_sel == n)
        item_select(item_next(inp_dir & 1));
}

/* @041D:0BC1 file:06591 items_tick — выбор (влево/вправо), применение
 * (OPTION), мигание активных, метка игрока, счётчики, таймер */
void items_tick(void)
{
    s16 si;
    if ((u8)(~inp_prev_mask & inp_mask) & 3 || !item_usable(item_sel))
        item_select(item_next(inp_mask & 1));
    if (inp_option & ~inp_prev_option)
        item_use(item_sel);
    si = frame_no & 3;
    if (item_active[si] != 0 && item_count[si + 5] != 0 && (frame_no & 0xC) == 0)
        hud_item(si + 5, frame_no & 0x10);
    if (two_players != 0 && (frame_no & 0x1F) == 0)
        hud_player_tag(cur_player, frame_no & 0x20);
    hud_ammo();
    hud_kcount();
    level_timer_tick();
}
