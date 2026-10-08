/* Модуль 0x17EC (file 0x196C0) — AI-типы 55–57: ходок, финальный босс
 * и неподвижная декорация. */
#include "orig/fn.h"
#include "orig/todo.h"

/* @17EC:0001 file:196C1 t55_reset — новое направление, восстановить hp */
static void t55_reset(void)
{
    ActiveObject *o;
    s16 r = rtl_rand() & 1;
    o = cur_obj;
    o->phase = r;
    o->misc = 0;
    o->hit_points = o->phase_ctr;
}

/* @17EC:0028 file:196E8 ai_t55 — ходит до препятствия; ранен —
 * замирает на 60 тиков (неуязвим), затем снова в путь */
void ai_t55(void)
{
    ActiveObject *o;
    s16 si;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 4;
        o->phase_ctr = o->hit_points;
        o->misc = 2;
        place_on_ground(o, 0x142, 0);
        update_bboxes(cur_obj);
        cur_obj->state_list = sl_1EF4;
        /* fallthrough */
    case 2:
        o = cur_obj;
        si = o->misc;
        o->misc = si | 1;
        switch (si) {
        case 0:
            pending_state = o->phase;
            ai_result |= 2;
            SETLONG(o->vx_hi, o->vx_lo, o->phase != 0 ? 0x8000L : (s32)0xFFFF8000L);
            o->box[0].hitflag = 1;
            /* fallthrough */
        case 1:
            o = cur_obj;
            if (attr_at((o->phase != 0 ? 0x14 : -0xA) + o->x_world, o->box[0].y1) != 0) {
                cur_obj->phase ^= 1;
                cur_obj->misc = 0;
            }
            break;
        case 2:
            pending_state = 2;
            ai_result |= 2;
            o = cur_obj;
            o->frame_ctr = 0;
            o->box[0].hitflag = 0;
            SETLONG(o->vx_hi, o->vx_lo, 0);
            /* fallthrough */
        case 3:
            if (cur_obj->frame_ctr > 0x3C)
                t55_reset();
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        o = cur_obj;
        o->hit_points = 0x64;
        o->state_byte = 2;
        o->misc = 2;
        break;
    case 6:
        ai_result |= 1;
        break;
    }
}

/* @17EC:01AA file:1986A boss2_burst — гибель босса: 6 «пуфов» по an_1F0C */
static void boss2_burst(void)
{
    ActiveObject *o;
    s16 si;
    for (si = 0; si < 6; si++) {
        alloc_idx = alloc_object(1);
        o = alloc_idx < 0 ? NULL : &all_objects[alloc_idx];
        if (o == NULL)
            continue;
        o->x_world = an_1F0C[si * 2] + cur_obj->x_world;
        o->y_world = an_1F0C[si * 2 + 1] + cur_obj->y_world;
        o->box[0].hitflag = 0;
        queue_state(o, (farfn)puff_handler);
        o->ai_init = (farfn)remove_object;
        o->phase = si == 0 ? 1 : 0;
    }
    play_sound(1);
    ai_result |= 1;
}

/* @17EC:027B file:1993B t56_fan — 16 снарядов (вид 11) по an_1F24 в
 * четырёх поворотах и один в игрока */
static void t56_fan(void)
{
    s16 si, di, t, a, b;
    di = cur_obj->x_world + 8;
    t = cur_obj->y_world + 0xF;
    for (si = 0; si < 4; si++) {
        a = an_1F24[si * 2];
        b = an_1F24[si * 2 + 1];
        spawn_projectile(di, t, a + di, b + t, 0xB, 0x18000L, 0);
        spawn_projectile(di, t, di - b, a + t, 0xB, 0x18000L, 0);
        spawn_projectile(di, t, di - a, t - b, 0xB, 0x18000L, 0);
        spawn_projectile(di, t, b + di, t - a, 0xB, 0x18000L, 0);
    }
    spawn_projectile(di, t, player_ptr->x_world + 5, player_ptr->y_world + 0xA, 0xB,
                     0x1F000L, 0);
}

/* @17EC:03B2 file:19A72 ai_t56 — финальный босс: снимает предметы
 * игрока, фиксирует камеру; появляется в одной из трёх точек t56_pos,
 * выпускает веер снарядов */
void ai_t56(void)
{
    ActiveObject *o;
    s16 si;
    u8 rec[0xA5];
    switch (cur_state) {
    case 0: case 1:
        play_sound(9);
        /* items_reset получает DS:D8AA — блок игрока в раскладке оригинала */
        ds_copy_out(0xD8AA, sizeof rec, rec);
        items_reset(rec);
        ds_copy_in(0xD8AA, sizeof rec, rec);
        hud_items();
        hud_sel_clear();
        o = cur_obj;
        o->tick_period = 5;
        o->misc = 0;
        o->aux = 1;
        o->phase_ctr = o->hit_points;
        o->state_list = sl_1F76;
        cam_rect[0] = view_rect[0];
        cam_rect[2] = view_rect[2];
        /* fallthrough */
    case 2:
        if (cur_obj->frame_ctr < 0x23) {
            cam_track(0x1E, 0x60);
            cam_track(0x1E, 0x60);
        } else if (cur_obj->frame_ctr < 0x2D) {
            camera_update();
            cam_rect[1] = view_rect[1];
            cam_rect[3] = view_rect[3];
        }
        o = cur_obj;
        si = o->misc;
        o->misc = si | 1;
        switch (si) {
        case 0:
            pending_state = 0;
            ai_result |= 2;
            o = cur_obj;
            o->num_bboxes = 0;
            o->x_world = t56_pos[o->aux * 2];
            o->y_world = t56_pos[o->aux * 2 + 1] - sprite_h[0x14A];
            o->hit_points = o->phase_ctr;
            o->frame_ctr = 0;
            /* fallthrough */
        case 1:
            if (cur_obj->frame_ctr > 0x32)
                cur_obj->misc = 2;
            break;
        case 2:
            o = cur_obj;
            o->hit_points = o->phase_ctr;
            o->num_bboxes = 1;
            o->frame_idx = 0;
            /* fallthrough */
        case 3:
            if (cur_obj->frame_idx == an_1F34[0] - 1)
                cur_obj->misc = 4;
            break;
        case 4:
            pending_state = 1;
            ai_result |= 2;
            t56_fan();
            cur_obj->throttle = 0x14;
            /* fallthrough */
        case 5:
            if (cur_obj->frame_idx == an_1F48[0] - 1) {
                cur_obj->misc = 6;
                cur_obj->throttle = 2;
            }
            break;
        case 6:
            pending_state = 2;
            ai_result |= 2;
            /* fallthrough */
        case 7:
            if (cur_obj->frame_idx == an_1F62[0] - 1) {
                cur_obj->aux = rtl_rand() % 3;
                cur_obj->misc = 0;
            }
            break;
        }
        ai_result |= 4;
        break;
    case 4:
        add_score(0x30000L);
        boss2_burst();
        ai_result |= 1;
        break;
    }
}

/* @17EC:0607 file:19CC7 ai_t57 — декорация: спрайт 0x14E + anim_len */
void ai_t57(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 0: case 1:
        o = cur_obj;
        o->tick_period = 0xFF;
        o->num_bboxes = 1;
        o->box[0].sprite = o->anim_len + 0x14E;
        stamp_object(o);
        break;
    }
}
