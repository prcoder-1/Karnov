/* Модуль 0x151A (file 0x169A0) — AI-типы 33–38: подъёмная лестница,
 * змей со следом, колонна-пресс, прыгуны и двое боссов. */
#include <string.h>
#include "orig/fn.h"


/* @151A:0002 file:169A2 col_mark — атрибут клеток колонки объекта от
 * верхней части до y */
static void col_mark(s16 attr)
{
    ActiveObject *o = cur_obj;
    s16 si, di, col;
    di = PX_TROW(o->box[o->num_bboxes - 1].dy + o->y_world);
    col = PX_COL(cur_obj->x_world >> tile_shift);
    for (si = PX_TROW(cur_obj->y_world) - 1; si >= di; si--)
        map_set_attr(col, si, attr);
}

/* @151A:0071 file:16A11 row_mark — атрибут 13 клеток строки справа */
static void row_mark(s16 attr)
{
    s16 si, di, row;
    di = PX_COL(cur_obj->x_world >> tile_shift) + 0xD;
    row = PX_TROW(cur_obj->y_world);
    for (si = PX_COL(cur_obj->x_world >> tile_shift) + 1; si <= di; si++)
        map_set_attr(si, row, attr);
}

/* @151A:00CD file:16A6D ai_t33 — лестница из 7 частей; ранена — части
 * съезжают вниз, лестница становится мостом (row_mark) */
void ai_t33(void)
{
    ActiveObject *o;
    SpriteBox *b;
    s16 si, di, n;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->misc = 0;
        o->phase_ctr = o->hit_points;
        o->active = 0;
        o->num_bboxes = 7;
        si = di = 0;
        for (b = &o->box[0]; cur_obj->num_bboxes > si; si++, b++) {
            if (si == 0) {
                b->sprite = 0xE6;
                b->dx = -4;
                b->hitflag = 1;
            } else {
                b->sprite = 0xE7;
                b->dx = -2;
                b->hitflag = 0;
            }
            di -= sprite_h[b->sprite];
            b->dy = di + 2;
        }
        break;
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            stamp_object(cur_obj);
            col_mark(7);
            /* fallthrough */
        case 1:
            cur_obj->hit_points = cur_obj->phase_ctr;
            break;
        case 2:
            unstamp_object(cur_obj);
            col_mark(2);
            row_mark(0);
            ladders_mark();
            /* fallthrough */
        case 3:
            n = 0;
            o = cur_obj;
            o->box[0].sprite = 0xE8;
            o->box[0].hitflag = 0;
            for (si = 1, b = &o->box[1]; cur_obj->num_bboxes > si; si++, b++) {
                if (b->dy != 0) {
                    n++;
                    b->dy += 2;
                    b->dx += 2;
                }
            }
            if (n == 0)
                cur_obj->misc = 4;
            break;
        case 4:
            stamp_object(cur_obj);
            break;
        }
        break;
    case 4:
        o = cur_obj;
        if (!(o->misc & 0xFFFE))
            o->misc = 2;
        cur_obj->state_byte = 2;
        break;
    case 6:
        col_mark(2);
        ai_result |= 1;
        break;
    }
}

/* @151A:02A6 file:16C46 ai_t34 — змей из 6 частей: голова (box[5])
 * движется по an_1BFE/an_1C0C, тело (box[0..4]) — по следу t34_trail;
 * части гибнут с хвоста */
