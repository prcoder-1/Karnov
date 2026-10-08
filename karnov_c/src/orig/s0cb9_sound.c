/* Модуль 0x0CB9 (file 0xE390) — звук на PC-динамике.
 *
 * Два канала на одном динамике: музыка (A) и эффекты (B). ISR таймера
 * (PIT ch0, делитель 0x1530 ≈ 220 Гц) на нечётных тиках ведёт музыку,
 * на чётных — эффект; каждый перепрограммирует делитель ch2.
 *
 * Скрипт музыки: байт ноты (индекс pitch_lut) + индекс пары длительностей
 * в note_len; FF i a b — записать пару note_len[i] = (a, b); 00 — конец
 * (с повтором, если mus_loop). Скрипт эффекта — опкоды 0..4 (см. sfx_tick). */
#include "orig/fn.h"
#include "orig/hw.h"

u8 snd_stop_marker;

static void speaker_off(void)
{
    hw_out(0x61, hw_in(0x61) & 0xFC);
}

/* @0CB9:000A file:0E39A sound_shutdown */
void sound_shutdown(void)
{
    hw_out(0x43, 0x36);
    hw_out(0x40, 0);
    hw_out(0x40, 0);
    set_vector(8, old_int8);
    speaker_off();
}

/* @0CB9:0033 file:0E3C3 get_music_enabled */
s16 get_music_enabled(void)
{
    return music_enabled;
}

/* @0CB9:0037 file:0E3C7 music_done */
s16 music_done(void)
{
    return mus_script == NULL ? 1 : 0;
}

/* @0CB9:0048 file:0E3D8 get_sfx_enabled */
s16 get_sfx_enabled(void)
{
    return sfx_enabled;
}

/* @0CB9:004C file:0E3DC sound_init */
void sound_init(void)
{
    old_int8 = get_vector(8);
    set_vector(8, (farfn)timer_isr);
    hw_out(0x43, 0x36);
    hw_out(0x40, 0x30);
    hw_out(0x40, 0x15);
}

/* @0CB9:007D file:0E40D play_music — SND_STOP вместо скрипта глушит канал */
void play_music(const u8 *script, u16 prio, s16 loop)
{
    if (script == SND_STOP) {
        speaker_off();
        mus_prio = 0;
        prio = 0;
        script = NULL;
    }
    if (prio < mus_prio && mus_script != NULL)
        return;
    mus_prio = prio;
    mus_pos = 0;
    mus_script = (u8 *)script;
    mus_loop = loop;
    mus_phase = 1;
    mus_timer = 1;
}

/* @0CB9:00DD file:0E46D play_sfx */
void play_sfx(const u8 *script, u16 prio)
{
    if (script == SND_STOP) {
        sfx_prio = 0;
        prio = 0;
        script = NULL;
        speaker_off();
    }
    if (prio < sfx_prio && sfx_script != NULL)
        return;
    sfx_prio = prio;
    sfx_pos = 0;
    sfx_script = (u8 *)script;
    sfx_repeat = 0;
    sfx_repeat_set = 0;
    sfx_timer = 1;
}

/* @0CB9:013C file:0E4CC set_music_enabled */
void set_music_enabled(s16 on)
{
    if (music_enabled && on == 0 && mus_script != NULL)
        speaker_off();
    music_enabled = on;
}

/* @0CB9:0165 file:0E4F5 set_sfx_enabled — включение обрывает текущий эффект */
void set_sfx_enabled(s16 on)
{
    sfx_enabled = on;
    if (on && sfx_script != NULL) {
        speaker_off();
        sfx_script = NULL;
    }
}

/* @0CB9:0191 file:0E521 mus_rest */
static void mus_rest(void)
{
    mus_div = 0;
    mus_timer = mus_gap;
}

