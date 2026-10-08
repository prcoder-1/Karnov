/* Модуль 0x1348 (file 0x14C80) — AI-типы 20–32: колонна голов, золотой
 * человек с шарами, арена (тип 30) и др. */
#include "orig/fn.h"
#include "orig/todo.h"

#define BYTE_AT(arr, i) (((const u8 *)(arr))[i])   /* байтовое чтение таблицы */

/* @1348:0007 file:14C87 ai_t23 — летит горизонтально (±3), исчезает,
 * покинув экран после появления */
void ai_t23(void)
{
    ActiveObject *o;
    s16 si = player_ptr->x_world - cur_obj->x_world;
    s16 di = si > 0 ? 1 : 0;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 2;
        o->box[0].hitflag = 1;
        o->box[0].sprite = 0xB9;
        SETLONG(o->vx_hi, o->vx_lo, di != 0 ? (s32)3 << 16 : -((s32)3 << 16));
        o->phase = 0;
        break;
    case 2:
        if (cur_obj->phase != 0) {
            if (!box_visible(&cur_obj->box[0]))
                ai_result |= 1;
        } else if (box_visible(&cur_obj->box[0])) {
            cur_obj->phase = 1;
        }
        break;
    case 4: case 6:
        ai_result |= 1;
        break;
    }
}

/* @1348:00D7 file:14D57 gold_ball — шар золотого человека: misc — этап
 * (0 вылет к точке an_19F4[phase], 2 пауза, 4 возврат), phase_ctr —
 * тики этапа */
void gold_ball(void)
{
    ActiveObject *o, *p;
    s16 si;
    s32 vx, vy;
    switch (cur_state) {
    case 1:
        gold_balls++;
        o = cur_obj;
        o->num_bboxes = 1;
        o->obj_type = 1;
        o->hit_points = 0xA;
        o->state_list = sl_1A16;
        pending_state = 0;
        ai_result |= 6;
        break;
    case 2:
        o = cur_obj;
        o->phase_ctr++;
        si = o->misc;
        o->misc |= 1;
        if (!(si & 1))
            o->phase_ctr = 0;
        switch (si) {
        case 0:
            o = cur_obj;
            p = o->parent;
            aim(p->x_world, p->y_world, an_19F4[o->phase * 2] + p->x_world,
                an_19F4[o->phase * 2 + 1] + p->y_world, &vx, &vy, 0xA);
            SETLONG(o->vx_hi, o->vx_lo, vx);
            SETLONG(o->vy_hi, o->vy_lo, vy);
            cur_obj->box[0].hitflag = 1;
            /* fallthrough */
        case 1:
            if (cur_obj->phase_ctr >= 0xA)
                cur_obj->misc = 2;
            break;
        case 2:
            set_motion(0);
            cur_obj->num_bboxes = 0;
            /* fallthrough */
        case 3:
            if (cur_obj->phase_ctr >= 0xF)
                cur_obj->misc = 4;
            break;
        case 4:
            o = cur_obj;
            p = o->parent;
            aim(o->x_world, o->y_world, (sprite_w[0xBB] >> 1) + p->x_world,
                (sprite_h[0xBB] >> 1) + p->y_world, &vx, &vy, 0xA);
            SETLONG(o->vx_hi, o->vx_lo, vx);
            SETLONG(o->vy_hi, o->vy_lo, vy);
            o->num_bboxes = 1;
            o->box[0].hitflag = 0;
            /* fallthrough */
        case 5:
            if (cur_obj->phase_ctr >= 0xA)
                cur_obj->state_byte = 6;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x500);
        /* fallthrough */
    case 6:
        gold_balls--;
        ai_result |= 1;
        break;
    }
}

/* @1348:02D7 file:14F57 gold_spawn_ball — шар n из центра cur_obj (при
 * misc 4 — сразу в точке an_19F4[n]) */
static void gold_spawn_ball(s16 n, s16 misc)
{
    ActiveObject *o;
    alloc_idx = alloc_object(1);
    o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
    if (o == NULL)
        return;
    queue_state(o, (farfn)gold_ball);
    o->parent = cur_obj;
    o->misc = misc;
    o->phase = n;
    o->x_world = (sprite_w[0xBB] >> 1) + cur_obj->x_world;
    o->y_world = (sprite_h[0xBB] >> 1) + cur_obj->y_world;
    if (misc == 4) {
        o->x_world += an_19F4[n * 2];
        o->y_world += an_19F4[n * 2 + 1];
    }
}