void ai_t34(void)
{
    ActiveObject *o;
    SpriteBox *b, *p;
    s16 si, *q;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->active = 0;
        o->num_bboxes = 6;
        o->misc = 0;
        o->phase = o->hit_points;
        o->phase_ctr = 5;
        o->aux = 0;
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            for (si = 0, b = &cur_obj->box[0]; si < 6; si++, b++) {
                b->sprite = 0xEE - si;
                b->hitflag = 0;
                b->dx = 0;
                b->dy = an_1C14[si];
            }
            for (si = 0; si < 0x14; si++) {
                t34_trail[si * 2 + 1] = 0;
                t34_trail[si * 2] = 0;
            }
            /* fallthrough */
        case 1:
            if (ABS16(player_ptr->x_world - cur_obj->x_world) < 0x64)
                cur_obj->misc = 2;
            break;
        case 2:
            o = cur_obj;
            o->box[5].hitflag = 1;
            o->frame_ctr = 1000;
            o->aux = 5;
            /* fallthrough */
        case 3:
            o = cur_obj;
            if (o->aux < 0 && o->frame_ctr > 0x1E) {
                o->frame_ctr = 0;
                o->aux = -o->aux;
            }
            /* при aux < 0 оригинал читает перед таблицей far-указатели —
             * значения больше любого frame_ctr здесь */
            if (cur_obj->aux >= 0 && an_1BF0[cur_obj->aux] < cur_obj->frame_ctr) {
                o = cur_obj;
                o->frame_ctr = 0;
                if (++o->aux == 6)
                    o->aux = 0;
                o->aux = -o->aux;
            }
            memmove(&t34_trail[0], &t34_trail[2], 0x4C);
            p = &cur_obj->box[5];
            q = &t34_trail[38];
            q[0] = p->dx;
            q[1] = p->dy;
            if (cur_obj->aux >= 0) {
                p->dx += an_1C0C[an_1BFE[cur_obj->aux] & 3];
                p->dy += an_1C0C[(an_1BFE[cur_obj->aux] & 0xC) >> 2];
            }
            for (si = 0, b = &cur_obj->box[0]; si < 5; si++, b++) {
                q = &t34_trail[si * 8];
                b->dx = q[0];
                b->dy = q[1];
            }
            break;
        }
        break;
    case 4:
        o = cur_obj;
        o->hit_points = o->phase;
        o->box[o->phase_ctr].sprite = 0;
        cur_obj->box[cur_obj->phase_ctr].hitflag = 0;
        o = cur_obj;
        if (o->phase_ctr != 0) {
            o->phase_ctr--;
            o->box[o->phase_ctr].hitflag = 1;
            cur_obj->state_byte = 2;
        } else {
            cur_obj->state_byte = 6;
        }
        cur_state = cur_obj->state_byte;
        ai_dispatch();
        add_score(0x110);
        break;
    case 6:
        o = cur_obj;
        o->x_world += o->box[0].dx;
        o->y_world += o->box[0].dy;
        add_score(0x110);
        die_puff();
        break;
    }
}

/* @151A:061B file:16FBB ai_t36 — пресс: опора (box[1]) твёрдая
 * (stamp_solid); ранен — опускается до исходной высоты */
void ai_t36(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->misc = 0;
        o->phase = 0;
        o->num_bboxes = 2;
        o->x_world &= ~7;
        o->box[0].sprite = 0xF5;
        o->box[0].dy = 0;
        o->box[0].dx = 2;
        o->box[0].hitflag = 0;
        o->box[1].sprite = 0xF9;
        o->box[1].dx = 0;
        o->box[1].hitflag = 1;
        o->phase_ctr = sprite_h[o->box[0].sprite] + o->y_world + sprite_h[o->box[1].sprite];
        o->box[1].dy = sprite_h[o->box[0].sprite];
        stamp_solid(cur_obj, cur_obj->y_world + cur_obj->box[0].dy, cur_obj->phase_ctr, 1);
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 2:
            cur_obj->num_bboxes = 1;
            cur_obj->vy_hi = 1;
            /* fallthrough */
        case 3:
            o = cur_obj;
            stamp_solid(o, o->y_world + o->box[0].dy, o->phase_ctr, 1);
            o = cur_obj;
            if (sprite_h[o->box[0].sprite] + o->y_world >= o->phase_ctr) {
                o->vy_hi = 0;
                o->misc = 0;
                stamp_object(cur_obj);
            }
            break;
        }
        break;
    case 5:
        if (all_objects[cur_obj->hit_idx].obj_type == 2) {
            o = cur_obj;
            o->phase++;
            si = (o->phase >> 1) - 1;
            (void)si;           /* в оригинале вычисляется и не используется */
            o->box[1].sprite = 0xFA;
            o->box[1].dx = 3;
        }
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

/* @151A:07FA file:1719A t35_land — приземление на атрибуты 0, 3, 4, 7 */
static s16 t35_land(void)
{
    ActiveObject *o = cur_obj;
    if (o->vy_hi <= 0)
        return 0;
    switch (attr_at(o->x_world, sprite_h[o->box[0].sprite] + o->y_world)) {
    case 0: case 3: case 4: case 7:
        place_on_ground_center(cur_obj, 0xEF);
        set_motion(0);
        return 1;
    }
    return 0;
}