/* @0CB9:019E file:0E52E mus_next */
static void mus_next(void)
{
    u16 e;
    if (mus_script[mus_pos] == 0) {
        if (mus_loop == 0) {
            mus_script = NULL;
            mus_pos = 0;
            mus_div = 0;
            return;
        }
        mus_pos = 0;
    }
    if (mus_script[mus_pos] == 0xFF) {
        mus_pos++;
        /* индекс до 255: за note_len (0x40) — соседние переменные DS */
        e = (u16)(0x122A + mus_script[mus_pos] * 2);
        mus_pos++;
        far_pokeb(DS_SEG, e, mus_script[mus_pos]);
        mus_pos++;
        far_pokeb(DS_SEG, (u16)(e + 1), mus_script[mus_pos]);
        mus_pos++;
        mus_next();
        return;
    }
    mus_phase = 2;
    mus_div = (u16)DS_IDX16(pitch_lut, 0x126A, mus_script[mus_pos]);
    mus_pos++;
    e = (u16)(0x122A + mus_script[mus_pos] * 2);
    mus_pos++;
    mus_timer = far_peekb(DS_SEG, e);
    mus_gap = far_peekb(DS_SEG, (u16)(e + 1));
}

/* @0CB9:0298 file:0E628 music_tick */
static void music_tick(void)
{
    if (mus_script == NULL)
        return;
    if (--mus_timer != 0)
        return;
    if (--mus_phase != 0 && mus_gap != 0)
        mus_rest();
    else
        mus_next();
}

/* @0CB9:02C9 file:0E659 sfx_tick
 * 0 — конец (или повтор с начала), 1 lo hi — делитель, 2 n — число
 * повторов (один раз), 3 lo hi — добавка к делителю, 4 n — ждать n тиков.
 * Неизвестный опкод зацикливает разбор (как в оригинале). */
static void sfx_tick(void)
{
    u16 si;
    if (sfx_enabled == 0 || sfx_script == NULL)
        return;
    if (--sfx_timer != 0)
        return;
    sfx_delta = 0;
    sfx_timer = 1;
    si = sfx_pos;
    for (;;) {
        switch (sfx_script[si]) {
        case 0:
            if (--sfx_repeat != 0) {
                si = 0;
                break;
            }
            sfx_delta = 0;
            sfx_pos = 0;
            sfx_script = NULL;
            sfx_prio = 0;
            speaker_off();
            return;
        case 1:
            si++;
            sfx_div = sfx_script[si];
            si++;
            sfx_div += sfx_script[si] << 8;
            si++;
            break;
        case 2:
            si++;
            if (sfx_repeat_set == 0) {
                sfx_repeat = sfx_script[si];
                sfx_repeat_set = 1;
            }
            si++;
            break;
        case 3:
            si++;
            sfx_delta = sfx_script[si];
            si++;
            sfx_delta += sfx_script[si] << 8;
            si++;
            break;
        case 4:
            si++;
            sfx_timer = sfx_script[si];
            si++;
            sfx_pos = si;
            if (sfx_repeat == 0)
                sfx_repeat = 1;
            return;
        default:
            break;
        }
    }
}

static void spk_program(u16 div)
{
    spk_div = div;
    hw_out(0x43, 0xB6);
    hw_out(0x42, (u8)div);
    hw_out(0x42, (u8)(div >> 8));
}

/* @0CB9:03CC file:0E75C timer_isr — INT 08 */
void timer_isr(void)
{
    u8 ph = tick_phase++;
    if (ph & 1) {
        music_tick();
        if (music_enabled != 0 && mus_script != NULL) {
            if (mus_div != 0) {
                if (mus_div != spk_div)
                    spk_program(mus_div);
                hw_out(0x61, hw_in(0x61) | 3);
            } else {
                speaker_off();
            }
        }
    } else if (sfx_enabled != 0 && sfx_script != NULL) {
        sfx_tick();
        sfx_div += sfx_delta;
        if (sfx_div != spk_div) {
            spk_program(sfx_div);
            hw_out(0x61, hw_in(0x61) | 3);
        }
    }
    timer_ticks++;
    if ((s8)++bios_tick_div >= 12) {
        bios_tick_div = 0;
        old_int8();
    } else {
        hw_out(0x20, 0x20);
    }
}

/* @0CB9:04B2 file:0E842 get_vector */
farfn get_vector(s16 n)
{
    return hw_getvect((u8)n);
}

/* @0CB9:04D8 file:0E868 set_vector */
void set_vector(s16 n, farfn f)
{
    hw_setvect((u8)n, f);
}