/* @1348:03A9 file:15029 ai_t24 — золотой человек: прыгает к игроку,
 * после возврата всех шаров выпускает новые 7 */
void ai_t24(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        gold_balls = 0;
        place_on_ground(cur_obj, 0xBB, 0);
        for (si = 0; si < 7; si++)
            gold_spawn_ball(si, 4);
        o = cur_obj;
        o->misc = 4;
        o->hit_points = 0x5A;
        o->state_list = sl_1A2C;
        pending_state = 0;
        ai_result |= 6;
        break;
    case 2:
        o = cur_obj;
        o->phase_ctr++;
        si = o->misc;
        o->misc |= 1;
        if (!(si & 1)) {
            o->phase_ctr = 0;
            cur_obj->box[0].hitflag = si == 6 ? 1 : 0;
        }
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            /* fallthrough */
        case 1:
            if (cur_obj->phase_ctr >= 0xA)
                cur_obj->misc = 2;
            break;
        case 2:
            cur_obj->vx_hi = player_ptr->x_world > cur_obj->x_world ? 2 : -2;
            /* fallthrough */
        case 3:
            if (cur_obj->phase_ctr >= 0xF)
                cur_obj->misc = 4;
            o = cur_obj;
            if (attr_at(o->x_world, o->box[0].y1 + 2) == 2)
                SETLONG(o->vx_hi, o->vx_lo, 0);
            break;
        case 4:
            set_motion(0);
            /* fallthrough */
        case 5:
            if (gold_balls == 0 && cur_obj->phase_ctr > 2)
                cur_obj->misc = 6;
            break;
        case 6:
            pending_state = 1;
            ai_result |= 2;
            /* fallthrough */
        case 7:
            if (cur_obj->phase_ctr < 0x26)
                break;
            cur_obj->box[0].hitflag = 0;
            cur_obj->hit_points = 0x5A;
            for (si = 0; si < 7; si++)
                gold_spawn_ball(si, 0);
            cur_obj->misc = 0;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x7000);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @1348:059E file:1521E col_settle — головы колонны (box[n..0]) оседают на
 * опору y не быстрее 6 за тик; 1 — что-то сдвинулось */
static s16 col_settle(s16 y, s16 n)
{
    SpriteBox *b;
    s16 si, di, top;
    if (n < 0)
        return 0;
    b = &cur_obj->box[n];
    si = cur_obj->y_world + b->dy;
    top = (b->sprite != 0 ? 0x14 : 0) + si;
    di = y - top < 6 ? y - top : 6;
    if (di != 0) {
        b->dy += di;
        col_settle(si + di, n - 1);
        return 1;
    }
    return col_settle(si, n - 1);
}

/* @1348:0638 file:152B8 col_hit_part — попадание в голову n: счётчик
 * (2 бита на голову в phase_ctr), на третьем голова снимается */
static void col_hit_part(s16 n)
{
    s16 di = n * 2, si;
    SpriteBox *b = &cur_obj->box[n];
    si = ((cur_obj->phase_ctr >> di) & 3) + 1;
    switch (si) {
    case 1: case 2:
        if (b->sprite != 0xAD)
            b->sprite = si + 0xAF;
        break;
    case 3:
        b->sprite = 0;
        b->hitflag = 0;
        add_score(0x400);
        break;
    }
    cur_obj->phase_ctr = (s16)(((u16)0xFFFC << di) & (u16)cur_obj->phase_ctr) |
                         (s16)((si & 3) << di);
}

/* @1348:06DD file:1535D ai_t20 — колонна из frame_idx голов: верхняя
 * (0xAD) стреляет двумя снарядами; колонна твёрдая (stamp_solid) */
