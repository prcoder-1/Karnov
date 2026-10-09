/* Модуль 0x161F (file 0x179F0) — AI-типы 39–42: лента, капли фонтана,
 * фонтан, прыгун со стрельбой. */
#include "orig/fn.h"


/* @161F:0002 file:179F2 ai_t39 — лента из 6 спрайтов 0x103..0x106,
 * анимируется сдвигом кадра (misc 1 или 3 — направление) */
void ai_t39(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->num_bboxes = 6;
        for (si = 0; cur_obj->num_bboxes > si; si++) {
            cur_obj->box[si].sprite = 0x103;
            cur_obj->box[si].dx = si * 8;
            cur_obj->box[si].dy = 0;
        }
        cur_obj->misc = cur_obj->y_world % 10 == 0 ? 1 : 3;
        /* fallthrough */
    case 2:
        for (si = 0; cur_obj->num_bboxes > si; si++)
            cur_obj->box[si].sprite =
                ((cur_obj->box[si].sprite + cur_obj->misc - 0x103) & 3) + 0x103;
        break;
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @161F:00FC file:17AEC t42_spray — 5 капель (тип 41) веером вверх
 * (phase 1) или вниз */
static void t42_spray(void)
{
    ActiveObject *o;
    s16 si, di, t, tx, ty;
    s32 vx, vy;
    di = cur_obj->x_world;
    t = cur_obj->y_world;
    for (si = 0; si < 5; si++) {
        o = new_object(1);
        if (o == NULL)
            continue;
        init_active_object(o, ai_t41, 0);
        o->x_world = di + 0xC;
        tx = o->x_world + (si - 2) * 0x12;
        o->y_world = (cur_obj->phase != 0 ? 5 : 0xF) + t;
        ty = o->y_world + (cur_obj->phase != 0 ? -0x32 : 0x32);
        aim_speed(di, t, tx, ty, &vx, &vy, 0x40000L);
        SETLONG(o->vx_hi, o->vx_lo, vx);
        SETLONG(o->vy_hi, o->vy_lo, vy);
        o->ay_hi = 0;
        o->ay_lo = 0x2C00;
    }
}

/* @161F:020A file:17BFA ai_t41 — капля: гаснет вне пустоты; неуязвима */
void ai_t41(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->obj_type = 3;
        o->hit_points = 0x64;
        o->box[0].hitflag = 1;
        o->tick_period = 2;
        o->num_bboxes = 1;
        o->state_list = sl_1CBE;
        pending_state = 0;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        if (attr_at(cur_obj->x_world, cur_obj->y_world) != 2)
            ai_result |= 1;
        ai_result |= 4;
        break;
    case 4: case 5: case 6:
        cur_obj->state_byte = 2;
        break;
    }
}

/* @161F:029F file:17C8F ai_t42 — фонтан (phase — вверх): пауза
 * frame_idx*10 тиков или случайно, затем выброс 5 капель */
void ai_t42(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 3;
        o->misc = 0;
        o->phase = o->frame_idx & 1;
        o->phase_ctr = o->frame_idx * 0xA;
        if (o->phase != 0)
            o->y_world -= 0x14;
        o = cur_obj;
        o->num_bboxes = 1;
        o->state_list = sl_1CF2;
        stamp_object(o);
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            pending_state = o->phase;
            ai_result |= 2;
            o->frame_idx = 0;
            /* fallthrough */
        case 1:
            o = cur_obj;
            if ((o->phase_ctr -= o->tick_period) <= 0 || !(rtl_rand() & 0x7F))
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = o->phase + 2;
            ai_result |= 2;
            o->frame_idx = 0;
            /* fallthrough */
        case 3:
            if (cur_obj->frame_idx == an_1CCE[0] - 1) {
                t42_spray();
                cur_obj->phase_ctr = 500;
                cur_obj->misc = 0;
            }
            break;
        }
        cur_obj->box[0].was_vis = 0;
        ai_result |= 4;
        break;
    }
}

/* @161F:03DD file:17DCD edge_stop — стоп на краю опоры */
static void edge_stop(void)
{
    ActiveObject *o = cur_obj;
    s16 si, di, t;
    di = o->box[0].x0;
    t = o->box[0].x1;
    si = o->box[0].y1 + 5;
    if (o->vx_hi < 0) {
        if (ATTR_PROP(attr_at(di, si)) & 1)
            return;
    } else if (ATTR_PROP(attr_at(t, si)) & 1) {
        return;
    }
    SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, 0);
}

/* @161F:0447 file:17E37 ai_t40 — падает сверху при приближении игрока,
 * затем бежит к нему, стреляя */
void ai_t40(void)
{
    ActiveObject *o;
    s16 si, dir;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 2;
        o->misc = 0;
        o->state_list = sl_1D22;
        o->phase = 0;
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc = si | 1;
        dir = player_ptr->x_world > cur_obj->x_world ? 1 : 0;
        switch (si) {
        case 0:
            pending_state = dir;
            ai_result |= 2;
            o = cur_obj;
            o->frame_idx = 0;
            o->active = 0;
            o->box[0].hitflag = 0;
            /* fallthrough */
        case 1:
            if (ABS16(player_ptr->x_world - cur_obj->x_world) < 0x78 ||
                cur_obj->frame_ctr > 0xC8)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = dir;
            ai_result |= 2;
            o = cur_obj;
            o->active = 1;
            o->vy_hi = 1;
            o->vy_lo = 0x8000;
            /* fallthrough */
        case 3:
            o = cur_obj;
            if (attr_at(o->x_world, o->box[0].y1) != 2)
                cur_obj->misc = 4;
            break;
        case 4:
            pending_state = dir;
            ai_result |= 2;
            o = cur_obj;
            o->frame_idx = 0;
            o->active = 0;
            o->frame_ctr = 0;
            place_on_ground_center(o, 0x107);
            set_motion(0);
            /* fallthrough */
        case 5:
            if ((ABS16(player_ptr->x_world - cur_obj->x_world) < 0x3C || cur_obj->phase != 0) &&
                cur_obj->frame_ctr > 0x4B)
                cur_obj->misc = 6;
            break;
        case 6:
            pending_state = dir;
            ai_result |= 2;
            o = cur_obj;
            o->active = 1;
            o->phase = 1;
            SETLONG(o->vx_hi, o->vx_lo, dir != 0 ? 0xC000L : (s32)0xFFFF4000L);
            o->frame_ctr = 0;
            o->box[0].hitflag = 1;
            /* fallthrough */
        case 7:
            edge_stop();
            if (!((cur_obj->frame_ctr >> 3) & 1)) {
                s16 ty = (rtl_rand() & 0x7F) + player_ptr->y_world - 0x3F;
                o = cur_obj;
                spawn_projectile(dir != 0 ? o->box[0].x1 : o->box[0].x0, o->y_world + 0xA,
                                 player_ptr->x_world, ty, 1, 0x18000L, 0);
            }
            if (cur_obj->frame_ctr > 0x1E)
                cur_obj->misc = 4;
            break;
        }
        stop_at_wall();
        ai_result |= 4;
        break;
    case 4:
        die_puff2();
        add_score(0x7000);
        /* fallthrough */
    case 6:
        ai_result |= 1;
        break;
    }
}
