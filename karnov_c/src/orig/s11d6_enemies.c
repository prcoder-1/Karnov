/* Модуль 0x11D6 (file 0x13560) — снаряды врагов и AI-типы 9–19.
 * Обработчики названы по AI-типу (индекс ai_handlers). */
#include <math.h>
#include "orig/fn.h"

/* @11D6:0006 file:13566 aim_speed — скорость к цели с модулем speed:
 * k = speed / max(2, |d|), v = d * k (целочисленно) */
void aim_speed(s16 x0, s16 y0, s16 x1, s16 y1, s32 *vx, s32 *vy, s32 speed)
{
    s32 dx = x1 - x0, dy = y1 - y0, d, k;
    d = (s32)sqrt((double)(s32)((u32)dx * (u32)dx + (u32)dy * (u32)dy));
    if (d < 2)
        d = 2;
    k = speed / d;
    *vx = (s32)((u32)dx * (u32)k);
    *vy = (s32)((u32)dy * (u32)k);
}

/* @11D6:00DD file:1363D aim — путь до цели за n тиков */
void aim(s16 x0, s16 y0, s16 x1, s16 y1, s32 *vx, s32 *vy, s16 n)
{
    *vx = (s32)((u32)(s32)(x1 - x0) << 16) / n;
    *vy = (s32)((u32)(s32)(y1 - y0) << 16) / n;
}

/* @11D6:0139 file:13699 spawn_projectile — снаряд вида kind (→ misc) из
 * (x0, y0) в (tx, ty); flat — лететь горизонтально */
void spawn_projectile(s16 x0, s16 y0, s16 tx, s16 ty, s16 kind, s32 speed, s16 flat)
{
    ActiveObject *o;
    s32 vx, vy;
    o = new_object(1);
    if (o == NULL)
        return;
    queue_state(o, projectile);
    o->x_world = x0;
    o->y_world = y0;
    ty = flat != 0 ? y0 : ty;
    aim_speed(x0, y0, tx, ty, &vx, &vy, speed);
    SETLONG(o->vx_hi, o->vx_lo, vx);
    SETLONG(o->vy_hi, o->vy_lo, vy);
    if (kind == 5)
        kind = o->vx_hi > 0 ? 6 : 5;
    if (kind == 0xA)
        SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, speed);   /* так в оригинале: cur_obj */
    o->misc = kind;
}

/* @11D6:021A file:1377A burst8 — 8 осколков (вид 4) во все стороны */
static void burst8(void)
{
    s16 si, di, t;
    for (si = 0; si < 8; si++) {
        di = si & 3 ? 0xC8 : 0;
        t = (si + 6) & 3 ? 0xC8 : 0;
        if (si > 4)
            di = -di;
        if (((si + 6) & 7) > 4)
            t = -t;
        spawn_projectile(cur_obj->x_world, cur_obj->y_world, cur_obj->x_world + di,
                         cur_obj->y_world + t, 4, 0x10000, 0);
    }
}

/* @11D6:02A3 file:13803 projectile — вид (misc) = аним-состояние; 7 —
 * разрывается в стене, 10 — гаснет вне батута, прочие — за экраном */
