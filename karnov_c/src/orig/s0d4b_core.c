/* Модуль 0x0D4B (file 0xECB0) — ядро объектов: клетки карты, движение с
 * коллизиями, автомат состояний (ai_dispatch), THINKCYCLE, эффекты.
 *
 * Обработчик объекта вызывается с cur_obj/cur_state и сообщает решение
 * битами ai_result: 1 — удалить (в конце THINKCYCLE), 2 — сменить
 * аним-состояние на pending_state, 4 — показать текущий кадр, 8 — передать
 * управление handoff_obj, 0x10 — «пуф» (puff_handler).
 * Состояния: 0, 1 — вход (1 — с переключением на ai_next), 2 — работа,
 * 3, 5 — ..., 4 — ранен, 6 — удаление. */
#include "orig/fn.h"
#include "orig/todo.h"

#define NO_STATE 0x3E7

/* @0D4B:0003 file:0ECB3 map_set_attr */
void map_set_attr(s16 col, s16 row, s16 attr)
{
    map_row[row][(u16)(col * 2) + 1] = (u8)attr;
}

/* @0D4B:0021 file:0ECD1 map_attr */
s16 map_attr(s16 col, s16 row)
{
    return map_row[row][(u16)(col * 2) + 1];
}

/* @0D4B:003E file:0ECEE map_tile */
s16 map_tile(s16 col, s16 row)
{
    return map_row[row][(u16)(col * 2)];
}

/* @0D4B:005A file:0ED0A attr_at — атрибут клетки под точкой (x_world, y) */
s16 attr_at(s16 x, s16 y)
{
    u8 *row = map_row[px_to_trow[y]];
    return row[(u16)(px_to_col[x >> tile_shift] * 2) + 1];
}

/* @0D4B:008D file:0ED3D set_motion — скорости cur_obj из motion_table */
void set_motion(s16 n)
{
    Motion *m = &motion_table[n];
    SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, m->vx);
    SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, m->vy);
    SETLONG(cur_obj->ax_hi, cur_obj->ax_lo, m->ax);
    SETLONG(cur_obj->ay_hi, cur_obj->ay_lo, m->ay);
}

/* @0D4B:0105 file:0EDB5 dist_to_player — |dx| + |dy| от cur_obj */
s16 dist_to_player(void)
{
    s16 dx = player_ptr->x_world - cur_obj->x_world;
    s16 dy = player_ptr->y_world - cur_obj->y_world;
    return (dx <= 0 ? -dx : dx) + (dy <= 0 ? -dy : dy);
}

/* @0D4B:014C file:0EDFC snap_to_ground — опустить o на первую клетку
 * колонки col не выше row, у которой свойства атрибута пересекаются с mask */
void snap_to_ground(ActiveObject *o, s16 sprite, u16 mask, s16 col, s16 row)
{
    s16 si = row - 1;
    u16 hit = 0;
    while (hit == 0) {
        si++;
        hit = attr_props[map_attr(col, si)] & mask;
    }
    o->y_world = si * 10 - sprite_h[sprite];
}

/* @0D4B:019F file:0EE4F place_on_ground — по левому краю */
void place_on_ground(ActiveObject *o, s16 sprite, s16 kind)
{
    snap_to_ground(o, sprite, attr_props[kind], px_to_col[o->x_world >> tile_shift],
                   px_to_trow[o->y_world] - 1);
}

/* @0D4B:01DE file:0EE8E place_on_ground_center — по середине спрайта */
void place_on_ground_center(ActiveObject *o, s16 sprite)
{
    snap_to_ground(o, sprite, 0x89,
                   px_to_col[(s16)((sprite_w[sprite] >> 1) + o->x_world) >> tile_shift],
                   px_to_trow[o->y_world] - 1);
}

/* @0D4B:0229 file:0EED9 center_on_column */
void center_on_column(ActiveObject *o, s16 col, s16 w)
{
    o->x_world = col * 8 + 4 - (w >> 1);
}