void ai_t20(void)
{
    ActiveObject *o;
    SpriteBox *b;
    s16 si, di, r, live, y0, y1;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->active = 0;
        o->obj_type = 9;
        o->num_bboxes = o->frame_idx;
        o->x_world = (o->x_world & ~7) + 2;
        for (si = 0, b = &o->box[0]; cur_obj->num_bboxes > si; si++, b++) {
            b->hitflag = 1;
            b->dx = 0;
            b->dy = si * 0x14;
            b->sprite = si != 0 ? 0xAF : 0xAD;
        }
        update_bboxes(cur_obj);
        o = cur_obj;
        o->phase_ctr = 0;
        o->misc = 0;
        o->phase = 0;
        /* fallthrough */
    case 2:
        o = cur_obj;
        o->hit_points = 0xFA;
        o->misc -= o->misc != 0;
        o->box[0].sprite = o->box[0].sprite != 0 ? 0xAD : 0;
        if (any_box_visible(cur_obj) && cur_obj->box[0].sprite != 0 && cur_obj->misc == 0) {
            play_sound(2);
            cur_obj->box[0].sprite = 0xAE;
            spawn_projectile(cur_obj->box[0].x0, cur_obj->box[0].y0,
                             player_ptr->x_world - 0x14, player_ptr->y_world, 1, 0x40000L, 0);
            spawn_projectile(cur_obj->box[0].x0, cur_obj->box[0].y0,
                             player_ptr->x_world + 5, player_ptr->y_world, 1, 0x40000L, 0);
            cur_obj->misc = (rtl_rand() & 0x1F) + 0x96;
        }
        o = cur_obj;
        di = o->num_bboxes - 1;
        r = col_settle(o->num_bboxes * 0x14 + o->y_world, di);
        live = (u8)count_live_parts(cur_obj);
        if (!(r == 0 && cur_obj->aux != 0) && cur_obj->phase == live)
            break;
        update_bboxes(cur_obj);
        o = cur_obj;
        y0 = (o->box[0].sprite != 0 ? 0x14 : 0) + o->box[0].y0;
        y1 = o->box[di].y0 + (o->box[di].sprite != 0 ? 0x14 : 0);
        stamp_solid(cur_obj, y0, y1, 2);
        cur_obj->aux = r;
        cur_obj->phase = live;
        break;
    case 5:
        o = cur_obj;
        o->hit_flag = 0;
        if (all_objects[o->hit_idx].active_pos == player_ptr->active_pos) {
            if (cur_obj->hit_box_self == 0) {
                die_hit();
                break;
            }
            cur_obj->state_byte = 2;
            handoff_obj = cur_obj;
            ai_result |= 8;
            break;
        }
        col_hit_part(cur_obj->hit_box_self);
        break;
    case 4: case 6:
        ai_result |= 1;
        break;
    }
}

/* @1348:09EC file:1566C bounce_clamp — v, отражённая у границ [lo, hi] */
static s16 bounce_clamp(s16 pos, s16 v, s16 lo, s16 hi)
{
    if (v > 0)
        return pos + v > hi ? -v : v;
    return pos + v < lo ? -v : v;
}

/* @1348:0A20 file:156A0 ai_t25 — летает в пределах экрана, каждые 30–45
 * тиков меняя направление (к игроку или случайно по an_1A68) */
void ai_t25(void)
{
    ActiveObject *o;
    s16 si, di, t;
    switch (cur_state) {
    case 0:
        t25_count++;
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->state_list = sl_1A58;
        set_motion(4);
        /* fallthrough */
    case 2:
        o = cur_obj;
        o->hit_flag = 0;
        o->box[0].hitflag = 1;
        if ((rtl_rand() & 0xF) + 0x1E < cur_obj->frame_ctr) {
            si = rtl_rand() & 1;
            if (si == 0)
                di = player_ptr->x_world > cur_obj->x_world ? 1 : 2;
            else
                di = BYTE_AT(an_1A68, rtl_rand() & 3);
            if (si != 0)
                t = player_ptr->y_world > cur_obj->y_world ? 4 : 8;
            else
                t = BYTE_AT(an_1A68, rtl_rand() & 3) << 2;
            set_motion(di + t);
            o = cur_obj;
            o->vx_hi = bounce_clamp(o->x_world, o->vx_hi, clip_rect[0] << tile_shift,
                                    clip_rect[2] << tile_shift);
            o->vy_hi = bounce_clamp(o->y_world, o->vy_hi, clip_rect[1], clip_rect[3]);
            o->frame_ctr = 0;
        }
        pending_state = cur_obj->vx_hi > 0 ? 1 : 0;
        ai_result |= 6;
        break;
    case 4:
        add_score(0x110);
        /* fallthrough */
    case 6:
        t25_count--;
        ai_result |= 1;
        break;
    }
}

