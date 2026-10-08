/* Модуль 0x02D3 (file 0x4530) — таблица рекордов: 10 записей по 0x1F
 * байт (имя до 26 знаков + 0, u32 очки в BCD по смещению 0x1B). */
#include <string.h>
#include "orig/fn.h"
#include "orig/hw.h"
#include "orig/todo.h"

#define HS_SIZE 0x1F

static u32 hs_score(s16 i)
{
    const u8 *p = &hiscores[i * HS_SIZE + 0x1B];
    return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void hs_set_score(s16 i, u32 v)
{
    u8 *p = &hiscores[i * HS_SIZE + 0x1B];
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* @02D3:0118 file:04648 hiscore_draw */
static void hiscore_draw(void)
{
    s16 si, dm;
    u8 digits[8];
    video_reset();
    dm = demo_mode;
    demo_mode = 0;
    bios_set_cursor(1, 0x14 - (s16)strlen(s_high_scores) / 2);
    con_puts(s_high_scores);
    for (si = 0; si < 0xA; si++) {
        bcd_set(digits, 8, (s32)hs_score(si));
        hud_digits(digits, 8, 2, (si * 2 + 4) * 8);
        bios_set_cursor(si * 2 + 4, 0xC);
        con_puts((const char *)&hiscores[si * HS_SIZE]);
    }
    demo_mode = dm;
}

/* @02D3:01EA file:0471A hiscore_name — ввод имени в строку n */
static void hiscore_name(s16 n)
{
    s16 done = 0, si = 0, sfx, dm, key;
    char ch[2] = { 0, 0 };
    sfx = get_sfx_enabled();
    dm = demo_mode;
    demo_mode = 0;
    set_sfx_enabled(1);
    hw_pokeb(0, 0x417, 0);
    play_music(SND_STOP, 0x1000, 0);
    delay(0x64);
    play_music(MUSIC(0x08CC), 0x1000, 0);
    while (done == 0) {
        if (si < 0x1A) {
            hud_put(0x3E, (si + 0xC) * 2, (n * 2 + 4) * 8, 0);
            bios_set_cursor(n * 2 + 4, si + 0xC);
            ch[0] = '_';
            con_puts(ch);
        }
        while (rtl_bioskey(1) == 0)
            ;
        key = rtl_bioskey(0) & 0xFF;
        switch (key) {
        case 0xD:
            done = 1;
            continue;
        case 8:
            if (si == 0)
                break;
            ch[0] = (char)key;
            con_puts(ch);
            si--;
            hiscores[n * HS_SIZE + si] = 0;
            hud_put(0x3E, (si + 0xC) * 2, (n * 2 + 4) * 8, 0);
            hud_put(0x3E, (si + 0xD) * 2, (n * 2 + 4) * 8, 0);
            continue;
        default:
            if (key < 0x20 || si >= 0x1A)
                break;
            hud_put(0x3E, (si + 0xC) * 2, (n * 2 + 4) * 8, 0);
            bios_set_cursor(n * 2 + 4, si + 0xC);
            ch[0] = (char)key;
            con_puts(ch);
            hiscores[n * HS_SIZE + si] = (u8)key;
            si++;
            hiscores[n * HS_SIZE + si] = 0;
            continue;
        }
        play_sfx(&sfx_data[0xC26 - 0xB17], 0x1000);
    }
    demo_mode = dm;
    play_music(SND_STOP, 0x1000, 0);
    set_sfx_enabled(sfx);
}

/* @02D3:000C file:0453C hiscore_enter — вставка очков (8 цифр BCD,
 * младшая первой) в таблицу; ввод имени */
void hiscore_enter(u8 *score)
{
    u32 v = 0;
    s16 si, di;
    for (si = 7; si >= 0; si--)
        v = v << 4 | score[si];
    for (si = 0; si < 0xA; si++) {
        if (hs_score(si) >= v)
            continue;
        for (di = 8; di >= si; di--)
            rtl_fmemcpy(&hiscores[(di + 1) * HS_SIZE], &hiscores[di * HS_SIZE], HS_SIZE);
        hs_set_score(si, v);
        hiscores[si * HS_SIZE] = 0;
        hiscore_draw();
        hiscore_name(si);
        video_reset();
        si = 0xA;
    }
}

/* @02D3:00FB file:0462B show_hiscores — таблица на ticks тиков */
void show_hiscores(s16 ticks)
{
    hiscore_draw();
    delay(ticks);
    video_reset();
}