/* @151A:0870 file:17210 t35_shoot */
static void t35_shoot(void)
{
    if (cur_obj->phase != 0)
        spawn_projectile(cur_obj->x_world, cur_obj->y_world, player_ptr->x_world,
                         player_ptr->y_world, 1, 0x24000L, 0);
}

/* @151A:08B0 file:17250 ai_t35 — прыгун: оживает рядом с игроком или
 * раненым, прыгает (phase — со стрельбой) */
void ai_t35(void)
{
    ActiveObject *o;
    s16 si, di, dir;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->misc = 0;
        o->phase_ctr = 0;
        o->phase = o->anim_len > 0xA ? 1 : 0;
        o->state_list = sl_1C40;
        place_on_ground_center(o, 0xF5);
        /* fallthrough */
    case 2:
        o = cur_obj;
        di = o->misc;
        o->misc |= 1;
        si = player_ptr->x_world - cur_obj->x_world;
        if (ABS16(si) > 0x1E)
            dir = si > 0 ? 1 : 0;
        else
            dir = rtl_rand() & 1;
        switch (di) {
        case 0:
            o = cur_obj;
            o->frame_idx = 0;
            o->active = 0;
            pending_state = 0;
            ai_result |= 6;
            /* fallthrough */
        case 1:
            if (cur_obj->hit_points < 0x46 || ABS16(si) < 0x3C)
                cur_obj->active = 1;
            if (cur_obj->frame_idx == an_1C20[0] - 1)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = dir + 1;
            ai_result |= 2;
            place_on_ground_center(cur_obj, 0xEF);
            if (rtl_rand() & 3)
                SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, 0);
            else
                SETLONG(cur_obj->vx_hi, cur_obj->vx_lo,
                        dir != 0 ? (s32)1 << 16 : -((s32)1 << 16));
            o = cur_obj;
            o->vy_hi = -2;
            o->vy_lo = 0xC000;
            o->ay_hi = 0;
            o->ay_lo = 0x2000;
            t35_shoot();
            /* fallthrough */
        case 3:
            if (t35_land())
                cur_obj->misc = 6;
            break;
        case 6:
            pending_state = dir + 3;
            ai_result |= 2;
            o = cur_obj;
            o->vy_hi = -2;
            o->vy_lo = 0xC000;
            o->ay_hi = 0;
            o->ay_lo = 0x2000;
            /* fallthrough */
        case 7:
            o = cur_obj;
            if (o->vy_hi < 0 && player_ptr->y_world - o->y_world > 0x19) {
                SETLONG(o->vy_hi, o->vy_lo, 0);
                o->ay_hi = 0;
                o->ay_lo = 0x2000;
                t35_shoot();
            }
            if (t35_land())
                cur_obj->misc = 2;
            break;
        }
        stop_at_wall();
        ai_result |= 4;
        break;
    case 4:
        add_score(0x800);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @151A:0B18 file:174B8 t37_shoot */
static void t37_shoot(void)
{
    play_sound(2);
    spawn_projectile(cur_obj->x_world, cur_obj->y_world + 7, player_ptr->x_world,
                     cur_obj->box[0].y1 - 4, 4, 0x20000L, 0);
}

/* @151A:0B60 file:17500 boss_burst — гибель босса: 4 «пуфа» по an_1C68 */
static void boss_burst(void)
{
    ActiveObject *o;
    s16 si;
    for (si = 0; si < 4; si++) {
        o = new_object(1);
        if (o == NULL)
            continue;
        o->x_world = an_1C68[si * 2] + cur_obj->x_world;
        o->y_world = an_1C68[si * 2 + 1] + cur_obj->y_world;
        o->box[0].hitflag = 0;
        queue_state(o, puff_handler);
        o->ai_init = remove_object;
        o->phase = si == 0 ? 1 : 0;
    }
    play_sound(1);
    ai_result |= 1;
}

/* @151A:0C31 file:175D1 ai_t37 — босс: идёт к игроку, стреляя сериями
 * по 6; урон больше 70 за тик гасится */
