/* Модуль 0x054B (file 0x6CB0) — демо LEVELn.MOV: проигрывание и запись.
 * Событие — маска ввода и номер кадра (frame_no), с которого она действует. */
#include <stdio.h>
#include "orig/fn.h"
#include "orig/todo.h"

static u16 ev_frame(const Demo *d, s16 i)
{
    return (u16)(d->ev[i * 3 + 1] | d->ev[i * 3 + 2] << 8);
}

/* @054B:0009 file:06CB9 demo_at_end */
static s16 demo_at_end(const Demo *d)
{
    return d == NULL || d->pos == d->count;
}

/* @054B:0029 file:06CD9 demo_rec_full — кольцо на 256 событий */
static s16 demo_rec_full(const Demo *d)
{
    return d == NULL || d->pos == ((d->count + 1) & 0xFF);
}

/* @054B:0050 file:06D00 demo_reset */
void demo_reset(Demo *d)
{
    d->pos = 0;
    d->count = 0;
}

/* @054B:0063 file:06D13 demo_rec_next */
static void demo_rec_next(Demo *d)
{
    d->count = (d->count + 1) & 0xFF;
}

/* @054B:0077 file:06D27 demo_advance */
static void demo_advance(Demo *d)
{
    d->pos = (d->pos + 1) & 0xFF;
}

/* @054B:0089 file:06D39 demo_copy */
void demo_copy(const Demo *src, Demo *dst)
{
    rtl_fmemcpy(dst, src, 0x304);
    dst->pos = 0;
}

/* @054B:00AD file:06D5D demo_record_event — 1, если кольцо заполнено */
s16 demo_record_event(Demo *d, s16 mask, s16 frame)
{
    if (demo_rec_full(d))
        return 1;
    d->ev[d->count * 3] = (u8)mask;
    d->ev[d->count * 3 + 1] = (u8)frame;
    d->ev[d->count * 3 + 2] = (u8)(frame >> 8);
    demo_rec_next(d);
    return 0;
}

/* @054B:0106 file:06DB6 demo_play — подставить маску события текущего
 * кадра; по концу демо 1 (кроме состояния игрока 9) */
s16 demo_play(Demo *d)
{
    if (demo_at_end(d))
        return player_state == 9 ? 0 : 1;
    if ((s16)ev_frame(d, d->pos) <= frame_no) {
        inp_mask = d->ev[d->pos * 3];
        demo_advance(d);
    }
    return 0;
}

/* @054B:0166 file:06E16 load_demo — levelN.mov текущего уровня. События
 * кладутся с нуля, а конец — номер последнего из файла. */
s16 load_demo(void)
{
    FILE *fp = NULL;
    s16 first, last, si;
    u8 *ev;
    demo_ptr->pos = 0;
    demo_ptr->count = 0;
    sprintf(msg_buf, s_mov_fmt_a, current_level);
    while (fp == NULL) {
        fp = rtl_fopen(msg_buf, s_rb_mov_a);
        if (fp != NULL)
            break;
        clear_screen();
        draw_text(s_insert_disk, 10);
        draw_text(s_any_key, 12);
        while (read_key() == 0)
            ;
        clear_screen();
        clear_arg(0);
    }
    fclose(fp);
    fp = rtl_fopen(msg_buf, s_rb_mov_b);
    if (fp == NULL)
        return 1;
    if (fread(&first, 2, 1, fp) != 1 || fread(&last, 2, 1, fp) != 1 || first >= last)
        goto fail;
    for (si = 0, ev = &demo_ptr->ev[0]; last - first > si; si++, ev += 3)
        if (fread(ev, 1, 1, fp) != 1 || fread(ev + 1, 2, 1, fp) != 1)
            goto fail;
    demo_ptr->pos = 0;
    demo_ptr->count = last;
    fclose(fp);
    return 0;
fail:
    fclose(fp);
    return 1;
}

/* @054B:031C file:06FCC save_demo — записанное демо в levelN.mov */
s16 save_demo(void)
{
    FILE *fp;
    s16 pos = demo_rec_ptr->pos, count = demo_rec_ptr->count;
    sprintf(msg_buf, s_mov_fmt_b, current_level);
    fp = rtl_fopen(msg_buf, s_wb_mov);
    if (fp == NULL)
        return 1;
    if (pos >= count || fwrite(&pos, 2, 1, fp) != 1 || fwrite(&count, 2, 1, fp) != 1 ||
        fwrite(&demo_rec_ptr->ev[0], 3, (size_t)(count - pos), fp) != (size_t)(count - pos)) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}
