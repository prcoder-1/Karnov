/* Модуль 0x17B6 (file 0x19360) — AI-типы 53 (падающий стрелок) и 54
 * (плевун). */
#include "orig/fn.h"
#include "orig/todo.h"

/* @17B6:000C file:1936C t53_shoot — два горизонтальных снаряда (вид 2) */
static void t53_shoot(void)
{
    spawn_projectile(cur_obj->x_world - 5, cur_obj->y_world + 3, player_ptr->x_world, 0,
                     2, 0x40000L, 1);
    spawn_projectile(cur_obj->x_world + 5, cur_obj->y_world + 0x14, player_ptr->x_world, 0,
                     2, 0x40000L, 1);
}

/* @17B6:007D file:193DD ai_t53 — падает до опоры (атрибут 0), идёт
 * влево; рядом с игроком один раз стреляет */
void ai_t53(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->misc = 0;
        o->phase = 0;
        o->tick_period = 2;
        o->state_list = sl_1E96;
        update_bboxes(o);
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
            o->vy_hi = 5;
            o->vy_lo = 0;
            o->box[0].hitflag = 0;
            /* fallthrough */
        case 1:
            o = cur_obj;
            if (attr_at(o->x_world, o->vy_hi * 2 + o->box[0].y1) == 0)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = 1;
            ai_result |= 2;
            o = cur_obj;
            SETLONG(o->vy_hi, o->vy_lo, 0);
            o->vx_hi = -2;
            o->vx_lo = 0;
            place_on_ground(o, 0x13F, 0);
            cur_obj->box[0].hitflag = 1;
            /* fallthrough */
        case 3:
            if (dist_to_player() < 0x1E && cur_obj->phase == 0) {
                t53_shoot();
                cur_obj->phase = 1;
            }
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

/* @17B6:01DB file:1953B ai_t54 — плевун: при игроке ближе 120 стреляет
 * влево; ранен — становится декорацией (тип 9) */
void ai_t54(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->misc = 0;
        o->state_list = sl_1EB0;
        place_on_ground(o, 0x140, 0);
        update_bboxes(cur_obj);
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            cur_obj->frame_ctr = 0;
            /* fallthrough */
        case 1:
            if (cur_obj->frame_ctr > 0x6E && dist_to_player() < 0x78)
                cur_obj->misc = 2;
            break;
        case 2:
            pending_state = 1;
            ai_result |= 2;
            o = cur_obj;
            o->frame_ctr = 0;
            spawn_projectile(o->x_world, o->y_world + 0xF, o->x_world - 0xA, 0, 2, 0x30000L, 1);
            /* fallthrough */
        case 3:
            if (cur_obj->frame_ctr > 0x19)
                cur_obj->misc = 0;
            break;
        case 4:
            pending_state = 2;
            ai_result |= 2;
            o = cur_obj;
            o->obj_type = 9;
            o->box[0].was_vis = 0;
            o->box[0].hitflag = 0;
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x1800);
        cur_obj->misc = 4;
        cur_obj->state_byte = 2;
        break;
    case 6:
        ai_result |= 1;
        break;
    }
}
