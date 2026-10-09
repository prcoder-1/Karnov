/* Модуль 0x1692 (file 0x18120) — AI-типы 43–47: летуны в зоне
 * (an_1D32[phase_ctr] — прямоугольник x0, y0, x1, y1) и стрелки. */
#include "orig/fn.h"


/* @1692:000D file:1812D zone_bounce — отражение от границ зоны; бит 0 —
 * по x, бит 1 — по y */
static s16 zone_bounce(ActiveObject *o)
{
    s16 si = 0;
    const s16 *r = &an_1D32[o->phase_ctr * 4];
    if (o->x_world > r[2] || o->x_world < r[0]) {
        SETLONG(o->vx_hi, o->vx_lo, -LONG(o->vx_hi, o->vx_lo));
        si |= 1;
    }
    if (o->y_world > r[3] || o->y_world < r[1]) {
        SETLONG(o->vy_hi, o->vy_lo, -LONG(o->vy_hi, o->vy_lo));
        si |= 2;
    }
    return si;
}

/* @1692:00B4 file:181D4 ai_t45 — порхает в зоне со случайной vy */
void ai_t45(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->phase_ctr = o->frame_idx;
        SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, (s32)(rtl_rand() & 0x1FFF) + (s32)0xFFFF8000L);
        o = cur_obj;
        o->vy_hi = 0;
        o->vy_lo = 0x2000;
        o->tick_period = 7;
        o->state_list = sl_1D56;
        pending_state = 0;
        ai_result |= 6;
        /* fallthrough */
    case 2:
        if ((rtl_rand() & 0x1F) < cur_obj->frame_ctr) {
            cur_obj->frame_ctr = 0;
            si = rtl_rand() & 0x1FFF;
            o = cur_obj;
            if (si & 1)
                SETLONG(o->vy_hi, o->vy_lo, LONG(o->vy_hi, o->vy_lo) - si);
            else
                SETLONG(o->vy_hi, o->vy_lo, LONG(o->vy_hi, o->vy_lo) + si);
        }
        if (zone_bounce(cur_obj) & 1) {
            pending_state = (1 - cur_obj->next_state_id) & 1;
            ai_result |= 2;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x100);
        die_puff();
        break;
    case 6:
        cur_obj->state_byte = 2;
        break;
    }
}

/* @1692:0205 file:18325 ai_t43 — плывёт по зоне, рядом с игроком
 * замедляется и роняет снаряд (вид 10) */
void ai_t43(void)
{
    ActiveObject *o;
    s16 si, di, dir;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->misc = 0;
        o->phase_ctr = o->frame_idx;
        o->tick_period = 3;
        o->state_list = sl_1D7E;
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        if (si == 0)
            SETLONG(cur_obj->vx_hi, cur_obj->vx_lo,
                    rtl_rand() & 1 ? 0xC000L : (s32)0xFFFF4000L);
        di = zone_bounce(cur_obj) & 1;
        dir = LONG(cur_obj->vx_hi, cur_obj->vx_lo) > 0 ? 1 : 0;
        switch (si) {
        case 0:
            pending_state = dir;
            ai_result |= 2;
            /* fallthrough */
        case 1:
            if (ABS16(player_ptr->x_world - cur_obj->x_world) < 0x50 && !(rtl_rand() & 0xF) &&
                cur_obj->frame_ctr > 0x14)
                cur_obj->misc = 2;
            if (di != 0) {
                pending_state = dir;
                ai_result |= 2;
            }
            o = cur_obj;
            if (o->vx_hi != 0) {
                if (o->frame_ctr <= 0x96)
                    break;
                SETLONG(o->vy_hi, o->vy_lo, -LONG(o->vx_hi, o->vx_lo));
                SETLONG(o->vx_hi, o->vx_lo, 0);
            } else {
                if (o->frame_ctr <= 0x1E)
                    break;
                o->vx_hi = o->vy_hi;
                o->vx_lo = o->vy_lo;
                SETLONG(o->vy_hi, o->vy_lo, 0);
            }
            cur_obj->frame_ctr = 0;
            break;
        case 2:
            pending_state = dir + 2;
            ai_result |= 2;
            o = cur_obj;
            SETLONG(o->vx_hi, o->vx_lo, LONG(o->vx_hi, o->vx_lo) / 2);
            cur_obj->frame_ctr = 0;
            /* fallthrough */
        case 3:
            if (cur_obj->frame_idx == 1)
                spawn_projectile(cur_obj->x_world, cur_obj->y_world + 0xA, cur_obj->x_world,
                                 player_ptr->y_world, 0xA, 0xE000L, 0);
            if (cur_obj->frame_idx == 0 && !(ai_result & 2))
                cur_obj->misc = 0;
            break;
        }
        ai_result |= 4;
        break;
    case 4: case 6:
        add_score(0x1800);
        die_puff();
        break;
    }
}