/* @1348:0BB2 file:15832 t22_drop — предмет 9, выпавший из тип 22: падает
 * до phase_ctr */
void t22_drop(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        o = cur_obj;
        o->obj_type = 5;
        o->phase = 0;
        o->misc = 9;
        o->num_bboxes = 1;
        o->box[0].sprite = 1;
        o->box[0].dx = 0;
        o->box[0].dy = 0;
        o->box[0].hitflag = 1;
        o->vy_hi = 1;
        o->vy_lo = 0x8000;
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->phase == 0 && o->y_world >= o->phase_ctr) {
            o->phase = 1;
            o->box[0].sprite = 0x30;
            o->y_world = o->phase_ctr;
            SETLONG(o->vy_hi, o->vy_lo, 0);
        }
        break;
    case 4:
        play_sound(5);
        item_pickup(9);
        /* fallthrough */
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @1348:0C82 file:15902 ai_t22 — неподвижный; убит — стреляет и роняет
 * предмет 9 на землю под собой */
void ai_t22(void)
{
    ActiveObject *o, *n;
    s16 si, di;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->phase = o->frame_idx;
        o->state_list = sl_1A78;
        pending_state = o->phase;
        di = PX_COL(o->x_world >> tile_shift);
        si = PX_TROW(cur_obj->y_world);
        while (!(ATTR_PROP(map_attr(di, si)) & 1))
            si++;
        cur_obj->phase_ctr = si * 0xA;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        ai_result |= 4;
        break;
    case 4:
        add_score(0x500);
        /* fallthrough */
    case 5:
        spawn_projectile(cur_obj->box[0].x0 + 2, cur_obj->box[0].y0 + 0x19,
                         player_ptr->x_world, player_ptr->y_world + 0xA, 2, 0x80000L, 1);
        alloc_idx = alloc_object(1);
        n = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
        if (n != NULL) {
            queue_state(n, (farfn)t22_drop);
            n->x_world = cur_obj->x_world;
            n->y_world = cur_obj->y_world + 5;
            cur_obj->phase_ctr -= 0xA;
            n->phase_ctr = cur_obj->phase_ctr;
        }
        /* fallthrough */
    case 6:
        if (cur_state == 4 || cur_state == 6)
            ai_result |= 1;
        break;
    }
}

/* @1348:0E0B file:15A8B attr_ahead — атрибут клетки перед ногами */
static s16 attr_ahead(void)
{
    ActiveObject *o = cur_obj;
    return attr_at(o->x_world + o->vx_hi + 3,
                   o->y_world + o->vy_hi + sprite_h[o->box[0].sprite] + 1);
}

/* @1348:0E46 file:15AC6 ai_t26 — прыгун по ступеням: misc — этап (0, 1
 * полёт, 2, 3 на земле, 4, 5 по склону) */
