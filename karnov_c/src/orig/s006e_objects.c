/* Модуль 0x006E (file 0x1EE0) — пул объектов, перемещение, отрисовка,
 * сбор пар пересечений.
 *
 * Две parity-цепочки: объект с чётным индексом — цепочка 0 (игрок и его
 * снаряды), с нечётным — 1 (враги). active_index[k*2 + p] — k-й активный
 * объект цепочки p; active_count[p] — их число. */
#include "orig/fn.h"
#include "orig/todo.h"

#define OBJ_ADDR(i) (0x2824u + (u16)(i) * ActiveObject_SIZE)

static ActiveObject *free_link(ActiveObject *o)
{
    return ds_resolve((u32)LONG(o->y_screen, o->x_screen));
}

static void set_free_link(ActiveObject *o, u32 segoff)
{
    o->x_screen = (s16)segoff;
    o->y_screen = (s16)(segoff >> 16);
}

/* @006E:0002 file:01EE2 init_object_pool */
void init_object_pool(void)
{
    s16 si, di;
    ActiveObject *o;
    active_count[1] = 0;
    active_count[0] = 0;
    stamp_count = 0;
    pair_count = 0;
    for (si = 0; si < 0x80; si++) {
        active_index[si] = 0;
        pair_table[si] = 0;
        all_objects[si].handler = NULL;
    }
    for (si = 0; si < 2; si++) {
        o = &all_objects[si];
        free_head[si] = o;
        for (di = si; di < 0x80; di += 2, o += 2) {
            set_free_link(o, (u32)DS_SEG << 16 | (u16)(OBJ_ADDR(di) + 0x1EA));
            o->self_idx = di;
        }
        set_free_link(&all_objects[si + 0x7E], 0);
    }
}

/* @006E:00D1 file:01FB1 objects_init */
void objects_init(void)
{
    switch (level_mode) {
    case 0: case 2: obj_2592 = 0x24; obj_271A = 0x1E; break;
    case 1:         obj_2592 = 0x12; obj_271A = 0x1E; break;
    case 3:         obj_2592 = 0x48; obj_271A = 0x1E; break;
    }
    init_object_pool();
    pair_dx = 0xF;
    pair_dy = 0x32;
}

/* @006E:011B file:01FFB alloc_object — индекс объекта или -1 */
s16 alloc_object(s16 parity)
{
    ActiveObject *o;
    s16 di;
    if (free_head[parity] == NULL)
        return -1;
    o = free_head[parity];
    free_head[parity] = free_link(o);
    di = active_count[parity] * 2 + parity;
    active_index[di] = o->self_idx;
    o->stamped = 0;
    o->hit_flag = 0;
    o->handler = NULL;
    o->parent = NULL;
    o->num_bboxes = 0;
    o->frame_ctr = 0;
    o->ay_hi = 0; o->ay_lo = 0;
    o->ax_hi = 0; o->ax_lo = 0;
    o->vy_hi = 0; o->vy_lo = 0;
    o->vx_hi = 0; o->vx_lo = 0;
    o->y_world = 0; o->y_lo = 0;
    o->x_world = 0; o->x_lo = 0;
    o->active_pos = di;
    active_count[parity]++;
    return o->self_idx;
}

/* @006E:0226 file:02106 free_object — последний в цепочке занимает место
 * освобождённого */
void free_object(s16 idx)
{
    ActiveObject *o;
    s16 p, di, last;
    unstamp_object(&all_objects[idx]);
    p = idx & 1;
    active_count[p]--;
    o = &all_objects[idx];
    di = o->active_pos;
    last = active_index[active_count[p] * 2 + p];
    active_index[di] = last;
    all_objects[last].active_pos = di;
    set_free_link(o, ds_unresolve(free_head[p]));
    free_head[p] = o;
}

/* @006E:02D0 file:021B0 free_all_objects */
void free_all_objects(void)
{
    s16 si;
    for (si = 0; si < 2; si++)
        while (active_count[si] != 0)
            free_object(active_index[si]);
}

/* @006E:02FC file:021DC unstamp_restore — снять отметки объекта с карты
 * (атрибуты клеток из tile_attr) и перерисовать тайлы под его боксами */
