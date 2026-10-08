/* Модуль 0x170C (file 0x188C0) — AI-типы 48 (метатель с разрывом на
 * осколки) и 49 (разрушаемая стена). */
#include "orig/fn.h"
#include "orig/todo.h"

#define ABS16(v) ((v) > 0 ? (v) : -(v))

/* @170C:000E file:188CE t48_drop — осколок: гаснет в стене и за экраном */
void t48_drop(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->obj_type = 3;
        o->hit_points = 0x64;
        o->num_bboxes = 1;
        o->box[0].hitflag = 1;
        o->box[0].sprite = 3;
        /* fallthrough */
    case 2:
        if (attr_at(cur_obj->x_world, cur_obj->y_world) != 2)
            ai_result |= 1;
        if (cur_obj->frame_ctr > 0x28 && !box_visible(&cur_obj->box[0]))
            ai_result |= 1;
        break;
    case 4: case 6:
        ai_result |= 1;
        break;
    }
}

/* @170C:00A1 file:18961 t48_burst — 6 осколков веером вверх */
static void t48_burst(void)
{
    ActiveObject *o;
    s16 si, di, t, tx, ty;
    s32 vx, vy;
    di = cur_obj->x_world;
    t = cur_obj->y_world;
    for (si = 0; si < 6; si++) {
        alloc_idx = alloc_object(1);
        o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
        if (o == NULL)
            continue;
        init_active_object(o, (farfn)t48_drop, 0);
        o->x_world = di + 3;
        tx = o->x_world + (si - 3) * 0x19;
        o->y_world = cur_obj->box[0].y1 - 5;
        ty = o->y_world - 0x46;
        aim_speed(di, t, tx, ty, &vx, &vy, 0x36000L);
        SETLONG(o->vx_hi, o->vx_lo, vx);
        SETLONG(o->vy_hi, o->vy_lo, vy);
        o->ay_hi = 0;
        o->ay_lo = 0x3000;
    }
}

/* @170C:018C file:18A4C ai_t48 — поворачивается к игроку и стреляет
 * (горизонтально); уязвим ближе 120; гибнет с разлётом осколков */
void ai_t48(void)
{
    ActiveObject *o;
    s16 si, dir;
    switch (cur_state) {
    case 0: case 1:
        place_on_ground(cur_obj, 0x12B, 0);
        o = cur_obj;
        o->misc = 0;
        o->tick_period = 4;
        o->state_list = sl_1E18;
        cur_obj->frame_idx = (rtl_rand() & 3) > 2 ? rtl_rand() & 3 : 2;
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        dir = player_ptr->x_world > cur_obj->x_world ? 1 : 0;
        cur_obj->box[0].hitflag = ABS16(player_ptr->x_world - cur_obj->x_world) < 0x78 ? 1 : 0;
        switch (si) {
        case 0:
            pending_state = dir;
            ai_result |= 2;
            cur_obj->frame_ctr = rtl_rand() & 0x5F;
            /* fallthrough */
        case 1:
            o = cur_obj;
            if (o->next_state_id != dir)
                o->misc = 0;
            if (cur_obj->frame_ctr > 0xFA)
                cur_obj->misc = 2;
            break;
        case 2: case 3:
            spawn_projectile(cur_obj->x_world, cur_obj->y_world - 5, player_ptr->x_world,
                             cur_obj->y_world, 1, 0x18000L, 1);
            cur_obj->misc = 0;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x1000);
        /* fallthrough */
    case 6:
        t48_burst();
        ai_result |= 1;
        break;
    }
}

/* @170C:034C file:18C0C wall_mark — атрибут клеток колонки стены (на
 * уровне 6 — на колонку правее) */
static void wall_mark(s16 attr)
{
    s16 si, di, r0, r1;
    di = PX_COL(cur_obj->x_world >> tile_shift);
    if (current_level == 6)
        di++;
    r0 = PX_TROW(cur_obj->y_world);
    r1 = PX_TROW(sprite_h[cur_obj->box[0].sprite] + cur_obj->y_world) - 1;
    for (si = r0; si < r1; si++)
        map_set_attr(di, si, attr);
}

/* @170C:03C5 file:18C85 ai_t49 — стена; при phase (уровень 9, x > 1100)
 * рушится от касания игрока */
void ai_t49(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->misc = 0;
        o->phase_ctr = o->hit_points;
        o->phase = current_level == 9 && o->x_world > 0x44C ? 1 : 0;
        o = cur_obj;
        o->active = 0;
        o->num_bboxes = 1;
        o->box[0].sprite = current_level == 6 ? 0x131 : 0x130;
        o = cur_obj;
        o->box[0].dy = o->box[0].dx = 0;
        wall_mark(3);
        stamp_object(cur_obj);
        break;
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            stamp_object(cur_obj);
            /* fallthrough */
        case 1:
            o = cur_obj;
            o->hit_points = o->phase_ctr;
            if (o->phase != 0 && o->hit_flag != 0 &&
                all_objects[o->hit_idx].obj_type == 0)
                cur_obj->misc = 2;
            break;
        case 2:
            unstamp_object(cur_obj);
            wall_mark(2);
            ladders_mark();
            ai_result |= 1;
            break;
        }
        cur_obj->hit_flag = 0;
        break;
    case 4:
        cur_obj->misc = 2;
        cur_obj->state_byte = 2;
        break;
    case 6:
        ai_result |= 1;
        break;
    }
}
