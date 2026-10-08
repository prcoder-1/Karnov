/* Модуль 0x0330 (file 0x4B00) — main, инициализация и завершение,
 * консольный вывод через BIOS. */
#include "orig/fn.h"
#include "orig/hw.h"
#include "orig/todo.h"

/* @0330:0009 file:04B09 bios_set_cursor — INT 10h AH=2 */
void bios_set_cursor(s16 row, s16 col)
{
    hw_int10_set_cursor((u8)row, (u8)col);
}

/* @0330:001A file:04B1A bios_set_mode — INT 10h AH=0 */
void bios_set_mode(s16 mode)
{
    hw_int10_set_mode((u8)mode);
}

/* @0330:0026 file:04B26 bios_get_mode — INT 10h AH=0Fh.
 * Во все четыре выхода пишется AL (так скомпилирован оригинал). */
void bios_get_mode(s16 *mode, s16 *cols, s16 *page, s16 *unused)
{
    u8 al = hw_int10_get_mode();
    *mode = al;
    *cols = al;
    *page = al;
    *unused = al;
}

/* @0330:0057 file:04B57 con_putc — телетайп; в EGA символ сначала
 * печатается на 25 строк ниже (вторая страница), затем на месте. */
void con_putc(s16 ch)
{
    if (level_mode == 1) {
        u16 pos = hw_int10_get_cursor();
        hw_int10_set_cursor((u8)((pos >> 8) + 0x19), (u8)pos);
        hw_int10_teletype((u8)ch);
        hw_int10_set_cursor((u8)(pos >> 8), (u8)pos);
    }
    hw_int10_teletype((u8)ch);
}

/* @0330:0097 file:04B97 con_puts — в режиме Hercules копирует страницу
 * B800:2000 → B800:4000 */
void con_puts(const char *s)
{
    for (; *s; s++)
        con_putc((s16)*s);
    if (level_mode == 2)
        hw_vmem_copy(0xB800, 0x4000, 0xB800, 0x2000, 0x2000);  /* fmemcpy @1C92:000E */
}

/* @0330:00D1 file:04BD1 sys_init */
void sys_init(void)
{
    bios_equip_saved = (s8)hw_peekb(0, 0x410);
    bios_kbflags_saved = (s8)hw_peekb(0, 0x417);
    cpu_type = detect_cpu();
    machine_kind = detect_machine();
    sound_init();
    if (bk_init())
        fatal(3, s_bkinit_failed);
    if (level_init())
        fatal(3, s_level_init_failed);
    objects_init();
    if (sprite_init())
        fatal(3, s_sprite_init_failed);
}

/* @0330:015A file:04C5A sys_shutdown */
void sys_shutdown(void)
{
    hw_pokeb(0, 0x417, (u8)bios_kbflags_saved);
    level_shutdown();
    sprite_shutdown();
    video_shutdown();
    sound_shutdown();
    if (level_mode == 2) {
        herc_shutdown();
        bios_set_mode(7);
    }
    bios_set_mode(saved_video_mode);
    hw_pokeb(0, 0x410, (u8)bios_equip_saved);
}

/* @0330:01AD file:04CAD fatal — сообщение не печатается */
void fatal(s16 code, const char *msg)
{
    (void)msg;
    sys_shutdown();
    hw_exit(code);
}

/* @0330:01C2 file:04CC2 game_main — по Shift/Ctrl/Alt при старте
 * меню выбора видеорежима; без него режим из bk_init. */
s16 game_main(void)
{
    s16 again;
    bios_get_mode(&saved_video_mode, &again, &again, &again);
    if (hw_peekb(0, 0x417) & 0x0F) {
        again = 1;
        while (again) {
            con_puts(s_press_key_video);
            con_puts(s_menu_cga);
            con_puts(s_menu_ega);
            con_puts(s_menu_tga);
            con_puts(s_menu_mono);
            switch (get_key()) {
            case 0x1B:
                return 0;
            case 'C': case 'c':
                level_mode = 0;
                bios_set_mode(4);
                again = 0;
                break;
            case 'E': case 'e':
                level_mode = 1;
                bios_set_mode(0x0D);
                again = 0;
                break;
            case 'T': case 't':
                level_mode = 3;
                bios_set_mode(9);
                again = 0;
                break;
            case 'M': case 'm':
                if (herc_detect() == 0) {
                    herc_init();
                    bios_set_mode(4);
                    level_mode = 2;
                    again = 0;
                } else {
                    con_puts(s_cant_force_herc);
                }
                break;
            }
        }
    }
    sys_init();
    game_run();
    sys_shutdown();
    rtl_exit(0);
    return 0;
}

/* @0330:030B file:04E0B clear_arg — обнуляет свой аргумент (без эффекта) */
void clear_arg(s16 a)
{
    a = 0;
    (void)a;
}
