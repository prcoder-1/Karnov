/* todo.c — заглушки непереведённых функций (см. todo.h). */
#include "orig/todo.h"
#include "orig/port.h"

void herc_init(void)
{
    port_todo("02C7:000E herc_init");
}

void herc_shutdown(void)
{
    port_todo("02C7:0052 herc_shutdown");
}

void kill_sky_chain(void)
{
    port_todo("0FFA:155E kill_sky_chain");
}

void tandy_convert(u8 *dst, const u8 *src)
{
    port_todo("0D18:000D tandy_convert");
}

void sprite_blit(u16 off, s16 sprite, s16 phase, s16 mode) { port_todo_once("0920:0002 sprite_blit"); }
void sprite_erase(u16 off, s16 sprite, s16 phase) { port_todo_once("0920:13FE sprite_erase"); }
void vb_present_rect(u16 off, s16 w, s16 h) { port_todo_once("0920:2392 vb_present_rect"); }

u16 tandy_sprite_convert(u8 *dst, const u8 *src)
{
    port_todo("0D18:009D tandy_sprite_convert");
    return 0;
}

void level_enter(void)
{
    port_todo("0E62:01F9 level_enter");
}

void show_hiscores(s16 ticks)
{
    port_todo("02D3:00FB show_hiscores");
}

void hiscore_enter(u8 *score)
{
    port_todo("02D3:000C hiscore_enter");
}

void PRE_KARNOV(void)
{
    port_todo("0FFA:1A1A PRE_KARNOV");
}

void POST_KARNOV(void)
{
    port_todo("0FFA:1C04 POST_KARNOV");
}

void scroll_blit(u16 off) { port_todo_once("05A3:0009 scroll_blit"); }

void die_hit(void)
{
    port_todo("0E62:06FE die_hit");
}

void h_0FFA_07CC(void)
{
    port_todo("0FFA:07CC h_0FFA_07CC");
}

void h_0FFA_0BCB(void)
{
    port_todo("0FFA:0BCB h_0FFA_0BCB");
}

void h_0E62_13C6(void)
{
    port_todo("0E62:13C6 h_0E62_13C6");
}
void hud_blit(u16 off, s16 sprite, s16 phase) { port_todo_once("0920:24C8 hud_blit"); }
void hud_erase(u16 off, s16 sprite, s16 phase) { port_todo_once("0920:2A56 hud_erase"); }

void set_player_anim(s16 a, s16 b)
{
    port_todo("0E62:0683 set_player_anim");
}