void ai_t26(void)
{
    ActiveObject *o;
    s16 si, d;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 2;
        o->state_list = sl_1AA4;
        o->misc = 2;
        o->phase = player_ptr->x_world > o->x_world ? 1 : 0;
        SETLONG(o->vx_hi, o->vx_lo, o->phase != 0 ? 0xC000L : (s32)0xFFFF4000L);
        /* fallthrough */
    case 2:
        if (cur_obj->phase != 0) {
            d = -0xA - (rtl_rand() & 7);
            if (d > player_ptr->x_world - cur_obj->x_world)
                cur_obj->phase = 0;
        } else {
            d = (rtl_rand() & 7) + 0xA;
            if (d < player_ptr->x_world - cur_obj->x_world)
                cur_obj->phase = 1;
        }
        o = cur_obj;
        o->vx_hi = o->phase != 0 ? 1 : -1;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            cur_obj->ay_hi = 0;
            cur_obj->ay_lo = 0x5000;
            /* fallthrough */
        case 1:
            if (cur_obj->vy_hi <= 0)
                break;
            switch ((u8)attr_ahead()) {
            case 0: case 4:
                cur_obj->misc = 2;
                break;
            case 6:
                cur_obj->misc = 4;
                break;
            }
            break;
        case 2:
            o = cur_obj;
            SETLONG(o->vy_hi, o->vy_lo, 0);
            SETLONG(o->ay_hi, o->ay_lo, 0);
            snap_to_ground(cur_obj, 0xC1, 0x41,
                           PX_COL((cur_obj->x_world + 3) >> tile_shift),
                           PX_TROW(o->y_world) - 1);
            /* fallthrough */
        case 3:
            if ((rtl_rand() & 0xF) < 2) {
                cur_obj->vy_hi = -2;
                cur_obj->vy_lo = 0;
                cur_obj->misc = 0;
                break;
            }
            switch ((u8)attr_ahead()) {
            case 2:
                cur_obj->misc = 0;
                break;
            case 6:
                cur_obj->misc = 4;
                break;
            }
            break;
        case 4:
            o = cur_obj;
            SETLONG(o->vy_hi, o->vy_lo,
                    (s32)((u32)LONG(o->vx_hi, o->vx_lo) * 0xAU) / 0x10);
            SETLONG(o->ay_hi, o->ay_lo, 0);
            snap_to_ground(cur_obj, 0xC1, 0x41,
                           PX_COL((cur_obj->x_world + 3) >> tile_shift),
                           PX_TROW(o->y_world) - 1);
            /* fallthrough */
        case 5:
            switch ((u8)attr_ahead()) {
            case 0: case 4: case 7:
                cur_obj->misc = 2;
                break;
            case 2:
                cur_obj->misc = 0;
                break;
            case 6:
                cur_obj->misc = 4;
                break;
            }
            break;
        }
        pending_state = (cur_obj->misc & 0xFE ? 0 : 2) + cur_obj->phase;
        ai_result |= 6;
        break;
    case 4:
        add_score(0x400);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @1348:1153 file:15DD3 ai_t21 — неподвижная стена из 7 частей; на
 * время жизни клетки под ней — стена (3) */
void ai_t21(void)
{
    ActiveObject *o;
    SpriteBox *b;
    s16 si, di, t, col, row;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->num_bboxes = 7;
        o->active = 0;
        b = &o->box[0];
        b->dx = 2;
        b->dy = -3;
        b->sprite = 0xB2;
        b->hitflag = 1;
        b = &cur_obj->box[1];
        for (t = 0x10; t < 0x90; t += 0x40)
            for (si = -0x1E; si < 0x3C; si += 0x1E, b++) {
                b->dx = t;
                b->dy = si;
                b->sprite = 0xB4;
                b->hitflag = 0;
            }
        col = PX_COL(cur_obj->x_world >> tile_shift);
        row = PX_TROW(cur_obj->y_world + sprite_h[0xB2]);
        for (di = 0; di < 6; di++)
            map_set_attr(col, row - di, 3);
        stamp_object(cur_obj);
        break;
    case 5:
        o = cur_obj;
        if (o->hit_points < 0x32 && o->box[0].sprite != 0xB3) {
            o->box[0].sprite = 0xB3;
            o->box[0].was_vis = 0;
        }
        break;
    case 4: case 6:
        col = PX_COL(cur_obj->x_world >> tile_shift);
        row = PX_TROW(cur_obj->y_world + sprite_h[0xB2]);
        for (di = 0; di < 6; di++)
            map_set_attr(col, row - di, 2);
        ai_result |= 1;
        break;
    }
}

/* @1348:12CF file:15F4F ai_t30 — арена: две стены (колонки x и x+0x98)
 * растут по 2 части; камера зажата видом; конец — когда погибли все
 * тип 25, появившиеся после старта */
