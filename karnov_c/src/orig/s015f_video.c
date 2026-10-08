/* Модуль 0x015F (file 0x2DF0) — видеорежим, внеэкранный буфер, скроллинг.
 *
 * level_mode: 0 CGA, 1 EGA, 2 Hercules, 3 Tandy, 4 — ещё не определён.
 * Внеэкранный буфер (vbuf) хранится в формате оригинала: таблицы
 * смещений vb_* строятся здесь, их значения передаются asm-драйверам. */
#include "orig/fn.h"
#include "orig/hw.h"
#include "orig/todo.h"

/* @015F:0494 file:03284 detect_video_mode — ждёт диск с sprites.ega,
 * затем (если режим не выбран меню) Tandy → EGA → CGA → Hercules */
void detect_video_mode(void)
{
    FILE *fp = NULL;
    s16 equip, i;
    while (fp == NULL) {
        fp = rtl_fopen(s_sprites_ega_a, s_rb_a);
        if (fp != NULL)
            break;
        for (i = 0; i < 0xF; i++)
            con_puts(s_crlf_a);
        con_puts(s_insert_disk);
        con_puts(s_crlf_b);
        con_puts(s_any_key);
        for (i = 0; i < 0xC; i++)
            con_puts(s_crlf_c);
        while (read_key() == 0)
            ;
        clear_arg(0);
    }
    fclose(fp);
    equip = (s8)hw_peekb(0, 0x410);
    if (level_mode != 4)
        return;
    if (machine_kind >= 5 && machine_kind <= 10) {
        fp = rtl_fopen(s_sprites_ega_b, s_rb_b);
        if (fp == NULL)
            goto cga;
        fclose(fp);
        bios_set_mode(9);
        level_mode = 3;
        return;
    }
    if (ega_detect() == 0) {
        fp = rtl_fopen(s_sprites_ega_c, s_rb_c);
        if (fp != NULL) {
            fclose(fp);
            bios_set_mode(0x0D);
            level_mode = 1;
            return;
        }
        hw_pokeb(0, 0x410, (u8)equip);
        goto cga;
    }
    if (cga_detect() == 0) {
cga:
        bios_set_mode(4);
        level_mode = 0;
        return;
    }
    if (herc_detect() == 0) {
        herc_init();
        bios_set_mode(4);
        level_mode = 2;
    }
}

/* @015F:0646 file:03436 bk_init — буфер, карта и таблицы смещений */
s16 bk_init(void)
{
    s32 bufsize;
    s16 i;
    u16 step, acc;
    detect_video_mode();
    if (level_mode == 4) {
        con_puts(s_video_dev_err);
        goto fail;
    }
    if (level_mode == 0 || level_mode == 2) {
        vb_row_bytes = 0x90;
        bufsize = 0x1000F;
        tile_shift = 1;
        vid_AF1E = 1;
    } else if (level_mode == 3) {
        vb_row_bytes = 0x120;
        bufsize = 0x2000F;
        tile_shift = 0;
        vid_AF1E = 0;
    } else {
        vb_row_bytes = 0x48;
        bufsize = 0x2000F;
        tile_shift = 2;
        vid_AF1E = 3;
    }
    vbuf_alloc = rtl_farmalloc((u32)bufsize);
    if (vbuf_alloc == NULL) {
        con_puts(s_no_mem_buffer);
        goto fail;
    }
    vbuf = hw_next_paragraph(vbuf_alloc);
    map_buf = rtl_farmalloc(0x7992);
    if (map_buf == NULL) {
        con_puts(s_no_mem_map);
        goto fail;
    }
    regions = NULL;
    spawns = NULL;

    step = level_mode == 1 ? 2 : level_mode == 3 ? 8 : 4;
    for (acc = 0, i = 0; i < 0x113; i++, acc += step)
        vb_col_off[i] = acc;
    step = level_mode == 1 ? 0x2D0 : level_mode == 3 ? 0xB40 : 0x5A0;
    for (acc = 0, i = 0; i < 0x73; i++, acc += step)
        vb_trow_off[i] = acc;
    step = level_mode == 1 ? 0x48 : level_mode == 3 ? 0x120 : 0x90;
    for (acc = 0, i = 0; i < 0x47E; i++, acc += step)
        vb_row_off[i] = acc;
    step = level_mode == 1 ? 2 : level_mode == 3 ? 8 : 4;
    for (i = 0; i < 0x898; i++)
        px_to_col[i] = i / step;
    for (i = 0; i < 0x47E; i++)
        px_to_trow[i] = i / 10;

    switch (level_mode) {
    case 0: case 2:
        vb_view_bytes = 0x14; vb_stride = 0x24;
        vid_B82E = 0x1E; vid_B826 = 0x1E; vid_AF20 = 0x88;
        vid_B832 = 0xB4;
        break;
    case 3:
        vb_view_bytes = 0x28; vb_stride = 0x48;
        vid_B82E = 0x1E; vid_B826 = 0x1E; vid_AF20 = 0x110;
        vid_B832 = 0xB4;
        break;
    case 1:
        vb_view_bytes = 0xA; vb_stride = 0x12;
        vid_B82E = 0x1E; vid_B826 = 0x1E; vid_AF20 = 0x44;
        vid_B832 = 0xB4;
        break;
    }
    tile_attr[0] = 2;
    loaded_level = 0x3E7;
    return 0;
fail:
    delay(0x370);
    return 1;
}