/* @0D4B:024C file:0EEFC stop_at_wall — vx = 0, если впереди (по vx_hi·4,
 * на 12 выше низа бокса 0) клетка не с атрибутом 2 */
void stop_at_wall(void)
{
    s16 si = cur_obj->vx_hi * 4, di = cur_obj->box[0].y1 - 0xC;
    if (si < 0 && attr_at(cur_obj->box[0].x0 + si, di) != 2)
        goto stop;
    if (si <= 0)
        return;
    if (attr_at(cur_obj->box[0].x1 + si, di) == 2)
        return;
stop:
    cur_obj->vx_hi = 0;
    cur_obj->vx_lo = 0;
}

/* @0D4B:02AC file:0EF5C count_live_parts — сумма hitflag боксов */
s16 count_live_parts(ActiveObject *o)
{
    s16 si, di = 0;
    for (si = 0; o->num_bboxes > si; si++)
        di += o->box[si].hitflag;
    return di;
}

/* @0D4B:02DB file:0EF8B stamp_solid — клетки над y0 — исходные атрибуты,
 * строки [y0, y1) — атрибут 4 (первая) и 3 (остальные): тело объекта
 * становится твёрдым для карты */
void stamp_solid(ActiveObject *o, s16 y0, s16 y1, s16 ncols)
{
    s16 r0, r1, c0, c1, si, di, attr;
    update_bboxes(o);
    r0 = px_to_trow[y0];
    r1 = px_to_trow[y1];
    c0 = px_to_col[o->box[0].x0 >> tile_shift];
    c1 = c0 + ncols;
    for (di = px_to_trow[o->y_world] - 1; di < r0; di++)
        for (si = c0; si < c1; si++)
            map_set_attr(si, di, (s16)tile_attr[map_tile(si, di)]);
    di = r0;
    attr = 4;
    for (; di < r1; di++) {
        for (si = c0; si < c1; si++)
            map_set_attr(si, di, attr);
        attr = 3;
    }
}

/* @0D4B:0392 file:0F042 player_sprite_h */
s16 player_sprite_h(void)
{
    return sprite_h[player_ptr->box[0].sprite];
}

/* @0D4B:03A5 file:0F055 dir_latch_release */
void dir_latch_release(void)
{
    if (plr_D8AE != 0 && (inp_dir & plr_D8AE) == 0)
        plr_D8AE = 0;
}

/* @0D4B:03C0 file:0F070 puff_handler — облачко; по последнему кадру
 * объект возвращается к ai_init (die_puff ставит туда remove_object) */
void puff_handler(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        o = cur_obj;
        o->aux = o->box[0].hitflag;
        o->tick_period = 2;
        o->throttle = 0;
        o->frame_ctr = 0;
        o->num_bboxes = 1;
        o->box[0].hitflag = 0;
        o->frame_idx = 0;
        o->state_list = o->phase != 0 ? sl_1462 : sl_1440;
        pending_state = 0;
        ai_result |= 6;
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->frame_idx == an_1426[0] - 1) {
            queue_state(o, o->ai_init);
            o = cur_obj;
            o->frame_idx = o->anim_len;
            o->box[0].hitflag = o->aux;
            handoff_obj = o;
            ai_result |= 8;
        } else {
            ai_result |= 4;
        }
        break;
    case 4: case 6:
        ai_result |= 1;
        break;
    }
}

/* @0D4B:04A5 file:0F155 die_puff — остановиться, облачко, затем удаление */
void die_puff(void)
{
    set_motion(0);
    cur_obj->frame_ctr = 0;
    cur_obj->ai_init = (farfn)remove_object;
    queue_state(cur_obj, (farfn)puff_handler);
    cur_obj->phase = 0;
    cur_obj->box[0].hitflag = 0;
    ai_result |= 8;
    handoff_obj = cur_obj;
}