void ai_t30(void)
{
    ActiveObject *o;
    SpriteBox *b;
    s16 si, di, k, row;
    switch (cur_state) {
    case 0: case 1:
        t25_count = 0;
        o = cur_obj;
        o->phase = 0;
        o->misc = 0;
        o->phase_ctr = player_ptr->x_world;
        o->num_bboxes = 0;
        for (si = 0, b = &o->box[0]; si < 6; si++, b++) {
            b->dx = si & 1 ? 0x98 : 5;
            b->dy = (si >> 1) * 0x14;
            b->sprite = 0xDA;
            b->hitflag = 0;
        }
        row = PX_TROW(cur_obj->y_world);
        for (k = 0; k < 2; k++) {
            di = PX_COL(((k != 0 ? 0x98 : 0) + cur_obj->x_world) >> tile_shift);
            for (si = 0; si < 6; si++)
                map_set_attr(di, row + si, 3);
        }
        rtl_fmemcpy(cam_saved, cam_rect, 8);
        rtl_fmemcpy(cam_rect, view_rect, 8);
        stamp_object(cur_obj);
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->num_bboxes != 6 && ++o->misc > 0xA) {
            for (si = 0; si < 2; si++)
                cur_obj->box[cur_obj->num_bboxes + si].was_vis = 0;
            cur_obj->num_bboxes += 2;
            cur_obj->misc = 0;
        }
        o = cur_obj;
        o->phase |= t25_count;
        if (o->phase != 0 && t25_count == 0)
            o->state_byte = 6;
        break;
    case 6:
        row = PX_TROW(cur_obj->y_world);
        for (k = 0; k < 2; k++) {
            di = PX_COL(((k != 0 ? 0x98 : 0) + cur_obj->x_world) >> tile_shift);
            for (si = 0; si < 6; si++)
                map_set_attr(di, row + si, TILE_ATTR(map_tile(di, row + si)));
        }
        rtl_fmemcpy(cam_rect, cam_saved, 8);
        for (si = 0; si < 4; si++)
            cam_hist[si * 2] = cur_obj->phase_ctr;
        ai_result |= 1;
        break;
    }
}

/* @1348:1531 file:161B1 ai_t29 — подпрыгивает к игроку (ближе 50), на
 * земле стреляет; misc — этап */
void ai_t29(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 8;
        o->misc = 0;
        o->state_list = sl_1AF0;
        place_on_ground(o, 0xD3, 0);
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            /* fallthrough */
        case 1:
            if (dist_to_player() < 0x32)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = 1;
            ai_result |= 2;
            o = cur_obj;
            o->vx_hi = 1;
            o->vx_lo = 0;
            o->vy_hi = -3;
            o->vy_lo = 0x8000;
            o->ay_hi = 0;
            o->ay_lo = 0x2100;
            /* fallthrough */
        case 3:
            o = cur_obj;
            if (o->vy_hi <= 0)
                break;
            switch (attr_at(o->x_world, sprite_h[o->box[0].sprite] + o->y_world)) {
            case 0: case 4: case 7:
                goto land;
            }
            break;
        case 4:
            pending_state = 2;
            ai_result |= 2;
            /* fallthrough */
        case 5:
            spawn_projectile(cur_obj->x_world, cur_obj->y_world, player_ptr->x_world,
                             player_ptr->y_world, 0, 0x20000L, 1);
            cur_obj->misc = 6;
            break;
        case 6:
            pending_state = 3;
            ai_result |= 2;
            cur_obj->vy_hi = -3;
            cur_obj->vy_lo = 0x8000;
            /* fallthrough */
        case 7:
            o = cur_obj;
            if (o->vy_hi < 0 && player_ptr->y_world - o->y_world > 0x14)
                SETLONG(o->vy_hi, o->vy_lo, -LONG(o->vy_hi, o->vy_lo));
            o = cur_obj;
            if (o->vy_hi <= 0)
                break;
            switch (attr_at(o->x_world, o->y_world + sprite_h[0xD8])) {
            case 0: case 4: case 7:
            land:
                place_on_ground(cur_obj, 0xD8, 0);
                set_motion(0);
                cur_obj->misc = 4;
                break;
            }
            break;
        }
        cur_obj->box[0].hitflag = cur_obj->misc & 0xFFFE ? 1 : 0;
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

/* @1348:17AB file:1642B ai_t27 — спускается сверху (с задержкой
 * anim_len*15) и ходит зигзагом; ушёл ниже вида — удаляется */