void projectile(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        o = cur_obj;
        o->state_list = sl_1784;
        o->tick_period = 2;
        o->obj_type = 3;
        o->hit_points = 0xA;
        o->num_bboxes = 1;
        o->box[0].hitflag = 1;
        pending_state = o->misc;
        ai_result |= 6;
        switch (o->misc) {
        case 0xA:
            if (LONG(cur_obj->vy_hi, cur_obj->vy_lo) <= 0)
                SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, -LONG(cur_obj->vy_hi, cur_obj->vy_lo));
            /* fallthrough */
        case 8: case 9:
            cur_obj->obj_type = 1;
            break;
        }
        /* fallthrough */
    case 2:
        switch (cur_obj->misc) {
        case 7:
            if (attr_at(cur_obj->x_world, cur_obj->y_world) == 2)
                break;
            burst8();
            ai_result |= 1;
            break;
        case 0xA:
            if (attr_at(cur_obj->x_world, cur_obj->y_world) == 9)
                break;
            ai_result |= 1;
            break;
        default:
            if (cur_obj->frame_ctr > 0x28 && !box_visible(&cur_obj->box[0]))
                ai_result |= 1;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        if (cur_obj->misc == 7)
            burst8();
        /* fallthrough */
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @11D6:0413 file:13973 ai_t10 — летит влево волной (ay меняет знак
 * каждые 16 тиков) */
void ai_t10(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->state_list = sl_180A;
        pending_state = 0;
        SETLONG(o->ay_hi, o->ay_lo, o->frame_idx >= 0xA ? (s32)0xFFFF6000L : 0xA000);
        o->vx_hi = -1;
        o->vx_lo = 0x1000;
        ai_result |= 6;
        o->frame_ctr = 8;
        /* fallthrough */
    case 2:
        o = cur_obj;
        if ((o->frame_ctr & 0xF) == 0)
            SETLONG(o->ay_hi, o->ay_lo, -LONG(o->ay_hi, o->ay_lo));
        ai_result |= 4;
        break;
    case 4:
        add_score(0x110);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @11D6:04CA file:13A2A ai_t11 — стоит, пока игрок дальше 10, иначе
 * идёт к нему (вдвое медленнее set_motion) */
void ai_t11(void)
{
    ActiveObject *o;
    s16 si = player_ptr->x_world - cur_obj->x_world;
    s16 di = si > 0 ? si : -si;
    s16 f = si > 0;
    pending_state = f;
    switch (cur_state) {
    case 0:
        place_on_ground(cur_obj, 0x4E, 0);
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->tick_period = 2;
        o->phase = 1;
        o->state_list = sl_1856;
        ai_result |= 6;
        /* fallthrough */
    case 2:
        switch (cur_obj->phase) {
        case 0:
            if (di >= 0xA)
                break;
            cur_obj->phase = 1;
            set_motion(0);
            pending_state += 2;
            ai_result |= 2;
            break;
        case 1:
            if (di <= 0xA)
                break;
            cur_obj->phase = 0;
            set_motion(f != 0 ? 1 : 2);
            o = cur_obj;
            SETLONG(o->vx_hi, o->vx_lo, LONG(o->vx_hi, o->vx_lo) / 2);
            ai_result |= 2;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x1400);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @11D6:0617 file:13B77 ai_t17 — идёт влево (-3) */
void ai_t17(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0:
        place_on_ground(cur_obj, 0xA1, 0);
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->tick_period = 2;
        o->state_list = sl_1880;
        pending_state = 0;
        ai_result |= 6;
        o->vx_hi = -3;
        o->vx_lo = 0;
        /* fallthrough */
    case 2:
        ai_result |= 4;
        break;
    case 4:
        add_score(0x400);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @11D6:06A8 file:13C08 ai_t13 — метатель: misc — оставшиеся броски
 * (по кадру 1 анимации порождает ai_t12), phase_ctr — пауза */
void ai_t13(void)
{
    ActiveObject *o, *n;
    s16 di = player_ptr->x_world - cur_obj->x_world;
    s16 si = di >= 0 ? 1 : 0;
    pending_state = si;
    switch (cur_state) {
    case 0:
        cur_obj->y_world -= sprite_h[0x56];
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->state_list = sl_18CC;
        o->misc = (o->frame_idx >= 0xA ? 1 : 0) * 3;
        o->phase_ctr = 0x19;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->misc == 0) {
            if (o->phase_ctr < 0) {
                o->misc = 4;
                pending_state = si + 2;
            }
            ai_result |= 2;
        } else if (o->frame_idx == 1) {
            n = new_object(1);
            if (n != NULL) {
                queue_state(n, ai_t12);
                n->anim_len = 0;
                handoff_obj = n;
                ai_result |= 8;
                n->frame_idx = si;
                n->x_world = cur_obj->x_world;
                n->y_world = cur_obj->y_world;
                o = cur_obj;
                if (--o->misc != 0)
                    o->frame_idx = o->frame_ctr & 3 ? 1 : 0;
                pending_state = si + 2;
                ai_result |= 2;
            }
        } else if (o->frame_idx == 0) {
            o->phase_ctr = 0x19;
            o->misc = 0;
            pending_state = si;
            ai_result |= 2;
        }
        cur_obj->phase_ctr--;
        ai_result |= 4;
        break;
    case 5:
        o = cur_obj;
        o->misc++;
        o->frame_idx = 0;
        cur_state = 2;
        ai_dispatch();
        break;
    case 4:
        add_score(0x400);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @11D6:08B3 file:13E13 ai_t12 — прыгающий снаряд: phase 0 — брошен
 * (дуга), 1 — падает и отскакивает с половинной скоростью */
void ai_t12(void)
{
    ActiveObject *o;
    s16 si, di;
    u8 a;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->phase = o->anim_len;
        o->misc = 0;
        o->obj_type = 1;
        o->hit_points = 0x14;
        o->box[0].hitflag = 1;
        o->num_bboxes = 1;
        if (o->phase == 0) {
            o->y_world -= sprite_h[0x15];
            SETLONG(o->vx_hi, o->vx_lo, o->frame_idx != 0 ? (s32)3 << 16 : -((s32)3 << 16));
            o->vy_hi = -2;
            o->vy_lo = 0x8000;
            o->ay_hi = 0;
            o->ay_lo = 0xAAAA;
        } else {
            o->vx_hi = 2;
            o->vx_lo = 0;
            o->vy_hi = 2;
            o->vy_lo = 0x8000;
            o->ay_hi = 0;
            o->ay_lo = 0;
        }
        cur_obj->state_list = sl_18F4;
        pending_state = 0;
        ai_result |= 6;
        break;
    case 2:
        o = cur_obj;
        si = PX_COL(o->x_world >> tile_shift);
        di = PX_TROW(o->y_world + sprite_h[0x15]);
        a = map_rd(di, (u16)(si * 2) + 1);
        if ((ATTR_PROP(a) & 9) && LONG(o->vy_hi, o->vy_lo) > 0) {
            if (o->misc++ < 3)
                play_sound(8);
            o = cur_obj;
            if (o->phase != 0) {
                SETLONG(o->vx_hi, o->vx_lo, -LONG(o->vx_hi, o->vx_lo));
                SETLONG(o->vy_hi, o->vy_lo, -LONG(o->vy_hi, o->vy_lo));
                o->ay_hi = 0;
                o->ay_lo = 0x5555;
                o->phase = 0;
            } else {
                SETLONG(o->vy_hi, o->vy_lo,
                        (s32)((u32)LONG(o->vy_hi, o->vy_lo) * 0xFFFFFFFFUL) >> 1);
            }
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x1000);
        /* fallthrough */
    case 6:
        die_puff2();
        break;
    }
}

/* @11D6:0ACB file:1402B ai_t15 — прыгун у точки phase_ctr (±40):
 * прыжок, стрельба на земле, подъём */
void ai_t15(void)
{
    ActiveObject *o;
    s16 di = player_ptr->x_world - cur_obj->x_world;
    s16 t = sprite_h[0x62] + plr.player_y - (cur_obj->y_world + sprite_h[0x99]);
    s16 si = di > 0 ? 1 : 0, m, d;
    pending_state = si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 3;
        o->state_list = sl_1918;
        pending_state = si + 2;
        ai_result |= 6;
        o->misc = 0;
        o->phase_ctr = o->x_world;
        break;
    case 2:
        o = cur_obj;
        m = o->misc;
        o->misc |= 1;
        switch (m) {
        case 0: case 1:
            if ((di > 0 ? di : -di) <= 0x50)
                cur_obj->misc = 2;
            break;
        case 2:
            if (rtl_rand() & 1)
                SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, si != 0 ? 0x28000L : (s32)0xFFFD8000L);
            else
                SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, 0);
            cur_obj->vy_hi = 3;
            cur_obj->vy_lo = 0;
            pending_state = si;
            ai_result |= 2;
            /* fallthrough */
        case 3:
            o = cur_obj;
            if (attr_at(o->x_world + 5, o->y_world + o->vy_hi + sprite_h[o->box[0].sprite]) != 2)
                cur_obj->misc = 4;
            break;
        case 4:
            cur_obj->phase = 0;
            set_motion(0);
            place_on_ground_center(cur_obj, 0x99);
            spawn_projectile(cur_obj->x_world, cur_obj->y_world + 5, player_ptr->x_world, 0,
                             1, 0x24000L, 1);
            pending_state = (rtl_rand() & 1 ? 2 : 4) + si;
            ai_result |= 2;
            break;
        case 5:
            if (cur_obj->phase != 0 || (rtl_rand() & 1)) {
                cur_obj->misc = 6;
                break;
            }
            spawn_projectile(cur_obj->x_world, cur_obj->y_world + 5, player_ptr->x_world, 0,
                             1, 0x20000L, 1);
            cur_obj->phase = 1;
            break;
        case 6:
            o = cur_obj;
            SETLONG(o->vx_hi, o->vx_lo, 0);
            o->vy_hi = -3;
            o->vy_lo = 0;
            pending_state = si;
            ai_result |= 2;
            /* fallthrough */
        case 7:
            if (t > 0x1E) {
                cur_obj->y_world -= 3;
                cur_obj->misc = 2;
            }
            break;
        }
        o = cur_obj;
        d = o->x_world + o->vx_hi - o->phase_ctr;
        if ((d > 0 ? d : -d) > 0x28) {
            o->x_world = (o->x_world > o->phase_ctr ? 0x28 : -0x28) + o->phase_ctr;
            SETLONG(o->vx_hi, o->vx_lo, 0);
        }
        ai_result |= 4;
        break;
    case 5:
        if (cur_obj->misc == 1)
            cur_obj->misc = 2;
        break;
    case 4:
        add_score(0x1800);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @11D6:0E08 file:14368 ai_t14 — летун на уровне игрока: снижается,
 * у земли бросает снаряд (вид 4) и взмывает */
void ai_t14(void)
{
    ActiveObject *o;
    s16 di = player_ptr->x_world - cur_obj->x_world;
    s16 t = sprite_h[0x62] + plr.player_y - (cur_obj->y_world + sprite_h[0x96]);
    s16 si;
    if ((di > 0 ? di : -di) > 0x1E)
        si = di > 0 ? 1 : 0;
    else
        si = cur_obj->phase_ctr;
    switch (cur_state) {
    case 0:
        place_on_ground(cur_obj, 0x95, 0);
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->state_list = sl_1958;
        o->misc = 2;
        o->phase_ctr = si;
        o->vx_hi = si != 0 ? 1 : -1;
        o->vy_hi = 2;
        o->y_world = sprite_h[0x62] + plr.player_y - sprite_h[0x96] - 0x14;
        pending_state = si;
        ai_result |= 6;
        break;
    case 2:
        switch (cur_obj->misc) {
        case 0:
            pending_state = cur_obj->phase_ctr;
            ai_result |= 6;
            /* fallthrough */
        case 1:
            if (t >= 0x28) {
                o = cur_obj;
                o->misc = 2;
                o->vx_hi = o->phase_ctr != 0 ? 1 : -1;
                o->vy_hi = -2;
            }
            o = cur_obj;
            SETLONG(o->vy_hi, o->vy_lo, LONG(o->vy_hi, o->vy_lo) - 0x4000);
            break;
        case 2:
            if (t < 5) {
                o = cur_obj;
                o->phase_ctr = si;
                o->vx_hi = o->phase_ctr != 0 ? 1 : -1;
                o->vy_hi = 2;
                o->misc = 0;
                pending_state = o->phase_ctr + 2;
                ai_result |= 6;
                spawn_projectile(cur_obj->x_world, cur_obj->y_world + 0x14, player_ptr->x_world,
                                 player_ptr->y_world + 5, 4, 0x22000L, 1);
            }
            o = cur_obj;
            SETLONG(o->vy_hi, o->vy_lo, LONG(o->vy_hi, o->vy_lo) + 0x4000);
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x2600);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @11D6:1033 file:14593 ai_t16 — птица: phase_ctr — фаза облёта игрока */
void ai_t16(void)
{
    ActiveObject *o;
    s16 d = player_ptr->x_world - cur_obj->x_world;
    s16 si = d > 0 ? 1 : 0, di;
    pending_state = si;
    switch (cur_state) {
    case 0:
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->tick_period = 3;
        o->vx_hi = -2;
        o->vx_lo = 0x8000;
        o->misc = 0xD;
        o->phase_ctr = 0;
        o->state_list = sl_1984;
        pending_state = si;
        ai_result |= 6;
        break;
    case 2:
        di = plr.player_y - cur_obj->y_world;
        switch (cur_obj->phase_ctr) {
        case 0: case 1:
            o = cur_obj;
            if (--o->misc != 0)
                break;
            if (o->phase_ctr == 1) {
                o->phase_ctr = 2;
                o->phase = si;
                SETLONG(o->vx_hi, o->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
                SETLONG(o->vy_hi, o->vy_lo, di >= 0 ? 0x28000L : (s32)0xFFFD8000L);
            } else {
                o->phase_ctr = 1;
                o->misc = 0xD;
                o->vx_hi = 1;
                o->vx_lo = 0x8000;
            }
            break;
        case 2:
            if (di > 0)
                break;
            cur_obj->phase_ctr = 3;
            goto level;
        case 3:
            if (!((cur_obj->phase != 0 && d < -0x14) || d > 0x14))
                break;
            cur_obj->phase_ctr = 4;
            SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
            cur_obj->vy_hi = -3;
            cur_obj->vy_lo = 0x8000;
            break;
        case 4:
            if (di <= 0x28)
                break;
            cur_obj->phase_ctr = 5;
            SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
            cur_obj->vy_hi = 2;
            cur_obj->vy_lo = 0x8000;
            break;
        case 5:
            if (di >= 0)
                break;
            cur_obj->phase_ctr = 6;
        level:
            SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
            SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, 0);
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x110);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @11D6:127D file:147DD ai_t18 — летун на уровне игрока; при выстрелах
 * игрока подпрыгивает, иногда бросает снаряд */
void ai_t18(void)
{
    ActiveObject *o;
    s16 di = player_ptr->x_world - cur_obj->x_world;
    s16 si = di > 0 ? 1 : 0, a, t;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->aux = 0;
        o->phase = o->frame_idx;
        o->phase_ctr = player_ptr->y_world;
        o->tick_period = 3;
        o->misc = 2;
        SETLONG(o->vx_hi, o->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
        o->vy_hi = 1;
        o->vy_lo = 0x8000;
        o->y_world = sprite_h[0x62] + plr.player_y - sprite_h[0xA4] - 0x14;
        o->state_list = sl_19B4;
        pending_state = si;
        ai_result |= 6;
        break;
    case 2:
        o = cur_obj;
        a = o->phase != 0 ? player_ptr->y_world - o->phase_ctr + 0x28 : 0;
        t = plr.player_y - a + sprite_h[0x62] - (cur_obj->y_world + sprite_h[0xA4]);
        switch (cur_obj->misc) {
        case 4:
            if (shots_alive == 0 || (di > 0 ? di : -di) <= 0xA) {
                if (cur_obj->phase == 0 || !(rtl_rand() & 1))
                    break;
            }
            o = cur_obj;
            o->misc = 0;
            pending_state = si + 4;
            SETLONG(o->vx_hi, o->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
            o->vy_hi = -2;
            o->vy_lo = 0x8000;
            ai_result |= 2;
            if (o->phase == 0)
                break;
            if (++o->aux > 4) {
                o->phase = 0;
                o->vx_hi = -(o->vx_hi > 0 ? o->vx_hi : -o->vx_hi);
            }
            if (!(rtl_rand() & 1))
                break;
            spawn_projectile(cur_obj->x_world, cur_obj->y_world,
                             (rtl_rand() & 1 ? 8 : -8) + cur_obj->x_world,
                             player_ptr->y_world, 0, 0x20000L, 0);
            break;
        case 0:
            cur_obj->misc = 1;
            pending_state = si + 6;
            ai_result |= 2;
            /* fallthrough */
        case 1:
            if (t < 0x28)
                break;
            o = cur_obj;
            o->misc = 5;
            SETLONG(o->vx_hi, o->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
            SETLONG(o->vy_hi, o->vy_lo, 0);
            pending_state = si;
            ai_result |= 2;
            break;
        case 5:
            if ((di > 0 ? di : -di) >= 0xA)
                break;
            o = cur_obj;
            o->misc = 2;
            SETLONG(o->vx_hi, o->vx_lo, 0);
            o->vy_hi = 1;
            o->vy_lo = 0x8000;
            break;
        case 2:
            if (!(t < 0 || (cur_obj->phase != 0 && cur_obj->frame_ctr > 0x1E)))
                break;
            o = cur_obj;
            SETLONG(o->vx_hi, o->vx_lo, si != 0 ? 0x18000L : (s32)0xFFFE8000L);
            SETLONG(o->vy_hi, o->vy_lo, 0);
            o->misc = 4;
            o->frame_ctr = 0;
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

/* @11D6:15D6 file:14B36 boss_helper — неподвижное препятствие: снимается
 * с фона, когда игрок правее на 30, и впечатывается, когда левее на 8 */
static void boss_helper(void)
{
    if (cur_obj->frame_ctr <= 0xA)
        return;
    if (player_ptr->x_world - cur_obj->x_world > 0x1E && cur_obj->stamped != 0) {
        unstamp_object(cur_obj);
        ladders_mark();
    }
    if (cur_obj->x_world - player_ptr->x_world > 8 && cur_obj->stamped == 0)
        stamp_object(cur_obj);
}

/* @11D6:1644 file:14BA4 ai_t09 — препятствие из спрайтов 0x91 и 0x92 */
void ai_t09(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->active = 0;
        o->num_bboxes = 2;
        o->box[0].sprite = 0x91;
        o->box[0].dx = 0;
        o->box[0].dy = 0;
        o->box[1].sprite = 0x92;
        o->box[1].dx = 5;
        o->box[1].dy = sprite_h[0x91];
        o->tick_period = 1;
        stamp_object(o);
        /* fallthrough */
    case 2:
        boss_helper();
        break;
    case 4: case 6:
        ai_result |= 1;
        break;
    }
}

/* @11D6:16C0 file:14C20 ai_t19 — препятствие из спрайта 0xAC */
void ai_t19(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->active = 0;
        o->num_bboxes = 1;
        o->box[0].sprite = 0xAC;
        o->box[0].dx = 0;
        o->box[0].dy = 0;
        o->tick_period = 1000;
        stamp_object(o);
        /* fallthrough */
    case 2:
        boss_helper();
        break;
    case 4: case 6:
        ai_result |= 1;
        break;
    }
}