/* @0D4B:04FF file:0F1AF die_puff2 — то же, облачко второго вида */
void die_puff2(void)
{
    set_motion(0);
    cur_obj->frame_ctr = 0;
    cur_obj->ai_init = (farfn)remove_object;
    queue_state(cur_obj, (farfn)puff_handler);
    cur_obj->phase = 1;
    cur_obj->box[0].hitflag = 0;
    ai_result |= 8;
    handoff_obj = cur_obj;
}

/* @0D4B:0559 file:0F209 remove_object */
void remove_object(void)
{
    ai_result |= 1;
}

/* @0D4B:0564 file:0F214 probe_tile — шагать (dx, dy) от (x, y) до предела,
 * пока свойства клеток укладываются в mask; на упоре — атрибут, колонка и
 * угол клетки, 1. Координаты Q16.16, старшее слово — x_world/пиксели. */
static s16 probe_tile(s32 x, s32 y, s32 dx, s32 dy, s32 xlim, s32 ylim, u16 mask,
                      s16 *out_attr, s16 *out_x, s16 *out_y, s16 *out_col)
{
    s32 cx = x, cy = y;
    s16 xpos = dx >= 0, ypos = dy >= 0, moving = dx != 0 || dy != 0;
    s16 yh = (s16)(cy >> 16), attr;
    u16 props;
    if (ypos)
        yh = yh - yh % 10 + 9;
    else
        yh -= yh % 10;
    cy = (s32)((u32)(u16)yh << 16 | (u16)cy);
    for (;;) {
        attr = attr_at((s16)(cx >> 16), (s16)(cy >> 16));
        props = attr_props[attr];
        if ((props & mask) != props) {
            *out_attr = attr;
            *out_col = px_to_col[(s16)(cx >> 16) >> tile_shift];
            *out_x = *out_col * 8;
            *out_y = px_to_trow[(s16)(cy >> 16)] * 10;
            return 1;
        }
        cx += dx;
        cy += dy;
        if (!moving)
            return 0;
        if ((cx <= xlim) != xpos || (cy <= ylim) != ypos)
            return 0;
    }
}

/* @0D4B:06CB file:0F37B move_with_collision — пробы углов бокса на пути
 * (dx, dy): направление по большей проекции, проверяются оно и следующее
 * по часовой; при движении не вверх — ещё «ступня» (середина низа) */
void move_with_collision(CollRes *res, SpriteBox *b, s32 dx, s32 dy)
{
    u16 x_lo = 0, y_lo = 0, offx_lo = 0, offy_lo = 0;
    s16 x_hi = b->x0, y_hi = b->y0, offx_hi = 0, offy_hi = 0;
    s16 si, opp, k, horiz, vert;
    s32 x, y, tx, ty, sdx, sdy, adx, ady, offx, offy;
    s16 *masks;
    s16 attr = 0, px = 0, py = 0, col = 0;
    u16 mask;

    adx = dx > 0 ? dx : -dx;
    ady = dy > 0 ? dy : -dy;
    if (adx > ady)
        si = dx >= 0 ? 1 : 3;
    else
        si = dy >= 0 ? 2 : 0;
    masks = res->masks;
    if (masks == NULL) {
        res->any = 0;
        res->n = 0;
        return;
    }
    opp = (s16)((si + 2) % 4);
    res->any = 0;
    res->n = 0;
    sdx = dx;
    sdy = dy;
    for (;;) {
        adx = sdx > 0 ? sdx : -sdx;
        ady = sdy > 0 ? sdy : -sdy;
        if (adx <= 0x100000 && ady <= 0xA0000)
            break;
        sdx >>= 1;
        sdy >>= 1;
    }
    x = LONG(x_hi, x_lo);
    y = LONG(y_hi, y_lo);
    tx = x + dx;
    ty = y + dy;
    if (si != 0) {
        mask = (u16)masks[4];
        offx = LONG(sprite_w[b->sprite], offx_lo) >> 1;
        offx_hi = (s16)(offx >> 16);
        offx_lo = (u16)offx;
        offy_hi = sprite_h[b->sprite];
        offy = LONG(offy_hi, offy_lo);
        if (probe_tile(x + offx, y + offy, sdx, sdy, tx + offx, ty + offy, mask,
                       &attr, &px, &py, &col)) {
            res->any = 1;
            k = res->n;
            res->dir[k] = 4;
            res->attr[k] = attr;
            res->x[k] = px;
            res->y[k] = py;
            res->col[k] = col;
            res->n = k + 1;
        }
    }
    while (si != opp) {
        horiz = si == 1 || si == 2;
        vert = si == 2 || si == 3;
        mask = (u16)masks[si];
        offx_hi = horiz ? sprite_w[b->sprite] : 0;
        offy_hi = vert ? sprite_h[b->sprite] - 10 : 0;
        offx = LONG(offx_hi, offx_lo);
        offy = LONG(offy_hi, offy_lo);
        if (probe_tile(x + offx, y + offy, sdx, sdy, tx + offx, ty + offy, mask,
                       &attr, &px, &py, &col)) {
            res->any = 1;
            k = res->n;
            res->dir[k] = si;
            res->attr[k] = attr;
            res->x[k] = (horiz ? 0 : 8) + px;
            res->y[k] = (vert ? 0 : 10) + py;
            res->col[k] = col;
            res->n = k + 1;
        }
        si = (s16)((si + 1) % 4);
    }
}