void ai_t27(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0:
        cur_obj->y_world = player_y - 0x46;
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->state_list = sl_1B46;
        o->tick_period = 3;
        o->misc = 0;
        o->phase_ctr = -(o->anim_len * 0xF);
        o->phase = o->anim_len > 2 ? 1 : 0;
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->y_world > view_rect[3])
            o->state_byte = 6;
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        o->phase_ctr++;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            set_motion(0);
            /* fallthrough */
        case 1:
            if (cur_obj->phase_ctr >= 0)
                cur_obj->misc = 2;
            break;
        case 2:
            o = cur_obj;
            pending_state = o->phase + 2;
            ai_result |= 2;
            SETLONG(o->vx_hi, o->vx_lo, o->phase != 0 ? (s32)1 << 16 : -((s32)1 << 16));
            o->vy_hi = 1;
            o->vy_lo = 0x8000;
            /* fallthrough */
        case 3:
            if (cur_obj->phase_ctr >= 8) {
                cur_obj->misc = 4;
                cur_obj->phase_ctr = 0;
            }
            break;
        case 4:
            o = cur_obj;
            pending_state = o->phase + 4;
            ai_result |= 2;
            SETLONG(o->vx_hi, o->vx_lo, o->phase != 0 ? -((s32)1 << 16) : (s32)1 << 16);
            SETLONG(o->vy_hi, o->vy_lo, 0);
            /* fallthrough */
        case 5:
            if (cur_obj->phase_ctr >= 8) {
                cur_obj->misc = 2;
                cur_obj->phase_ctr = 0;
            }
            break;
        }
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

/* @1348:197C file:165FC ai_t28 — ходит за игроком, один раз стреляет */
void ai_t28(void)
{
    ActiveObject *o;
    s16 d;
    switch (cur_state) {
    case 0:
        place_on_ground(cur_obj, 0xCF, 0);
        ai_result |= 0x10;
        break;
    case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->phase = 0;
        o->state_list = sl_1B92;
        ai_result |= 2;
        o->aux = player_ptr->x_world > o->x_world ? 1 : 0;
        o->vx_hi = o->aux != 0 ? 1 : -1;
        /* fallthrough */
    case 2:
        if (cur_obj->aux != 0) {
            d = -0xA - (rtl_rand() & 7);
            if (d > player_ptr->x_world - cur_obj->x_world)
                cur_obj->aux = 0;
        } else {
            d = (rtl_rand() & 7) + 0xA;
            if (d < player_ptr->x_world - cur_obj->x_world)
                cur_obj->aux = 1;
        }
        o = cur_obj;
        SETLONG(o->vx_hi, o->vx_lo, o->aux != 0 ? 0x8000L : (s32)0xFFFF8000L);
        if (o->phase == 0 && o->frame_ctr > 0x4B) {
            spawn_projectile(o->x_world, o->y_world + 7, player_ptr->x_world,
                             player_ptr->y_world, 5, 0x20000L, 1);
            cur_obj->phase = 1;
        }
        pending_state = cur_obj->aux;
        ai_result |= 6;
        break;
    case 4:
        add_score(0x400);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @1348:1B1A file:1679A ai_t31 — стоит, моргая; ранен или по таймеру
 * уходит влево */
void ai_t31(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->misc = 0;
        place_on_ground(o, 0xDD, 0);
        cur_obj->state_list = sl_1BC4;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            /* fallthrough */
        case 1:
            cur_obj->frame_idx = (rtl_rand() & 0xF) < 4 ? 0 : 1;
            if (cur_obj->hit_points < 0x1F ||
                (rtl_rand() & 0x3F) + 0x64 < cur_obj->frame_ctr)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = 1;
            ai_result |= 2;
            cur_obj->vx_hi = -2;
            cur_obj->vx_lo = 0x8000;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x7000);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}

/* @1348:1C34 file:168B4 ai_t32 — стоит и стреляет раз в 90 тиков */
void ai_t32(void)
{
    ActiveObject *o;
    s16 y;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 4;
        place_on_ground(o, 0xDE, 0);
        cur_obj->state_list = sl_1BE0;
        pending_state = 0;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        if (cur_obj->frame_ctr > 0x5A) {
            y = (rtl_rand() & 8) + cur_obj->y_world + 3;
            spawn_projectile(cur_obj->x_world, y, player_ptr->x_world, player_ptr->y_world,
                             5, 0x20000L, 1);
            cur_obj->frame_ctr = 0x5A - ((rtl_rand() & 0xF) + 0x46);
            pending_state = 1;
        } else {
            pending_state = 0;
        }
        ai_result |= 6;
        break;
    case 4:
        add_score(0x7000);
        /* fallthrough */
    case 6:
        die_puff();
        break;
    }
}