static void unstamp_restore(ActiveObject *o)
{
    s16 r[4], v[4];
    s16 nb, si, di, c0, c1, r0, r1, h, w;
    u16 off;
    SpriteBox *b;
    u16 co;
    for (nb = 0, b = &o->box[0]; o->num_bboxes > nb; nb++, b++) {
        r[0] = (s16)((b->x0 > 0 ? b->x0 : 0) >> tile_shift);
        r[2] = (s16)((b->x1 > 0 ? b->x1 : 0) >> tile_shift);
        r[1] = b->y0 > 0 ? b->y0 : 0;
        r[3] = b->y1 > 0 ? b->y1 : 0;
        c0 = PX_COL(r[0]);
        c1 = PX_COL(r[2]) + 1;
        r0 = PX_TROW(r[1]);
        r1 = PX_TROW(r[3]) + 1;
        for (di = r0; di < r1; di++) {
            si = c0;
            for (co = (u16)(si * 2); si < c1; si++, co += 2)
                MAP_WR(di, co + 1, TILE_ATTR(MAP_RD(di, co)));
        }
        rect_intersect(r, view_rect, v);
        c0 = PX_COL(v[0]);
        c1 = PX_COL(v[2]) + 1;
        r0 = PX_TROW(v[1]);
        r1 = PX_TROW(v[3]) + 1;
        for (di = r0; di < r1; di++)
            for (si = c0; si < c1; si++)
                draw_tile(si, di);
        off = vb_row_off[v[1]] + v[0];
        h = v[3] - v[1] + 3;
        w = v[2] - v[0] + 1;
        if (h > 0 && w > 0)
            vb_present_rect(off, w, h);
    }
}

/* @006E:04B4 file:02394 unstamp_object */
void unstamp_object(ActiveObject *o)
{
    s16 si, di;
    o = &all_objects[active_index[o->active_pos]];
    if (o->stamped == 0)
        return;
    o->stamped = 0;
    stamp_count--;
    si = o->stamp_pos;
    di = stamp_list[stamp_count];
    stamp_list[si] = di;
    all_objects[di].stamp_pos = si;
    unstamp_restore(o);
}

/* @006E:052B file:0240B stamp_object */
void stamp_object(ActiveObject *o)
{
    s16 si, di = active_index[o->active_pos];
    if (o->stamped == 0) {
        o->stamped = 1;
        for (si = 0; o->num_bboxes > si; si++)
            o->box[si].was_vis = 0;
        o->stamp_pos = stamp_count;
        stamp_list[stamp_count] = di;
        stamp_count++;
    }
    update_bboxes(o);
}

/* @006E:059A file:0247A update_bboxes — мировые прямоугольники боксов */
void update_bboxes(ActiveObject *o)
{
    s16 di, si, v;
    SpriteBox *b;
    for (di = 0, b = &o->box[0]; o->num_bboxes > di; di++, b++) {
        si = b->sprite;
        v = o->x_world + b->dx;
        b->x0 = v;
        b->x1 = v + sprite_w[si];
        v = o->y_world + b->dy;
        b->y0 = v;
        b->y1 = v + sprite_h[si];
    }
}

/* @006E:060A file:024EA update_all_bboxes */
static void update_all_bboxes(void)
{
    s16 p, k, end, n, di, si, v;
    ActiveObject *o;
    SpriteBox *b;
    for (p = 0; p < 2; p++) {
        end = active_count[p] * 2 + p;
        for (k = p; k < end; k += 2) {
            o = &all_objects[active_index[k]];
            n = o->num_bboxes;
            for (di = 0, b = &o->box[0]; di < n; di++, b++) {
                si = b->sprite;
                v = o->x_world + b->dx;
                b->x0 = v;
                b->x1 = v + sprite_w[si];
                v = o->y_world + b->dy;
                b->y0 = v;
                b->y1 = v + sprite_h[si];
            }
        }
    }
}

/* @006E:06D3 file:025B3 UPDATELOCS — фаза 1: x += vx, y += vy, vx += ax,
 * vy += ay (Q16.16); счётчики цикла — глобалы оригинала */