/* @1692:0479 file:18599 ai_t47 — неподвижный стрелок (вид 8) */
void ai_t47(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->phase_ctr = o->frame_idx;
        o->tick_period = 0xA;
        o->y_world -= sprite_h[0x128];
        o->state_list = sl_1DA4;
        pending_state = 0;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        if (!(rtl_rand() & 0xF))
            spawn_projectile(cur_obj->x_world, cur_obj->y_world, player_ptr->x_world,
                             player_ptr->y_world + 0xA, 8, 0xC000L, 0);
        ai_result |= 4;
        break;
    case 4: case 6:
        add_score(0x1000);
        die_puff();
        break;
    }
}

/* @1692:052E file:1864E ai_t44 — летает в зоне, иногда разворачиваясь */
void ai_t44(void)
{
    ActiveObject *o;
    s16 si, di;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->phase_ctr = o->frame_idx;
        SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, (s32)(rtl_rand() & 0x1FFF) + (s32)0xFFFF2000L);
        o = cur_obj;
        o->vy_hi = 0;
        o->vy_lo = 0x3000;
        o->tick_period = 3;
        o->state_list = sl_1DC0;
        pending_state = 0;
        ai_result |= 6;
        /* fallthrough */
    case 2:
        si = !(rtl_rand() & 0x1F);
        di = zone_bounce(cur_obj);
        if (si != 0 || (di & 1)) {
            pending_state = (1 - cur_obj->next_state_id) & 1;
            ai_result |= 2;
            if (si != 0) {
                o = cur_obj;
                SETLONG(o->vx_hi, o->vx_lo, -LONG(o->vx_hi, o->vx_lo));
            }
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x1800);
        die_puff();
        break;
    case 6:
        cur_obj->state_byte = 2;
        break;
    }
}

/* @1692:0634 file:18754 t46_shoot */
static void t46_shoot(void)
{
    spawn_projectile(cur_obj->x_world, cur_obj->y_world, player_ptr->x_world,
                     player_ptr->y_world + 0xA, 8, 0xC000L, 0);
}

/* @1692:066B file:1878B ai_t46 — стоит, затем стреляет очередью, тем
 * длиннее, чем дальше игрок */
void ai_t46(void)
{
    ActiveObject *o;
    s16 si, dir;
    switch (cur_state) {
    case 0: case 1:
        place_on_ground(cur_obj, 0x124, 0);
        o = cur_obj;
        o->misc = 0;
        o->tick_period = 9;
        o->state_list = sl_1DE4;
        /* fallthrough */
    case 2:
        dir = player_ptr->x_world > cur_obj->x_world ? 1 : 0;
        o = cur_obj;
        si = o->misc;
        o->misc = si | 1;
        switch (si) {
        case 0:
            pending_state = dir;
            ai_result |= 2;
            o = cur_obj;
            o->frame_ctr = 0;
            o->box[0].hitflag = 0;
            /* fallthrough */
        case 1:
            if (cur_obj->frame_ctr > 0x46)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = dir + 2;
            ai_result |= 2;
            cur_obj->frame_ctr = -((dist_to_player() >> 2) + 5);
            cur_obj->box[0].hitflag = 1;
            /* fallthrough */
        case 3:
            if (cur_obj->frame_ctr < 0)
                t46_shoot();
            else
                cur_obj->misc = 0;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x1000);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}