void ai_t37(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 7;
        o->misc = 0;
        o->phase_ctr = 0;
        o->phase = o->hit_points;
        o->state_list = sl_1C86;
        place_on_ground_center(o, 0xFB);
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        if (o->phase - o->hit_points > 0x46)
            o->hit_points = -0x14;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            cur_obj->box[0].hitflag = 0;
            /* fallthrough */
        case 1:
            if (dist_to_player() < 0x55)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = 1;
            ai_result |= 2;
            o = cur_obj;
            o->box[0].hitflag = 1;
            o->vx_hi = -1;
            o->vx_lo = 0x8000;
            /* fallthrough */
        case 3:
            if ((frame_no & 4) && cur_obj->phase_ctr < 6) {
                t37_shoot();
                o = cur_obj;
                o->phase_ctr++;
                o->frame_ctr = 0;
                if (o->hit_points < 0xFA)
                    o->hit_points += 0xA;
            }
            o = cur_obj;
            if (o->phase_ctr == 6 && o->frame_ctr > 0x14)
                o->phase_ctr = 0;
            if (cur_obj->x_world <= player_ptr->x_world) {
                o = cur_obj;
                SETLONG(o->vx_hi, o->vx_lo, 0);
                o->active = 0;
                o->phase_ctr = -0x19;
            }
            break;
        }
        stop_at_wall();
        cur_obj->phase = cur_obj->hit_points;
        ai_result |= 4;
        break;
    case 4:
        add_score(0x12000L);
        /* fallthrough */
    case 6:
        boss_burst();
        break;
    }
}

/* @151A:0DDF file:1777F t38_shoot — прицельно (вид 7) или над игроком */
static void t38_shoot(void)
{
    if (rtl_rand() & 1)
        spawn_projectile(cur_obj->x_world, cur_obj->y_world + 7, player_ptr->x_world,
                         player_ptr->y_world, 7, 0x20000L, 1);
    else
        spawn_projectile(cur_obj->x_world, cur_obj->y_world + 7, player_ptr->x_world + 0xF,
                         player_ptr->y_world + 5, 1, 0x18000L, 0);
}

/* @151A:0E5D file:177FD t38_land — приземление на всё, кроме 0, 4, 7 */
static void t38_land(void)
{
    ActiveObject *o = cur_obj;
    if (o->vy_hi <= 0)
        return;
    switch (attr_at(o->x_world - 0x10, sprite_h[o->box[0].sprite] + o->y_world)) {
    case 0: case 4: case 7:
        break;
    default:
        set_motion(0);
        cur_obj->misc = 0;
        break;
    }
}

/* @151A:0EC7 file:17867 ai_t38 — босс-прыгун: ждёт игрока, прыгает
 * влево со стрельбой */
void ai_t38(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0:
        o = cur_obj;
        o->tick_period = 7;
        o->misc = 0;
        o->phase = o->hit_points;
        o->aux = 0;
        o->state_list = sl_1CA4;
        o->box[0].hitflag = 0;
        o->frame_ctr = 0x4B;
        /* fallthrough */
    case 2:
        o = cur_obj;
        o->aux |= dist_to_player() < 0x64 ? 1 : 0;
        o = cur_obj;
        if (o->phase - o->hit_points > 0x46)
            o->hit_points = -0x14;
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            place_on_ground_center(cur_obj, 0x101);
            /* fallthrough */
        case 1:
            o = cur_obj;
            if (o->aux != 0 && o->frame_ctr > 0x4B)
                o->misc = 2;
            break;
        case 2:
            pending_state = 1;
            ai_result |= 2;
            o = cur_obj;
            o->frame_ctr = 0;
            o->box[0].hitflag = 1;
            o->vx_hi = -2;
            o->vx_lo = 0x8000;
            o->vy_hi = -2;
            o->vy_lo = 0;
            o->ay_hi = 0;
            o->ay_lo = 0x4000;
            t38_shoot();
            /* fallthrough */
        case 3:
            if (cur_obj->x_world <= player_ptr->x_world)
                SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, 0);
            t38_land();
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        partner_kill();
        add_score(0x20000L);
        /* fallthrough */
    case 6:
        boss_burst();
        break;
    }
}