void UPDATELOCS(void)
{
    ActiveObject *o;
    for (objloop_chain = 0; objloop_chain < 2; objloop_chain++) {
        objloop_end = active_count[objloop_chain] * 2 + objloop_chain;
        for (objloop_k = objloop_chain; objloop_k < objloop_end; objloop_k += 2) {
            o = &all_objects[active_index[objloop_k]];
            SETLONG(o->x_world, o->x_lo, LONG(o->x_world, o->x_lo) + LONG(o->vx_hi, o->vx_lo));
            SETLONG(o->y_world, o->y_lo, LONG(o->y_world, o->y_lo) + LONG(o->vy_hi, o->vy_lo));
            SETLONG(o->vx_hi, o->vx_lo, LONG(o->vx_hi, o->vx_lo) + LONG(o->ax_hi, o->ax_lo));
            SETLONG(o->vy_hi, o->vy_lo, LONG(o->vy_hi, o->vy_lo) + LONG(o->ay_hi, o->ay_lo));
            o->frame_ctr++;
        }
    }
}

/* @006E:07A5 file:02685 box_visible — бокс пересекает clip_rect и имеет спрайт */
s16 box_visible(SpriteBox *b)
{
    s16 x0 = (s16)(b->x0 >> tile_shift), x1 = (s16)(b->x1 >> tile_shift);
    return b->y0 < clip_rect[3] && b->y1 > clip_rect[1] &&
           x0 < clip_rect[2] && x1 > clip_rect[0] && b->sprite != 0;
}

/* @006E:07FC file:026DC any_box_visible */
s16 any_box_visible(ActiveObject *o)
{
    s16 vis = 0, si;
    SpriteBox *b;
    for (si = 0, b = &o->box[0]; o->num_bboxes > si && vis == 0; si++, b++)
        vis = box_visible(b);
    return vis;
}

/* @006E:0846 file:02726 ERASECYCLE — фаза 11: стереть спрайты
 * невпечатанных объектов. В EGA игрок (obj_type 0) выровнен на 4. */
void ERASECYCLE(void)
{
    s16 p, k, end, si, ega_player, row;
    u16 col;
    ActiveObject *o;
    SpriteBox *b;
    for (p = 0; p < 2; p++) {
        end = active_count[p] * 2 + p;
        for (k = p; k < end; k += 2) {
            o = &all_objects[active_index[k]];
            ega_player = level_mode == 1 && o->obj_type == 0;
            if (o->stamped != 0)
                continue;
            for (si = 0, b = &o->box[0]; o->num_bboxes > si; si++, b++) {
                if (!box_visible(b))
                    continue;
                if (ega_player)
                    col = (u16)(b->x0 & 0xFFFC) >> tile_shift;
                else
                    col = (u16)(b->x0 >> tile_shift);
                row = b->y0 > 0 ? b->y0 : 0;
                sprite_erase(vb_row_off[row] + col, b->sprite,
                             ega_player ? 0 : b->x0 & 1);
            }
        }
    }
}

/* @006E:0976 file:02856 draw_object_sprites */
static void draw_object_sprites(s16 idx)
{
    ActiveObject *o = &all_objects[idx];
    s16 n = o->num_bboxes, si, row, col, phase;
    SpriteBox *b;
    for (si = 0, b = &o->box[0]; si < n; si++, b++) {
        if (!box_visible(b))
            continue;
        col = (s16)(b->x0 >> tile_shift);
        row = b->y0 > 0 ? b->y0 : 0;
        phase = sprite_phase_on ? b->x0 & vid_AF1E : 0;
        sprite_blit(vb_row_off[row] + col, b->sprite, phase, 0);
    }
}

/* @006E:0A2A file:0290A draw_player_ega — колонка выровнена на 4, без фазы */
static void draw_player_ega(s16 idx)
{
    ActiveObject *o = &all_objects[idx];
    s16 n = o->num_bboxes, si, row;
    u16 col;
    SpriteBox *b;
    for (si = 0, b = &o->box[0]; si < n; si++, b++) {
        if (!box_visible(b))
            continue;
        col = (u16)(b->x0 & 0xFFFC) >> tile_shift;
        row = b->y0 > 0 ? b->y0 : 0;
        sprite_blit(vb_row_off[row] + col, b->sprite, 0, 0);
    }
}

/* @006E:0AC4 file:029A4 draw_stamped — впечатанный объект рисуется в фон
 * один раз, когда бокс впервые попадает в view_rect */