/* @015F:0922 file:03712 video_shutdown */
void video_shutdown(void)
{
    rtl_farfree(vbuf_alloc);
    rtl_farfree(map_buf);
}

/* Заливка видеопамяти — fmemset (@1C95:0028) по адресу карты. */
#define VFILL(seg, off, n) hw_vmem_fill(seg, off, 0, n)

static void ega_write_all_planes(void)
{
    hw_out(0x3CE, 5);
    hw_out(0x3CF, 0);
    hw_out(0x3C4, 2);
    hw_out(0x3C5, 0xF);
}

/* @015F:0001 file:02DF1 video_reset — очистка видеопамяти */
void video_reset(void)
{
    switch (level_mode) {
    case 2:
        VFILL(0xB800, 0x4000, 0x4000);
        /* fallthrough */
    case 0:
        VFILL(0xB800, 0x0000, 0x4000);
        break;
    case 3:
        VFILL(0xB800, 0x0000, 0x8000);
        break;
    case 1:
        ega_write_all_planes();
        VFILL(0xA000, 0x0000, 0x4000);
        break;
    }
}

/* @015F:0091 file:02E81 clear_screen — обе страницы */
void clear_screen(void)
{
    switch (level_mode) {
    case 2:
        VFILL(0xB800, 0x4000, 0x14A0);
        /* fallthrough */
    case 0:
        VFILL(0xB800, 0x0000, 0x14A0);
        VFILL(0xB800, 0x2000, 0x14A0);
        break;
    case 3:
        VFILL(0xB800, 0x0000, 0x2940);
        VFILL(0xB800, 0x2000, 0x2940);
        VFILL(0xB800, 0x4000, 0x2940);
        VFILL(0xB800, 0x6000, 0x2940);
        break;
    case 1:
        ega_write_all_planes();
        VFILL(0xA000, 0x0000, 0x13B0);
        VFILL(0xA000, 0x1F40, 0x13B0);
        break;
    }
}

/* @015F:0195 file:02F85 SCROLLUPDATE — фаза 10: буфер на экран */
void SCROLLUPDATE(void)
{
    switch (level_mode) {
    case 0: case 2: scroll_blit((u16)(video_offset + 0x8000)); break;
    case 3:         scroll_blit(video_offset); break;
    case 1:         scroll_blit((u16)(video_offset + 0x4000)); break;
    }
}

/* @015F:01DE file:02FCE redraw_rect — перерисовать тайлы прямоугольника
 * (в пределах мира) и запомнить вид как prev_view */
void redraw_rect(const s16 *r)
{
    s16 v[4], c0, c1, r0, r1, si, di;
    u16 off;
    rect_intersect(r, world_rect, v);
    c0 = px_to_col[v[0]];
    r0 = px_to_trow[v[1]];
    c1 = px_to_col[v[2] - 1] + 1;
    r1 = px_to_trow[v[3] - 1] + 1;
    off = vb_row_off[v[1]] + v[0];
    for (di = r0; di < r1; di++)
        for (si = c0; si < c1; si++)
            draw_tile(si, di);
    vb_present_rect(off, v[2] - v[0], v[3] - v[1]);
    ds_copy_in(0xBA70, 8, (const u8 *)view_rect);   /* fmemcpy(prev_view, view_rect, 8) */
}

/* @015F:0296 file:03086 SCROLLVIEW — фаза 8: дорисовать тайлы,
 * открывшиеся при сдвиге вида: сначала полоса по x, затем по y */
void SCROLLVIEW(void)
{
    s16 dx = view_rect[0] - prev_view[0], dy = view_rect[1] - prev_view[1];
    s16 oc0, or0, oc1, or1, nc0, nr0, nc1, nr1;
    s16 from, to, px, py, w, h, si, di;
    rect_intersect(prev_view, world_rect, old_vis);
    rect_intersect(view_rect, world_rect, new_vis);
    oc0 = px_to_col[old_vis[0]];
    or0 = px_to_trow[old_vis[1]];
    oc1 = px_to_col[old_vis[2] - 1] + 1;
    or1 = px_to_trow[old_vis[3] - 1] + 1;
    nc0 = px_to_col[new_vis[0]];
    nr0 = px_to_trow[new_vis[1]];
    nc1 = px_to_col[new_vis[2] - 1] + 1;
    nr1 = px_to_trow[new_vis[3] - 1] + 1;
    if (dx != 0) {
        if (dx > 0) {
            from = oc1; to = nc1;
            px = old_vis[2];
            w = new_vis[2] - px;
        } else {
            from = nc0; to = oc0;
            px = new_vis[0];
            w = old_vis[0] - px;
        }
        py = old_vis[1];
        h = old_vis[3] - py;
        for (si = from; si < to; si++)
            for (di = or0; di < or1; di++)
                draw_tile(si, di);
        h++;
        w++;
        vb_present_rect(vb_row_off[py] + px, w, h);
        oc0 = nc0;
        oc1 = nc1;
    }
    if (dy != 0) {
        if (dy > 0) {
            from = or1; to = nr1;
            py = old_vis[3];
            h = new_vis[3] - py;
        } else {
            from = nr0; to = or0;
            py = new_vis[1];
            h = old_vis[1] - py;
        }
        px = old_vis[0];
        w = old_vis[2] - px;
        for (di = from; di < to; di++)
            for (si = oc0; si < oc1; si++)
                draw_tile(si, di);
        h++;
        w++;
        vb_present_rect(vb_row_off[py] + px, w, h);
    }
}

/* @015F:0641 file:03431 video_nop */
void video_nop(void)
{
}
