/* Модуль 0x175E (file 0x18DE0) — AI-типы 50–52: стена со шипами,
 * ворота (тип 51 и их части), вращающийся блок. */
#include "orig/fn.h"
#include "orig/port.h"
#include "orig/todo.h"

/* @175E:000F file:18DEF ai_t50 — стена из 7 частей (клетки — стена 3);
 * игрок правее 720 в падении ранит её насмерть */
void ai_t50(void)
{
    ActiveObject *o;
    SpriteBox *b;
    s16 si, di, col, row;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->num_bboxes = 7;
        o->active = 0;
        b = &o->box[0];
        b->dx = -4;
        b->dy = 0;
        b->sprite = 0x132;
        b->hitflag = 1;
        for (di = 0x20, b = &cur_obj->box[1]; di < 0xB0; di += 0x18, b++) {
            b->dx = di;
            b->dy = -0xA;
            b->sprite = 0x134;
            b->hitflag = 0;
        }
        col = PX_COL(cur_obj->x_world >> tile_shift);
        row = PX_TROW(cur_obj->y_world + sprite_h[0x132]);
        for (si = 0; si < 6; si++)
            map_set_attr(col, row - si, 3);
        stamp_object(cur_obj);
        /* fallthrough */
    case 2:
        if (player_ptr->x_world > 0x2D0 && player_ptr->vy_hi > 0) {
            cur_obj->hit_points = -0x1E;
            cur_obj->state_byte = 4;
        }
        break;
    case 5:
        o = cur_obj;
        if (o->hit_points < 0x32 && o->box[0].sprite != 0x133) {
            o->box[0].sprite = 0x133;
            o->box[0].was_vis = 0;
        }
        break;
    case 4: case 6:
        col = PX_COL(cur_obj->x_world >> tile_shift);
        row = PX_TROW(cur_obj->y_world + sprite_h[0x132]);
        for (si = 0; si < 6; si++)
            map_set_attr(col, row - si, 2);
        ai_result |= 1;
        break;
    }
}

/* @175E:01A3 file:18F83 row_fill — атрибут строки под объектом на
 * ширину спрайта 0x135 */
static void row_fill(ActiveObject *o, s16 attr)
{
    s16 si, di, row, n;
    di = PX_COL(o->x_world >> tile_shift);
    row = PX_TROW(o->y_world);
    n = PX_COL(sprite_w[0x135] >> tile_shift);
    for (si = 0; si < n; si++)
        map_set_attr(di + si, row, attr);
}

/* @175E:0203 file:18FE3 ai_t52 — блок; игрок сверху запускает 3 оборота */
void ai_t52(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->misc = 0;
        o->tick_period = 3;
        o->y_world -= o->y_world % 10;
        cur_obj->state_list = sl_1E70;
        /* TODO(@175E:025D): row_fill(1) вызван с одним словом вместо
         * (o, attr) — объект и атрибут берутся из стека вызывающего;
         * не воспроизведено. */
        port_todo_once("175E:025D row_fill с мусорными аргументами");
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc |= 1;
        o->hit_flag = 0;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            o = cur_obj;
            o->box[0].hitflag = 1;
            o->hit_flag = 0;
            break;
        case 2:
            pending_state = (player_ptr->x_world > cur_obj->x_world + 8 ? 1 : 0) + 1;
            ai_result |= 2;
            o = cur_obj;
            o->phase_ctr = 0;
            o->box[0].hitflag = 0;
            o->frame_idx = 1;
            /* fallthrough */
        case 3:
            o = cur_obj;
            o->phase_ctr += o->frame_idx == 0;
            if (o->phase_ctr > 2)
                o->misc = 0;
            break;
        }
        ai_result |= 4;
        break;
    case 5:
        if (all_objects[cur_obj->hit_idx].obj_type == 0 &&
            player_ptr->y_world < cur_obj->y_world)
            cur_obj->misc = 2;
        break;
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @175E:0366 file:19146 gate_fill — атрибут клеток 96..97 × 31..37 */
static void gate_fill(s16 attr)
{
    s16 si, di;
    for (di = 0x1F; di < 0x26; di++)
        for (si = 0x60; si < 0x62; si++)
            map_set_attr(si, di, attr);
}

/* @175E:0391 file:19171 partner_kill — ранить ворота (тип 51) */
void partner_kill(void)
{
    if (t51_obj != NULL)
        t51_obj->state_byte = 4;
}

/* @175E:03A9 file:19189 t51_open — открытые ворота: столб из 8 частей
 * в (768, 310); клетки ворот — пустые */
void t51_open(void)
{
    ActiveObject *o;
    SpriteBox *b;
    s16 si, di;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->obj_type = 4;
        o->tick_period = 0xA;
        o->x_world = 0x300;
        o->y_world = 0x136;
        o->num_bboxes = 8;
        di = 0;
        for (si = 0, b = &o->box[0]; cur_obj->num_bboxes > si; si++, di += 0xA, b++) {
            b->dx = 0;
            b->dy = di;
            b->sprite = 0x13D;
            b->hitflag = 0;
        }
        stamp_object(cur_obj);
        /* fallthrough */
    case 2:
        gate_fill(2);
        for (si = 0, b = &cur_obj->box[0]; cur_obj->num_bboxes > si; si++, b++)
            b->was_vis = 0;
        break;
    case 6:
        cur_obj->state_byte = 2;
        break;
    }
}

/* @175E:0498 file:19278 ai_t51 — закрытые ворота (клетки — 4); гибель
 * (или partner_kill от босса тип 38) открывает их */
void ai_t51(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        t51_obj = o;
        o->tick_period = 0xFF;
        o->num_bboxes = 8;
        o->box[0].sprite = 0x135;
        o->box[0].dy = o->box[0].dx = 0;
        cur_obj->y_world -= cur_obj->y_world % 10;
        row_fill(cur_obj, 0);
        gate_fill(4);
        break;
    case 4: case 6:
        alloc_idx = alloc_object(0);
        o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
        if (o == NULL)
            break;
        row_fill(cur_obj, 2);
        queue_state(o, (farfn)t51_open);
        t51_obj = NULL;
        ai_result |= 1;
        break;
    }
}