static void draw_stamped(s16 idx)
{
    ActiveObject *o = &all_objects[idx];
    s16 n = o->num_bboxes, k, si, di, h, w = 0, was, phase;
    SpriteBox *b;
    for (k = 0, b = &o->box[0]; k < n; k++, b++) {
        si = (s16)(b->x0 >> tile_shift);
        di = b->y0 > 0 ? b->y0 : 0;
        h = sprite_h[b->sprite];
        was = b->was_vis;
        switch (level_mode) {
        case 0: case 2: w = sprite_wb[b->sprite] * 2 + 0xC; break;
        case 1:         w = (s16)(sprite_w[b->sprite] >> tile_shift); break;
        case 3:         w = sprite_wb[b->sprite] * 4 + 0x18; break;
        }
        b->was_vis = view_rect[0] < si && si + w < view_rect[2] &&
                     view_rect[1] < di && di + h + 1 < view_rect[3];
        if (was == 0 && b->was_vis != 0) {
            phase = sprite_phase_on ? b->x0 & vid_AF1E : 0;
            sprite_blit(vb_row_off[di] + si, b->sprite, phase, 1);
            sprite_erase(vb_row_off[di] + si, b->sprite, phase);
        }
    }
}

/* @006E:0C37 file:02B17 DRAWCYCLE — фаза 9: впечатанные, затем игрок
 * (active_index[0]), цепочка 1, остальная цепочка 0 */
void DRAWCYCLE(void)
{
    s16 si, di;
    update_all_bboxes();
    for (si = 0; si < stamp_count; si++)
        draw_stamped(stamp_list[si]);
    if (active_count[0] != 0) {
        if (level_mode == 1)
            draw_player_ega(active_index[0]);
        else
            draw_object_sprites(active_index[0]);
    }
    for (si = 0; si < active_count[1]; si++) {
        di = active_index[si * 2 + 1];
        if (all_objects[di].stamped == 0)
            draw_object_sprites(di);
    }
    for (si = 1; si < active_count[0]; si++) {
        di = active_index[si * 2];
        if (all_objects[di].stamped == 0)
            draw_object_sprites(di);
    }
}

/* @006E:0CDB file:02BBB POSTHITS — фаза 2: пары пересечений боксов
 * «цепочка 0 × цепочка 1». Грубый отсев по |dx| < pair_dx, |dy| < pair_dy
 * и отсутствию hit_flag не действует для объектов с более чем 2 боксами.
 * Сравниваются только боксы с ненулевым hitflag; первая пара — и выход. */
void POSTHITS(void)
{
    s16 end0, end1, i, j, di, si, d;
    ActiveObject *a, *bo;
    SpriteBox *ba, *bb;
    pair_count = 0;
    end0 = active_count[0] * 2;
    end1 = active_count[1] * 2 + 1;
    for (i = 0; i < end0; i += 2) {
        a = &all_objects[active_index[i]];
        for (j = 1; j < end1; j += 2) {
            bo = &all_objects[active_index[j]];
            d = a->x_world - bo->x_world;
            if (d <= 0)
                d = -d;
            if (d < pair_dx) {
                d = a->y_world - bo->y_world;
                if (d <= 0)
                    d = -d;
                if (d < pair_dy && a->hit_flag == 0 && bo->hit_flag == 0)
                    goto test;
            }
            if (bo->num_bboxes <= 2)
                continue;
test:
            for (di = 0, ba = &a->box[0]; a->num_bboxes > di; di++, ba++) {
                if (ba->hitflag == 0)
                    continue;
                for (si = 0, bb = &bo->box[0]; bo->num_bboxes > si; si++, bb++) {
                    if (ba->x0 < bb->x1 && ba->y0 < bb->y1 &&
                        ba->x1 > bb->x0 && ba->y1 > bb->y0 && bb->hitflag != 0) {
                        bo->hit_flag = 1;
                        a->hit_flag = 1;
                        bo->hit_box_other = di;
                        a->hit_box_self = di;
                        bo->hit_box_self = si;
                        a->hit_box_other = si;
                        bo->hit_idx = a->self_idx;
                        pair_table[pair_count++] = a->self_idx;
                        a->hit_idx = bo->self_idx;
                        pair_table[pair_count++] = bo->self_idx;
                        si = 0x3E7;
                        di = si;
                    }
                }
            }
        }
    }
}
