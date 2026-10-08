/* Модуль 0x0FFA (file 0x117A0) — выстрелы игрока, предметы на карте,
 * свиток конца уровня, падающий лёд (sky-цепь), PRE-/POST-KARNOV. */
#include "orig/fn.h"
#include "orig/todo.h"

/* @0FFA:0009 file:117A9 shot_init — vx = ±speed по взгляду; аним-состояние
 * 1 при огневой мощи больше 1 */
static void shot_init(s32 speed)
{
    shots_alive++;
    cur_obj->tick_period = 2;
    cur_obj->obj_type = 2;
    cur_obj->hit_points = 0xA;
    SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, player_dir & 1 ? speed : -speed);
    cur_obj->num_bboxes = 1;
    cur_obj->box[0].hitflag = 1;
    cur_obj->state_list = sl_16B2;
    pending_state = firepower > 1 ? 1 : 0;
    ai_result |= 6;
}

/* @0FFA:0083 file:11823 shot_tick — за краем вида снаряд гибнет */
static void shot_tick(void)
{
    s16 si = (s16)(cur_obj->x_world >> tile_shift);
    if (si > clip_rect[2] || si < clip_rect[0])
        cur_obj->state_byte = 4;
    if (firepower > 1 && weapon == 0)
        ai_result |= 4;
}

/* @0FFA:00C2 file:11862 shot_straight */
void shot_straight(void)
{
    switch (cur_state) {
    case 1:
        shot_init((s32)6 << 16);
        /* fallthrough */
    case 2:
        shot_tick();
        break;
    case 4:
        shots_alive--;
        ai_result |= 1;
        break;
    }
}

/* @0FFA:00FF file:1189F shot_upper — вверх на 16 и дальше прямо */
void shot_upper(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        shot_init((s32)7 << 16);
        o = cur_obj;
        o->vy_hi = -5;
        o->phase = 1;
        o->misc = o->y_world - 0x10;
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->phase != 0 && o->y_world < o->misc) {
            o->vy_hi = 0;
            o->vy_lo = 0;
            o->phase = 0;
        }
        shot_tick();
        break;
    case 4:
        shots_alive--;
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0183 file:11923 shot_lower — падает в пустоту (атрибут 2, 9),
 * гибнет в стене */