/* @0D4B:0AF5 file:0F7A5 init_active_object */
void init_active_object(ActiveObject *o, farfn h, s16 arg)
{
    o->state_byte = 0;
    o->ai_init = h;
    o->handler = h;
    o->anim_len = arg;
    o->frame_idx = arg;
    o->active = 1;
}

/* @0D4B:0B31 file:0F7E1 queue_state — следующий обработчик; при
 * пустом текущем ставится сразу */
void queue_state(ActiveObject *o, farfn h)
{
    o->ai_next = h;
    o->state_byte = 1;
    if (o->handler == NULL)
        o->handler = h;
}

/* @0D4B:0B6E file:0F81E damage_exchange — игрок и b теряют HP друг друга;
 * 1, если игрок обнулился */
static s16 damage_exchange(ActiveObject *b)
{
    s16 php, bhp;
    if (player_state == 9)
        return 0;
    php = player_ptr->hit_points;
    bhp = b->hit_points;
    player_ptr->hit_points -= bhp;
    if (player_ptr->hit_points < 0)
        player_ptr->hit_points = 0;
    b->hit_points -= php;
    if (b->hit_points < 0)
        b->hit_points = 0;
    return player_ptr->hit_points == 0;
}

/* @0D4B:0BD5 file:0F885 ai_dispatch — шаг автомата cur_obj (см. шапку) */
void ai_dispatch(void)
{
    ActiveObject *o = cur_obj;
    StateEntry *e;
    ai_result = 0;
    pending_state = NO_STATE;
    if (cur_state == 2) {
        if (o->active != 0 && ++o->frame_idx >= o->state_anim[0])
            o->frame_idx = 0;
    } else {
        switch (cur_state) {
        case 1:
            o->handler = o->ai_next;
            /* fallthrough */
        case 0:
            o->active = 1;
            o->throttle = 0;
            o->tick_period = 1;
            o->frame_ctr = 0;
            o->next_state_id = NO_STATE;
            o->state_byte = 2;
            break;
        case 3: case 5:
            if (o->state_byte != 1)
                o->state_byte = 2;
            break;
        default:
            break;
        }
    }
    cur_obj->handler();
    if (ai_result == 0)
        return;
    o = cur_obj;
    if (ai_result & 1) {
        remove_list[remove_count++] = active_index[o->active_pos];
        return;
    }
    if ((ai_result & 2) && o->next_state_id != pending_state && pending_state != NO_STATE) {
        e = &o->state_list[pending_state];
        o->state_anim = e->anim;
        o->state_offs = e->offs;
        o->next_state_id = pending_state;
        o->frame_idx = 0;
    }
    if (ai_result & 4) {
        o->box[0].sprite = o->state_anim[o->frame_idx + 1];
        o->box[0].dx = o->state_offs[o->frame_idx * 2];
        o->box[0].dy = o->state_offs[o->frame_idx * 2 + 1];
    }
    if (ai_result & 8) {
        cur_obj = handoff_obj;
        cur_state = cur_obj->state_byte;
    } else if (ai_result & 0x10) {
        queue_state(cur_obj, (farfn)puff_handler);
        cur_obj->phase = 0;
        cur_state = 1;
    } else {
        return;
    }
    ai_dispatch();
}