void shot_lower(void)
{
    s16 si, di, a;
    switch (cur_state) {
    case 1:
        shot_init((s32)7 << 16);
        /* fallthrough */
    case 2:
        si = PX_COL(cur_obj->x_world >> tile_shift);
        di = PX_TROW(cur_obj->y_world);
        a = map_attr(si, di + 1);
        cur_obj->vy_hi = a == 2 || a == 9 ? 5 : 0;
        a = map_attr(si, di);
        if (a == 3 || a == 4)
            cur_obj->state_byte = 4;
        shot_tick();
        ai_result |= 4;
        break;
    case 4:
        shots_alive--;
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0247 file:119E7 boomerang — оружие 2: этапы ускорений из
 * an_16C2 (длительности) и an_16CC (ax, ay << 14); на этапе 4 ловится
 * игроком (оружие остаётся) */
void boomerang(void)
{
    ActiveObject *o;
    s16 si, di;
    s16 *e;
    s32 v;
    switch (cur_state) {
    case 1:
        o = cur_obj;
        o->obj_type = 2;
        o->hit_points = 0x64;
        o->num_bboxes = 1;
        v = (s32)((u32)motion_table[inp_dir & 3].vx << 1);
        SETLONG(o->x_world, o->x_lo, v + LONG(player_ptr->x_world, player_ptr->x_lo));
        o->y_world = player_ptr->y_world + 7;
        SETLONG(o->vx_hi, o->vx_lo, player_dir & 1 ? (s32)3 << 16 : -((s32)3 << 16));
        o->phase_ctr = o->vx_hi > 0;
        o->misc = 0;
        o->phase = 0;
        o->aux = 0;
        o->frame_ctr = 0;
        o->box[0].hitflag = 0;
        o->state_list = sl_16EA;
        pending_state = 0;
        ai_result |= 6;
        break;
    case 2:
        o = cur_obj;
        o->hit_flag = 0;
        if ((++o->aux & 0xF) == 0)
            play_sound(4);
        if ((cur_obj->x_world - player_ptr->x_world > 0) != cur_obj->phase_ctr &&
            !box_visible(&cur_obj->box[0])) {
            cur_obj->state_byte = 6;
            ai_result |= 4;
            break;
        }
        o = cur_obj;
        if (an_16C2[o->misc] == o->frame_ctr) {
            o->frame_ctr = 0;
            o->misc++;
            e = &an_16CC[o->misc * 2];
            v = (s32)((u32)(s32)e[0] << 14);
            if (cur_obj->phase_ctr == 0)
                v = -v;
            SETLONG(cur_obj->ax_hi, cur_obj->ax_lo, v);
            SETLONG(cur_obj->ay_hi, cur_obj->ay_lo, (s32)((u32)(s32)e[1] << 14));
            switch (cur_obj->misc) {
            case 2:
                cur_obj->box[0].hitflag = 1;
                break;
            case 4:
                cur_obj->vy_hi = 0;
                cur_obj->vy_lo = 0;
                break;
            }
        }
        if (cur_obj->misc == 4) {
            si = cur_obj->x_world + cur_obj->vx_hi - player_ptr->x_world;
            if (si <= 0)
                si = -si;
            di = cur_obj->y_world + cur_obj->vy_hi - player_ptr->y_world;
            if (di <= 0)
                di = -di;
            if (si < 5 && di < 0xA) {
                cur_obj->phase = 1;
                cur_obj->state_byte = 6;
            }
        }
        ai_result |= 4;
        break;
    case 4:
        cur_obj->hit_points = 0x64;
        cur_obj->state_byte = 2;
        break;
    case 6:
        if (cur_obj->misc == 0)
            break;
        weapon = cur_obj->phase != 0 ? 2 : 0;
        fire_cooldown = 0;
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0575 file:11D15 fireball — оружие 1 (за патроны) */
void fireball(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        shots_alive++;
        cur_obj->obj_type = 2;
        cur_obj->hit_points = 0x19;
        set_motion(player_dir);
        o = cur_obj;
        o->x_world = (s16)(o->vx_hi << 1) + player_ptr->x_world + 3;
        o->y_world = player_ptr->box[0].y0 + 6;
        SETLONG(o->vx_hi, o->vx_lo, player_dir & 1 ? (s32)4 << 16 : -((s32)4 << 16));
        o->misc = o->vx_hi > 0 ? 2 : 0;
        o->num_bboxes = 1;
        o->box[0].dx = 0;
        o->box[0].dy = 0;
        o->box[0].hitflag = 1;
        update_bboxes(o);
        /* fallthrough */
    case 2:
        o = cur_obj;
        o->hit_flag = 0;
        o->box[0].sprite = (o->frame_ctr & 1 ? 0x21 : 0x22) + o->misc;
        shot_tick();
        break;
    case 4:
        shots_alive--;
        ai_result |= 1;
        break;
    }
}

/* @0FFA:067D file:11E1D item_chain_add */
static void item_chain_add(ActiveObject *o)
{
    o->parent = item_chain;
    item_chain = o;
}

/* @0FFA:069E file:11E3E chain_remove — объекты сравниваются по active_pos */
static ActiveObject *chain_remove(ActiveObject *list, ActiveObject *o)
{
    if (list == NULL)
        return list;
    if (list->active_pos == o->active_pos)
        return list->parent;
    list->parent = chain_remove(list->parent, o);
    return list;
}

/* @0FFA:0704 file:11EA4 item_chain_remove */
static void item_chain_remove(ActiveObject *o)
{
    item_chain = chain_remove(item_chain, o);
}

/* @0FFA:0724 file:11EC4 ladders_mark — атрибут лестницы (7) под всеми
 * сегментами раскладных лестниц */
void ladders_mark(void)
{
    ActiveObject *o;
    s16 n, r0, si, di;
    SpriteBox *b;
    for (o = item_chain; o != NULL; o = o->parent) {
        for (n = 0, b = &o->box[0]; o->num_bboxes > n; n++, b++) {
            b->was_vis = 0;
            r0 = PX_TROW(o->box[n].y0);
            di = r0 + 2;
            for (si = r0; si < di; si++)
                MAP_WR(si, (u16)(o->phase_ctr * 2) + 1, 7);
        }
    }
}

/* @0FFA:07CC file:11F6C ladder_item — предмет 0: раскладная лестница
 * растёт вверх по сегменту (sprite 0x18), пока клетки пусты (2) или
 * лестница (7); сегменты отмечаются в карте атрибутом 7 */
void ladder_item(void)
{
    ActiveObject *o;
    s16 n, r0, r1, c0, c1, si, di, blocked;
    u8 a;
    switch (cur_state) {
    case 1:
        cur_obj->num_bboxes = 0;
        if (player_state != 0 && player_state != 8) {
            item_chain_remove(cur_obj);
            ai_result |= 1;
            break;
        }
        item_consume(0);
        item_chain_add(cur_obj);
        o = cur_obj;
        o->tick_period = 2;
        o->misc = 0;
        o->phase_ctr = cell_col;
        center_on_column(o, o->phase_ctr, sprite_w[0x18]);
        cur_obj->y_world = player_sprite_h() + player_ptr->y_world - sprite_h[0x18];
        stamp_object(cur_obj);
        break;
    case 2:
        switch (cur_obj->misc) {
        case 0:
            o = cur_obj;
            n = o->num_bboxes;
            if (n >= 8) {
                cur_obj->misc = 1;
                break;
            }
            o->num_bboxes++;
            o->box[n].sprite = 0x18;
            o->box[n].dx = -1;
            o->box[n].dy = (s16)-(sprite_h[0x18] * n);
            o->box[n].hitflag = 0;
            o->box[n].was_vis = 0;
            update_bboxes(cur_obj);
            r0 = PX_TROW(cur_obj->box[n].y0);
            r1 = r0 + 2;
            c0 = cur_obj->phase_ctr - 1;
            c1 = c0 + 2;
            blocked = 0;
            for (di = c0; di < c1 && blocked == 0; di++)
                for (si = r0 - 3; si < r1 && blocked == 0; si++) {
                    a = (u8)map_attr(di, si);
                    blocked = !((a == 2 || a == 7) && si > 0);
                }
            if (blocked == 0) {
                di = cur_obj->phase_ctr * 2 + 1;
                for (si = r0; si < r1; si++)
                    MAP_WR(si, (u16)di, 7);
                break;
            }
            if (--cur_obj->num_bboxes == 0) {
                cur_obj->state_byte = 6;
                break;
            }
            cur_obj->misc = 1;
            break;
        case 1:
            if (!any_box_visible(cur_obj) && cur_obj->frame_ctr > 0x32)
                cur_obj->state_byte = 6;
            else
                cur_obj->frame_ctr = 0;
            break;
        }
        break;
    case 6:
        item_chain_remove(cur_obj);
        if (cur_obj->num_bboxes == 0)
            item_pickup(0);
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0A7B file:1221B bomb_blast — взрыв: рядом с игроком (кадры 0, 3)
 * убивает его */
void bomb_blast(void)
{
    ActiveObject *o;
    s16 d;
    switch (cur_state) {
    case 1:
        o = cur_obj;
        o->tick_period = 2;
        o->throttle = 3;
        o->obj_type = 2;
        o->num_bboxes = 1;
        o->box[0].hitflag = 1;
        set_motion(0);
        cur_obj->state_list = sl_1706;
        pending_state = 0;
        ai_result |= 6;
        break;
    case 2:
        o = cur_obj;
        o->hit_points = 0x64;
        o->box[0].hitflag = 1;
        ai_result |= o->frame_idx == 0 ? 1 : 4;
        switch (cur_obj->frame_idx) {
        case 0: case 3:
            d = player_ptr->x_world - cur_obj->box[0].x0;
            if (d <= 0)
                d = -d;
            if (d >= 0xF)
                break;
            d = player_ptr->y_world - cur_obj->box[0].y0;
            if (d <= 0)
                d = -d;
            if (d >= 0x1E)
                break;
            die_hit();
            break;
        }
        break;
    case 3: case 4: case 5:
        cur_obj->state_byte = 2;
        cur_obj->box[0].hitflag = 0;
        break;
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0BCB file:1236B bomb — предмет 2: падает, через 50 тиков взрыв */
void bomb(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        o = cur_obj;
        o->obj_type = 2;
        o->num_bboxes = 1;
        o->box[0].sprite = 0x19;
        o->box[0].dx = 0;
        o->box[0].dy = 0;
        o->box[0].hitflag = 0;
        o->x_world = (player_dir & 1 ? sprite_w[0x62] : 0) + player_x;
        o->y_world = player_y + 5;
        o->misc = 0x32;
        o->hit_points = 0x64;
        /* fallthrough */
    case 2:
        o = cur_obj;
        o->misc -= o->misc != 0;
        if (o->misc == 0) {
            queue_state(cur_obj, (farfn)bomb_blast);
            break;
        }
        if (ATTR_PROP(attr_at(o->x_world, o->box[0].y0 + 0xA)) & 0x41)
            SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, 0);
        else
            SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, (s32)2 << 16);
        break;
    case 3: case 4: case 5:
        cur_obj->state_byte = 2;
        break;
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0CD7 file:12477 drop_seed — 99 → случайный предмет 0..10 */
static s16 drop_seed(s16 n)
{
    if (n != 0x63)
        return n;
    return rtl_rand() % 0xB;
}

/* @0FFA:0CF6 file:12496 hidden_item — AI-тип 4: виден (мигая) только
 * при действии предмета 5 */
void hidden_item(void)
{
    s16 vis;
    switch (cur_state) {
    case 0: case 1:
        cur_obj->misc = drop_seed(cur_obj->frame_idx);
        cur_obj->num_bboxes = 1;
        cur_obj->box[0].dx = 0;
        cur_obj->box[0].dy = 0;
        /* fallthrough */
    case 2:
        vis = plr_D8B8 != 0 && (frame_no & 8);
        cur_obj->box[0].sprite = vis ? cur_obj->misc + 0x27 : 0;
        cur_obj->box[0].hitflag = vis;
        break;
    case 4:
        play_sound(5);
        item_pickup(cur_obj->misc);
        /* fallthrough */
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0DAA file:1254A falling_item — AI-тип 5: падает до земли,
 * затем впечатывается в фон */
void falling_item(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        cur_obj->misc = drop_seed(cur_obj->frame_idx);
        o = cur_obj;
        o->phase = 1;
        o->num_bboxes = 1;
        o->box[0].sprite = o->misc + 0x27;
        o->box[0].dx = 0;
        o->box[0].dy = 0;
        o->box[0].hitflag = 1;
        o->vy_hi = 2;
        o->vy_lo = 0;
        update_bboxes(o);
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->phase == 0)
            break;
        if (!(ATTR_PROP(attr_at(o->x_world, o->box[0].y0 + 0xA)) & 1))
            break;
        set_motion(0);
        stamp_object(cur_obj);
        cur_obj->phase = 0;
        break;
    case 4:
        play_sound(5);
        unstamp_object(cur_obj);
        ladders_mark();
        item_pickup(cur_obj->misc);
        /* fallthrough */
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @0FFA:0EB8 file:12658 static_item — AI-тип 6: предмет 0..10 на месте;
 * иначе (seed ≥ 11) невидимая зона, включающая предмет seed-7 рядом с
 * игроком (ближе 40) */
void static_item(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        cur_obj->misc = drop_seed(cur_obj->frame_idx);
        o = cur_obj;
        o->tick_period = 0x64;
        if (o->misc < 0xB) {
            o->num_bboxes = 1;
            o->box[0].sprite = o->misc + 0x27;
            o->box[0].dx = 0;
            o->box[0].dy = (s16)-((o->y_world + sprite_h[0x27]) % 10);
            o->box[0].hitflag = 1;
            o->phase = 0;
        } else {
            o->num_bboxes = 0;
            o->phase = 1;
            o->aux = 0;
        }
        stamp_object(cur_obj);
        /* fallthrough */
    case 2:
        si = dist_to_player();
        o = cur_obj;
        if (o->phase == 0)
            break;
        if (o->aux != 0) {
            if (si > 0x28) {
                item_set_active(o->misc, 0);
                cur_obj->aux = 0;
            }
        } else if (si < 0x28) {
            item_set_active(o->misc, 1);
            cur_obj->aux = 1;
        }
        break;
    case 4:
        if (cur_obj->phase != 0) {
            cur_obj->state_byte = 2;
            break;
        }
        play_sound(5);
        item_pickup(cur_obj->misc);
        unstamp_object(cur_obj);
        ladders_mark();
        /* fallthrough */
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @0FFA:1032 file:127D2 level_scroll — AI-тип 7: свиток конца уровня.
 * Пока goal-объект жив, невидим (phase 0). Ближе 0x50 — управление
 * игроком переходит к демо (пустые события): игрок идёт к свитку, враги
 * уничтожаются; касание (состояние 5) — «вниз», музыка; по её концу —
 * end_life */
void level_scroll(void)
{
    ActiveObject *o;
    s16 d;
    u8 dir;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->num_bboxes = 0;
        o->box[0].dx = 0;
        o->box[0].dy = 0;
        o->box[0].sprite = 0x90;
        o->box[0].hitflag = 0;
        o->misc = 0;
        o->phase = 0;
        goal_watch = o;
        update_bboxes(o);
        /* fallthrough */
    case 2:
        switch (cur_obj->misc) {
        case 0:
            if (cur_obj->phase == 0)
                break;
            d = player_ptr->x_world - cur_obj->x_world;
            if (d <= 0)
                d = -d;
            if (d >= 0x50)
                break;
            set_player_state(9);
            player_ptr->box[0].hitflag = 1;
            o = cur_obj;
            o->misc = 1;
            o->num_bboxes = 1;
            o->vy_hi = 2;
            queue_state(player_ptr, (farfn)k_walk);
            if (!(game_D564 & 4)) {
                demo_ptr = &demo_buf[0];
                demo_reset(demo_ptr);
                game_D564 = 4;
            }
            demo_record_event(demo_ptr, 0, frame_no + 1);
            demo_record_event(demo_ptr, 0, frame_no + 2);
            plr_D8B4 = 0;
            plr_D8B6 = 0;
            plr_D8BA = 0;
            queue_state(player_ptr, (farfn)k_walk);
            break;
        case 1:
            o = cur_obj;
            if (attr_at(o->x_world, o->box[0].y1 + 1) == 2)
                break;
            o->misc = 2;
            o->box[0].hitflag = 1;
            o->vy_hi = 0;
            o->vy_lo = 0;
            dir = cur_obj->x_world > player_ptr->x_world ? 1 : 2;
            demo_record_event(demo_ptr, dir, frame_no + 1);
            for (scroll_i = 0, scroll_k = 1; scroll_i < active_count[1];
                 scroll_i++, scroll_k += 2) {
                o = &all_objects[active_index[scroll_k]];
                if (o->active_pos != cur_obj->active_pos)
                    o->state_byte = 6;
            }
            break;
        case 3:
            if (++cur_obj->phase_ctr <= 5)
                break;
            if (music_done() != 0)
                end_life();
            break;
        }
        break;
    case 5:
        demo_record_event(demo_ptr, 4, frame_no + 1);
        demo_record_event(demo_ptr, 4, frame_no + 2);
        o = cur_obj;
        o->misc = 3;
        o->phase_ctr = 0;
        o->x_world = player_ptr->x_world + 5;
        o->y_world = player_ptr->y_world + 0xE;
        play_music(MUSIC(0x06FC), 0, 0);
        break;
    }
}

/* @0FFA:1340 file:12AE0 ice_throw — три осколка в игрока */
static void ice_throw(void)
{
    s16 si, di = 0;
    for (si = 0; si < 3; si++) {
        spawn_projectile(cur_obj->x_world + 2, cur_obj->y_world + si * 2 + 2,
                         player_ptr->x_world, player_ptr->y_world + di - 5,
                         0, 0x14000, 0);
        di += rtl_rand() & 0x1F;
    }
}

/* @0FFA:139F file:12B3F sky_ice — AI-тип 8: льдина падает рядом с
 * игроком; на земле бросает осколки (раз в 100 тиков, вблизи чаще) */
void sky_ice(void)
{
    ActiveObject *o;
    s16 si, y;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->obj_type = 1;
        o->box[0].hitflag = 0;
        o->num_bboxes = 1;
        cur_obj->x_world = (rtl_rand() & 0x3F) + player_ptr->x_world - 0xA;
        y = player_ptr->y_world - 0x64;
        cur_obj->y_world = y - (rtl_rand() & 0x3F);
        cur_obj->misc = 0;
        cur_obj->state_list = sl_171C;
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            o = cur_obj;
            o->vy_hi = 1;
            o->vy_lo = 0;
            o->tick_period = 3;
            /* fallthrough */
        case 1:
            o = cur_obj;
            if (ATTR_PROP(attr_at(o->x_world, o->y_world + 5)) & 1)
                cur_obj->misc = 2;
            if (cur_obj->y_world > player_y + 0x82) {
                cur_obj->x_world += rtl_rand() & 0x1F;
                cur_obj->y_world = player_y - 0x8C;
            }
            break;
        case 2:
            pending_state = 1;
            ai_result |= 2;
            o = cur_obj;
            o->vy_hi = 0;
            o->vy_lo = 0;
            o->frame_ctr = 0;
            place_on_ground(o, 0x4B, 0);
            /* fallthrough */
        case 3:
            if (cur_obj->frame_ctr > 0x64) {
                ice_throw();
                cur_obj->frame_ctr = -0x1E;
            }
            if (dist_to_player() < 0xF)
                cur_obj->frame_ctr += 0x64;
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

/* @0FFA:155E file:12CFE kill_sky_chain */
void kill_sky_chain(void)
{
    while (sky_chain != NULL) {
        sky_chain->state_byte = 4;
        sky_chain = sky_chain->parent;
    }
}

/* @0FFA:1585 file:12D25 sky_spawn — n льдин в цепочку */
static void sky_spawn(s16 n)
{
    s16 si;
    ActiveObject *o, *prev = NULL;
    for (si = 0; si < n; si++) {
        alloc_idx = alloc_object(1);
        o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
        if (o == NULL)
            continue;
        o->parent = prev;
        prev = o;
        init_active_object(o, (farfn)sky_ice, 0);
    }
    sky_chain = prev;
}

/* @0FFA:1615 file:12DB5 sky_driver — после 390 кадров почти на месте
 * (сдвиг меньше 50) — 1–2 льдины; продвижение сбрасывает цепь */
static void sky_driver(void)
{
    s16 si = player_ptr->x_world - sky_x0;
    if (si > 0x32) {
        sky_x0 = player_ptr->x_world;
        sky_timer = 0;
        kill_sky_chain();
        return;
    }
    if (player_state != 3)
        sky_timer++;
    if (sky_timer > 0x186 && sky_chain == NULL) {
        sky_spawn((rtl_rand() & 1) + 1);
        sky_x0 = player_ptr->x_world;
        sky_timer = 0;
    }
}

/* @0FFA:167D file:12E1D fire_fireball — патрон долой; кончились —
 * обычное оружие */
static void fire_fireball(void)
{
    ActiveObject *o;
    alloc_idx = alloc_object(0);
    o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
    if (o == NULL)
        return;
    queue_state(o, (farfn)fireball);
    ammo_dirty = 1;
    if (bcd_sub(ammo_digits, 2, 1))
        weapon = 0;
}

/* @0FFA:16F9 file:12E99 fire_boomerang */
static void fire_boomerang(void)
{
    ActiveObject *o;
    alloc_idx = alloc_object(0);
    o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
    if (o != NULL)
        queue_state(o, (farfn)boomerang);
}

/* @0FFA:174D file:12EED spawn_shot */
static void spawn_shot(s16 x, s16 y, farfn h)
{
    ActiveObject *o;
    alloc_idx = alloc_object(0);
    o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
    if (o == NULL)
        return;
    o->x_world = x;
    o->y_world = y;
    queue_state(o, h);
}

/* @0FFA:17AF file:12F4F fire_normal — 1–3 снаряда по огневой мощи */
static void fire_normal(void)
{
    s32 x = (s32)((u32)motion_table[player_dir].vx << 1) +
            LONG(player_ptr->x_world, player_ptr->x_lo) + ((s32)4 << 16);
    s16 xh = (s16)(x >> 16), y = player_ptr->box[0].y0 + 7;
    if (player_state == 3 && (inp_dir & 4))
        y += 0x10;
    switch (firepower) {
    case 3:
        spawn_shot(xh, y - 2, (farfn)shot_upper);
        /* fallthrough */
    case 2:
        spawn_shot(xh, y + 2, (farfn)shot_lower);
        /* fallthrough */
    case 1:
        spawn_shot(xh, y, (farfn)shot_straight);
        break;
    }
    if (player_state == 0) {
        player_ptr->box[0].sprite = player_dir & 2 ? 0x69 : 0x71;
        player_ptr->box[0].dy = 1;
    }
}

/* @0FFA:1888 file:13028 fire — не больше 15 снарядов; перезарядка 3
 * тика (в god mode 1), у бумеранга — до его возврата. rand() вызывается
 * при каждой попытке. */
static void fire(void)
{
    s16 cd = god_mode != 0 ? 1 : 3;
    if (shots_alive < 0xF) {
        play_sound(0);
        switch (weapon) {
        case 0:
            fire_normal();
            fire_cooldown = cd;
            break;
        case 1:
            fire_fireball();
            fire_cooldown = cd;
            break;
        case 2:
            fire_boomerang();
            fire_cooldown = 1000;
            break;
        }
    }
    rtl_rand();
}

/* @0FFA:18E5 file:13085 try_jump — по нажатию «вверх»: на лестнице —
 * хвататься, иначе прыжок vy -4 (-6 с предметом 1), ay 0.3125 */
static void try_jump(void)
{
    s16 di = inp_prev_mask & 0xF, si = inp_mask & 0xF;
    if (!(si & 8) || (di & 8) || player_state == 1)
        return;
    if (attr_head() == 7) {
        item_set_active(0xF, 0);
        plr_D8BA = 0;
        ladder_col = cell_col;
        ladder_grab();
        return;
    }
    if (player_state == 3)
        return;
    player_ptr->vy_hi = -4 - (plr_D8B4 != 0 ? 2 : 0);
    player_ptr->vy_lo = 0;
    player_ptr->ay_hi = 0;
    player_ptr->ay_lo = 0x5000;
    set_player_state(1);
    karnov_mode = 0;
    queue_state(player_ptr, (farfn)k_jump);
}

/* @0FFA:19AB file:1314B try_crouch — по нажатию «вниз»: на лестнице —
 * спуск, иначе присед */
static void try_crouch(void)
{
    s16 di = inp_prev_mask & 0xF, si = inp_mask & 0xF;
    if (!(si & 4) || (di & 4) || player_state == 1)
        return;
    if (!(current_level == 9 && goal_idx >= 0) && attr_feet() == 7) {
        ladder_col = cell_col;
        ladder_grab_down();
        return;
    }
    queue_state(player_ptr, (farfn)k_crouch);
}

/* @0FFA:1A1A file:131BA PRE_KARNOV — фаза 5: предметы, левая граница
 * камеры (не назад, кроме уровней 7 и 9), рамки мира, движение игрока с
 * упорами, кэш позиции для AI, прыжок/присед, лёд */
void PRE_KARNOV(void)
{
    s16 si, di, upd;
    items_tick();
    si = (s16)((player_x - 0xB9) >> tile_shift);
    if (current_level != 7 && current_level != 9 && si > cam_rect[0] &&
        cam_rect[2] - si > view_rect[2] - view_rect[0])
        cam_rect[0] = si;
    di = (s16)((cam_rect[0] + vb_view_bytes) << tile_shift);
    if (player_ptr->x_world < di) {
        player_ptr->x_world = di;
        plr_D8AE |= 2;
    } else if (player_ptr->x_world + sprite_w[0x62] > (s16)(world_rect[2] << tile_shift)) {
        player_ptr->x_world = (s16)(world_rect[2] << tile_shift) - 0xC;
        plr_D8AE |= 1;
    }
    if (player_ptr->y_world < 0)
        player_ptr->y_world = 0;
    move_with_collision(&coll[0], &player_ptr->box[0], motion_table[inp_dir & 3].vx,
                        LONG(player_ptr->vy_hi, player_ptr->vy_lo));
    if (coll[0].any != 0) {
        cur_obj = player_ptr;
        cur_state = 3;
        ai_dispatch();
        coll[0].any = 0;
    } else {
        plr_D8AE = 0;
    }
    upd = 1;
    switch (player_state) {
    case 1:
        if (karnov_mode == 0 || karnov_mode == 1)
            upd = 0;
        break;
    case 6: case 7: case 9:
        upd = 0;
        break;
    }
    if (upd && player_ptr->y_world + 0x14 <= world_rect[3])
        player_y = player_ptr->box[0].y0;
    player_x = player_ptr->x_world;
    switch (player_state) {
    case 0: case 9:
        try_crouch();
        /* fallthrough */
    case 3: case 8:
        try_jump();
        break;
    }
    if (current_level != 9)
        sky_driver();
}

/* @0FFA:1BD4 file:13374 timer_dec — *warn |= «осталось меньше 65» */
static void timer_dec(s16 *t, s16 *warn)
{
    *t -= *t != 0;
    *warn |= *t != 0 && *t < 0x41;
}

/* @0FFA:1C04 file:133A4 POST_KARNOV — фаза 7: таймеры предметов 1, 5, 6
 * (писк в конце), огонь, клетки игрока, ступень (сносит влево), камера */
void POST_KARNOV(void)
{
    s16 warn, old5;
    if (player_state != 6) {
        if (fire_cooldown != 0)
            fire_cooldown--;
        warn = 0;
        old5 = plr_D8B8;
        timer_dec(&plr_D8B4, &warn);
        timer_dec(&plr_D8B8, &warn);
        timer_dec(&plr_D8B6, &warn);
        if (old5 != 0 && plr_D8B8 == 0)
            item_set_active(0xC, 0);
        if (warn != 0 && (frame_no & 0xF) == 0)
            play_sound(9);
        if (inp_prev_fire == 0 && weapon != 2)
            fire_cooldown = 0;
        if (inp_fire != 0 && fire_cooldown == 0 && player_state != 0xA)
            fire();
    }
    if (coll[0].any == 0)
        plr_D8AE = 0;
    cell_col = PX_COL((s16)((sprite_w[player_ptr->box[0].sprite] >> 1) +
                               player_ptr->x_world) >> tile_shift);
    cell_row_head = PX_TROW(player_ptr->y_world);
    cell_row_feet = PX_TROW(player_sprite_h() + player_ptr->y_world);
    if (attr_feet() == 5 && player_state != 6)
        SETLONG(player_ptr->vx_hi, player_ptr->vx_lo,
                LONG(player_ptr->vx_hi, player_ptr->vx_lo) - 0x8000);
    if (!(current_level == 9 && goal_idx >= 0)) {
        switch (player_state) {
        case 1: case 5: case 6: case 7:
            break;
        case 3:
            cam_track(0x28, 0x28);
            break;
        default:
            cam_track(0x1E, 0x46);
            break;
        }
    }
    camera_update();
    hud_score(score_bcd);
    if (player_ptr->x_world > respawn_D8CC)
        respawn_D8CC = player_ptr->x_world;
}