/* @0D4B:0E03 file:0FAB3 THINKCYCLE — фаза 6: исходы пар пересечений,
 * шаг автомата всех объектов, удаление помеченных */
void THINKCYCLE(void)
{
    s16 si, di, ia, ib, end, ahp;
    ActiveObject *a, *b;

    remove_count = 0;
    for (si = 0; si < pair_count; si += 2) {
        ia = pair_table[si];
        ib = pair_table[si + 1];
        a = &all_objects[ia];
        b = &all_objects[ib];
        a->throttle = 0;
        b->throttle = 0;
        switch (a->obj_type) {
        case 0:                         /* игрок */
            if (player_state == 6)
                break;
            switch (b->obj_type) {
            case 3:
                if (player_state != 9)
                    b->state_byte = 4;
                /* fallthrough */
            case 1: case 7:
                if (god_mode != 0 || damage_exchange(b) == 0 || god_mode != 0) {
                    b->hit_flag = 0;
                } else {
                    a->state_byte = 4;
                    play_sound(2);
                }
                break;
            case 5:
                b->state_byte = 4;
                break;
            case 9:
                b->state_byte = 5;
                break;
            default:
                break;
            }
            a->hit_flag = 0;
            break;
        case 2:                         /* выстрел игрока */
            switch (b->obj_type) {
            case 7: case 9:
                b->state_byte = 5;
                /* fallthrough */
            case 1:
                ahp = a->hit_points;
                a->hit_points -= b->hit_points;
                b->hit_points -= ahp;
                if (b->hit_points > 0) {
                    b->hit_flag = 0;
                } else {
                    if (player_state != 9) {
                        b->state_byte = 4;
                        play_sound(1);
                    }
                    if (ib == goal_idx && player_state != 6 && player_state != 7 &&
                        goal_watch != NULL) {
                        goal_watch->phase = 1;
                        player_ptr->box[0].hitflag = 0;
                    }
                }
                if (player_state != 9 && a->hit_points < 0)
                    a->state_byte = 4;
                break;
            case 3: case 5:
                a->hit_flag = 0;
                b->hit_flag = 0;
                break;
            default:
                break;
            }
            a->hit_flag = 0;
            b->hit_flag = 0;
            break;
        case 4:
            a->hit_flag = 0;
            b->hit_flag = 0;
            break;
        default:
            break;
        }
    }
    for (si = 0; si < 2; si++) {
        end = active_count[si] * 2 + si;
        for (di = si; di < end; di += 2) {
            cur_obj = &all_objects[active_index[di]];
            if (cur_obj->handler != NULL &&
                (--cur_obj->throttle <= 0 || cur_obj->state_byte != 2)) {
                cur_obj->throttle = cur_obj->tick_period;
                cur_state = cur_obj->state_byte;
                ai_dispatch();
            }
            if (si != 0 && active_index[di] != goal_idx &&
                player_x - cur_obj->x_world > 0x7D) {
                cur_obj->state_byte = 6;
                cur_obj->throttle = 0;
            }
        }
    }
    for (si = 0; si < remove_count; si++)
        free_object(remove_list[si]);
}

/* @0D4B:1124 file:0FDD4 play_sound */
void play_sound(s16 n)
{
    play_sfx(sfx_scripts[n], (u16)sfx_prio_tab[n]);
}

/* @0D4B:1148 file:0FDF8 level_clear_state */
void level_clear_state(void)
{
    goal_idx = -1;
    lvl_D880 = 0;
    lvl_D886 = 0;
    goal_watch = NULL;
}
