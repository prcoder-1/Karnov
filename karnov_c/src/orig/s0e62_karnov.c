/* Модуль 0x0E62 (file 0xFE20) — Карнов: камера, вход в уровень, состояния
 * игрока и гибель.
 *
 * Состояние игрока — обработчик объекта игрока (ставится queue_state).
 * В каждом: cur_state 1 — вход, 2 — ход (ввод → скорость), 3 — разбор
 * упоров plr.coll после move_with_collision, 4 — ранен (гибель).
 * Атрибуты клеток в упорах: 1, 2 — пусто, 3, 4 — стена, 5 — ступень,
 * 6 — склон, 7 — лестница, 8 — вода, 9 — батут. */
#include <string.h>
#include "orig/fn.h"
#include "orig/hw.h"

/* @0E62:0007 file:0FE27 cam_hist_fill */
static void cam_hist_fill(void)
{
    s16 si;
    for (si = 0; si < 4; si++) {
        cam_hist[si * 2] = plr.player_x;
        cam_hist[si * 2 + 1] = plr.player_y;
    }
}

/* @0E62:0032 file:0FE52 cam_track — смещения камеры к цели по 1/4 */
void cam_track(s16 tx, s16 ty)
{
    s16 si = tx - cam_dx;
    if ((si <= 0 ? -si : si) > 4)
        cam_dx += si / 4;
    si = ty - cam_dy;
    if ((si <= 0 ? -si : si) > 4)
        cam_dy += si / 4;
}

/* @0E62:0081 file:0FEA1 camera_update — вид по сглаженной позиции игрока
 * (x — среднее 4 кадров, y — взвешенное 8:4:2:1); в EGA и в состоянии 3 x
 * игрока выравнивается на 4 */
void camera_update(void)
{
    s16 si, di, sx = 0, sy = 0, cx, cy;
    for (si = 0, di = 3; si < 4; si++, di--) {
        sx += cam_hist[si * 2];
        sy += cam_hist[si * 2 + 1] << di;
    }
    if (plr.player_state == 3 || level_mode == 1)
        cx = player_ptr->x_world & 0xFFFC;
    else
        cx = sx >> 2;
    cy = sy / 15;
    for (si = 0; si < 3; si++) {
        cam_hist[si * 2] = cam_hist[(si + 1) * 2];
        cam_hist[si * 2 + 1] = cam_hist[(si + 1) * 2 + 1];
    }
    cam_hist[6] = plr.player_x;
    cam_hist[7] = plr.player_y;
    memcpy(prev_view, view_rect, 8);
    if (level_mode == 2)
        cy &= 0xFFFE;
    view_rect[0] = (s16)((cx - cam_dx) >> tile_shift) - vb_view_bytes;
    view_rect[2] = view_rect[0] + vid_AF20;
    view_rect[1] = cy - cam_dy - vid_B82E;
    view_rect[3] = view_rect[1] + vid_B832;
    rect_move_inside(view_rect, cam_rect);
    clip_rect[0] = view_rect[0] + vb_view_bytes;
    clip_rect[1] = view_rect[1] + vid_B82E;
    clip_rect[2] = view_rect[2] - vb_stride;
    clip_rect[3] = view_rect[3] - vid_B826;
    video_offset = VB_ROW(clip_rect[1]) + clip_rect[0];
}

/* @0E62:01E7 file:10007 set_player_state — кроме состояния 9 */
void set_player_state(s16 s)
{
    if (plr.player_state != 9)
        plr.player_state = s;
}

/* @0E62:01F9 file:10019 level_enter — точка возрождения по региону,
 * игрок, камера, сброс состояния, таймер уровня, HUD, музыка */
void level_enter(void)
{
    s16 on_ground = 1, si;
    ObjRegion *r;
    EnemySpawn *e;
    sprite_phase_on = 1;
    plr.respawn_x += 0xBB;
    for (si = 0, r = regions; si < region_count; si++, r++) {
        if (r->x0 > plr.respawn_D8CC || r->x1 <= plr.respawn_x || r->x0 > plr.respawn_x ||
            r->y1 <= plr.respawn_y || r->y0 > plr.respawn_y)
            continue;
        player_spawn.wx = regions[si].spawn_x;
        player_spawn.wy = regions[si].spawn_y;
        if (plr.current_level == 5 && si == 1)
            on_ground = 0;
        si = 0x3E7;
    }
    player_ptr = spawn_active_object(&player_spawn);
    if (on_ground == 0)
        player_ptr->vy_hi = 1;
    plr.coll.masks = NULL;
    if (on_ground)
        place_on_ground(player_ptr, 0x62, 0);
    else
        k_start_state3();
    update_bboxes(player_ptr);
    plr.player_x = player_ptr->x_world;
    plr.player_y = player_ptr->y_world;
    cam_dx = 0x1E;
    cam_dy = 0x46;
    sky_x0 = plr.player_x;
    sky_timer = 0;
    cam_hist_fill();
    for (si = 0, e = spawns; si < spawn_count; si++, e++)
        e->spawned = e->x + e->rx < player_ptr->x_world;
    plr.player_state = 0xA;
    plr.plr_D8AE = 0;
    plr.player_dir = 1;
    plr.weapon = 0;
    plr.fire_cooldown = 0;
    plr.plr_D8B4 = 0;
    plr.plr_D8B6 = 0;
    plr.plr_D8B8 = 0;
    plr.plr_D8BA = 0;
    player_ptr->hit_points = 5;
    shots_alive = 0;
    plr.item_chain = NULL;
    for (si = 0; si < 4; si++)
        plr.item_active[si] = 0;
    bcd_set(plr.ammo_digits, 2, 0);
    plr.ammo_dirty = 1;
    bcd_set(plr.time_digits, 3, level_time[plr.current_level]);
    plr.hurry = 0;
    hud_options();
    hud_redraw();
    camera_update();
    redraw_rect(view_rect);
    play_music(MUSIC(0x04CE), 0, 1);
    if (opt_joystick == 0) {
        inp_mask = 0;
        kb_mask = inp_option = inp_fire = inp_dir = 0;
        inp_dir_changed = 1;
        inp_key = '5';
        for (si = 0; si < 0x28; si++)
            read_key();
    }
}

/* следующее состояние игрока — сразу, в этом же такте */
static void k_switch(farfn h)
{
    queue_state(cur_obj, h);
    handoff_obj = cur_obj;
    ai_result |= 8;
}

/* @0E62:0486 file:102A6 k_wall — упор k в стену: встать вплотную,
 * запретить направление в plr.plr_D8AE */
static void k_wall(s16 k)
{
    s16 d = plr.coll.dir[k], si = plr.coll.x[k], di = 2;
    if (d == 1 || d == 2) {
        si -= sprite_w[cur_obj->box[0].sprite];
        di = 1;
    }
    plr.plr_D8AE |= di;
    cur_obj->frame_idx = 0;
    cur_obj->x_world = si;
    cur_obj->vx_hi = 0; cur_obj->vx_lo = 0;
    cur_obj->ax_hi = 0; cur_obj->ax_lo = 0;
    ai_result |= 4;
}

/* @0E62:0502 file:10322 k_land — на землю: присед (вниз) или ходьба */
static void k_land(s16 k)
{
    plr.coll.n = 0;
    k_switch(inp_dir & 4 ? k_crouch : k_walk);
    cur_obj->frame_idx = 2;
    cur_obj->y_world = plr.coll.y[k] - 0x1A;
    update_bboxes(cur_obj);
}

/* @0E62:0585 file:103A5 k_bounce_start — батут */
static void k_bounce_start(s16 k)
{
    k_switch(k_bounce);
    cur_obj->y_world = plr.coll.y[k];
}

/* @0E62:05C0 file:103E0 k_start_state3 */
void k_start_state3(void)
{
    k_switch(k_state3);
}

/* @0E62:05EF file:1040F k_fall — сорваться: прыжок в фазе 3 с ay 0.3125 */
static void k_fall(s16 k)
{
    k_switch(k_jump);
    plr.karnov_mode = 3;
    cur_obj->y_world += 4;
    cur_obj->vy_hi = 0; cur_obj->vy_lo = 0;
    cur_obj->ay_hi = 0; cur_obj->ay_lo = 0x5000;
    cur_obj->x_world += plr.player_dir & 1 ? 1 : -2;
    SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, LONG(cur_obj->vx_hi, cur_obj->vx_lo) >> 3);
    cur_obj->ax_hi = 0; cur_obj->ax_lo = 0;
    (void)k;
}

/* @0E62:0683 file:104A3 karnov_die — гибель: состояние s, фаза mode;
 * место гибели — точка возрождения */
void karnov_die(s16 s, s16 mode)
{
    queue_state(player_ptr, karnov_death);
    set_player_state(s);
    plr.karnov_mode = mode;
    plr.respawn_x = player_ptr->x_world;
    plr.respawn_y = player_ptr->y_world;
    plr.firepower = 1;
}

/* @0E62:06C6 file:104E6 die_water */
void die_water(s16 k)
{
    karnov_die(7, 4);
    handoff_obj = player_ptr;
    ai_result |= 8;
    player_ptr->y_world += 4;
    (void)k;
    play_sound(7);
}

/* @0E62:06FE file:1051E die_hit */
void die_hit(void)
{
    karnov_die(6, 4);
}

/* @0E62:0712 file:10532 attr_head */
s16 attr_head(void)
{
    return map_attr(plr.cell_col, plr.cell_row_head);
}

/* @0E62:0727 file:10547 attr_feet */
s16 attr_feet(void)
{
    return map_attr(plr.cell_col, plr.cell_row_feet);
}

/* @0E62:073C file:1055C ladder_kind — что под ногами (вниз) или над головой
 * (вверх) на лестнице: 0 — продолжать, 1 — верх, 2 — выход наверх,
 * 3 — низ, 4 — сорваться, 5 — состояние 3 */
static s16 ladder_kind(void)
{
    static const u8 down[10] = { 3, 0, 4, 0, 3, 3, 3, 0, 0, 5 };
    static const u8 up[5] = { 1, 1, 2, 1, 1 };
    u16 a;
    if (LONG(player_ptr->vy_hi, player_ptr->vy_lo) == 0)
        return 0;
    if (LONG(player_ptr->vy_hi, player_ptr->vy_lo) > 0) {
        a = (u16)attr_feet();
        return a <= 9 ? down[a] : 0;
    }
    a = (u16)attr_head();
    return a <= 4 ? up[a] : 0;
}

/* @0E62:07C3 file:105E3 ladder_grab */
void ladder_grab(void)
{
    center_on_column(player_ptr, ladder_col, sprite_w[0x78]);
    player_ptr->x_world--;
    queue_state(player_ptr, k_climb);
}

/* @0E62:07FF file:1061F ladder_grab_down */
void ladder_grab_down(void)
{
    ladder_grab();
    player_ptr->y_world += 0x16;
    player_ptr->box[0].sprite = 0x7B;
    player_ptr->throttle = 8;
    update_bboxes(player_ptr);
}

/* @0E62:082B file:1064B ladder_exit_top */
static void ladder_exit_top(void)
{
    queue_state(player_ptr, k_walk);
    player_ptr->throttle = 8;
    snap_to_ground(player_ptr, 0x62, 0x80, plr.cell_col, plr.cell_row_head - 2);
    player_ptr->vy_hi = 0; player_ptr->vy_lo = 0;
    player_ptr->y_world += 2;
    player_ptr->box[0].dy = 0x1A;
    player_ptr->box[0].sprite = 0x7B;
    update_bboxes(player_ptr);
}

/* @0E62:08A1 file:106C1 boost_x — предмет 1: скорость ×1.5 */
static void boost_x(void)
{
    s32 v;
    if (plr.plr_D8B4 == 0)
        return;
    v = LONG(cur_obj->vx_hi, cur_obj->vx_lo);
    SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, (v >> 1) + v);
}

/* @0E62:08D5 file:106F5 boost_y */
static void boost_y(void)
{
    s32 v;
    if (plr.plr_D8B4 == 0)
        return;
    v = LONG(cur_obj->vy_hi, cur_obj->vy_lo);
    SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, (v >> 1) + v);
}

/* @0E62:0909 file:10729 slope_y — y на склоне (атрибут 6, тайлы 0x37..0x3A) */
static void slope_y(void)
{
    s16 x, si, di, row, attr;
    u8 tile;
    s32 off = 0;
    x = player_ptr->x_world + player_ptr->vx_hi + (sprite_w[0x62] >> 1);
    si = player_ptr->y_world + player_ptr->vy_hi + sprite_h[0x62];
    di = PX_COL(x >> tile_shift);
    row = PX_TROW(si);
    attr = map_attr(di, row);
    tile = (u8)map_tile(di, row);
    if (attr == 6) {
        switch (tile) {
        case 0x38:
            off += (s32)5 << 16;
            /* fallthrough */
        case 0x37: case 0x39: case 0x3A:
            off += (s32)((u32)(u16)((x + 7) % 8 * 10) << 16) / 16;
            break;
        }
    }
    cur_obj->frame_idx = 2;
    cur_obj->y_world = si - si % 10 + (s16)(off >> 16) - 0x1A;
}

/* @0E62:0A1D file:1083D slope_land */
static void slope_land(void)
{
    s16 si;
    plr.coll.n = 0;
    plr.coll.any = 0;
    k_switch(k_slope);
    if (player_ptr->vx_hi == 0)
        si = 0;
    else
        si = player_ptr->vx_hi > 0 ? 1 : -1;
    snap_to_ground(player_ptr, 0x62, 0x41, plr.cell_col + si, plr.cell_row_head);
    player_ptr->vy_hi = 0; player_ptr->vy_lo = 0;
    player_ptr->ay_hi = 0; player_ptr->ay_lo = 0;
    slope_y();
}

/* @0E62:0ABE file:108DE ladder_bottom — сход с лестницы по атрибуту под
 * ногами; предметы на лестнице поблизости подбираются */
static void ladder_bottom(void)
{
    ActiveObject *o;
    s16 si, di;
    player_ptr->vy_hi = 0; player_ptr->vy_lo = 0;
    player_ptr->y_world += 0xA;
    switch (attr_feet()) {
    case 0: case 4:
        queue_state(player_ptr, k_walk);
        snap_to_ground(player_ptr, 0x62, 1, plr.cell_col, plr.cell_row_head);
        break;
    case 5:
        queue_state(player_ptr, k_walk);
        snap_to_ground(player_ptr, 0x62, 0x20, plr.cell_col, plr.cell_row_head);
        break;
    case 6:
        slope_land();
        break;
    }
    for (o = plr.item_chain; o != NULL; o = o->parent) {
        si = player_ptr->x_world - o->x_world;
        if (si <= 0)
            si = -si;
        di = player_ptr->y_world + 0x1A - sprite_h[0x18] - o->y_world;
        if (di <= 0)
            di = -di;
        if (si < 5 && di < 0xF) {
            o->state_byte = 6;
            item_pickup(0);
        }
    }
}

/* @0E62:0C49 file:10A69 k_appear — AI-тип 1: появление игрока */
void k_appear(void)
{
    ActiveObject *o = cur_obj;
    switch (cur_state) {
    case 0: case 1:
        o->x_world &= 0xFFFC;
        o->state_list = sl_14E0;
        o->frame_ctr = 0;
        o->active = 0;
        o->frame_idx = 0;
        o->box[0].sprite = 0;
        break;
    case 2:
        if (o->frame_ctr == 6) {
            pending_state = 0;
            ai_result |= 6;
            o->active = 1;
        }
        if (cur_obj->active == 0)
            break;
        if (cur_obj->frame_idx == anim_frames(cur_obj) - 1)
            queue_state(cur_obj, k_walk);
        ai_result |= 4;
        break;
    }
}

/* @0E62:0CF6 file:10B16 k_walk — ходьба */
void k_walk(void)
{
    switch (cur_state) {
    case 1:
        cur_obj->state_list = sl_1500;
        set_player_state(0);
        plr.coll.masks = an_1510;
        cur_obj->box[0].hitflag = 1;
        /* fallthrough */
    case 2:
        k_i = ~plr.plr_D8AE & inp_dir & 3;
        if (k_i == 0)
            cur_obj->frame_idx = 0;
        set_motion(k_i);
        boost_x();
        dir_latch_release();
        pending_state = plr.player_dir & 1;
        ai_result |= 6;
        break;
    case 3:
        for (k_i = 0; k_i < plr.coll.n; k_i++) {
            switch (plr.coll.attr[k_i]) {
            case 1: case 2:
                if (plr.coll.dir[k_i] == 4)
                    k_fall(k_i);
                break;
            case 3: case 4:
                if (plr.coll.dir[k_i] != 4)
                    k_wall(k_i);
                break;
            case 6:
                if (plr.coll.dir[k_i] == 4)
                    slope_land();
                break;
            case 8:
                die_water(k_i);
                break;
            }
        }
        break;
    case 4:
        die_hit();
        break;
    }
}

/* @0E62:0E20 file:10C40 k_slope — ходьба по склону: vy = vx·10/16 */
void k_slope(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        set_player_state(0);
        plr.coll.masks = an_151A;
        cur_obj->box[0].hitflag = 1;
        cur_obj->state_list = sl_1500;
        /* fallthrough */
    case 2:
        k_i = ~plr.plr_D8AE & inp_dir & 3;
        o = cur_obj;
        SETLONG(o->vx_hi, o->vx_lo, motion_table[k_i].vx);
        o->ax_hi = 0; o->ax_lo = 0;
        o->ay_hi = 0; o->ay_lo = 0;
        if (k_i == 0)
            o->frame_idx = 0;
        else
            cur_obj->vx_hi++;
        boost_x();
        SETLONG(cur_obj->vy_hi, cur_obj->vy_lo,
                LONG(cur_obj->vx_hi, cur_obj->vx_lo) * 10 / 16);
        dir_latch_release();
        pending_state = plr.player_dir & 1;
        ai_result |= 6;
        break;
    case 3:
        for (k_i = 0; k_i < plr.coll.n; k_i++) {
            switch (plr.coll.attr[k_i]) {
            case 0:
                if (plr.coll.dir[k_i] == 4)
                    k_land(k_i);
                break;
            case 2:
                if (plr.coll.dir[k_i] == 4)
                    k_fall(k_i);
                break;
            case 3: case 4:
                if (plr.coll.dir[k_i] != 4)
                    k_wall(k_i);
                break;
            case 8:
                die_water(k_i);
                break;
            }
        }
        break;
    case 4:
        die_hit();
        break;
    }
}

/* @0E62:0FAE file:10DCE k_crouch — присед; на ступени (атрибут 5)
 * съезжает на полпикселя влево за ход */
void k_crouch(void)
{
    switch (cur_state) {
    case 1:
        set_player_state(8);
        cur_obj->box[0].hitflag = 1;
        cur_obj->box[0].dy = 0x1A - sprite_h[0x67];
        set_motion(0);
        /* fallthrough */
    case 2:
        if (inp_dir & 4) {
            cur_obj->box[0].sprite = plr.player_dir & 1 ? 0x6F : 0x67;
        } else {
            k_switch(k_walk);
        }
        if (attr_feet() == 5)
            SETLONG(cur_obj->x_world, cur_obj->x_lo,
                    LONG(cur_obj->x_world, cur_obj->x_lo) - 0x8000);
        break;
    case 4:
        die_hit();
        break;
    }
}

/* @0E62:1071 file:10E91 k_climb — лестница */
void k_climb(void)
{
    switch (cur_state) {
    case 1:
        cur_obj->tick_period = 2;
        set_player_state(5);
        plr.coll.masks = an_1546;
        cur_obj->box[0].hitflag = 1;
        cur_obj->state_list = sl_153E;
        pending_state = 0;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        k_kind = ladder_kind();
        switch (k_kind) {
        case 0: case 1:
            k_i = (k_kind == 1 ? (s16)0xFFF7 : (s16)0xFFFF) & ~plr.plr_D8AE & inp_dir & 0xC;
            if (k_i == 0)
                cur_obj->frame_idx = 0;
            set_motion(k_i);
            boost_y();
            dir_latch_release();
            ai_result |= 4;
            break;
        case 2:
            ladder_exit_top();
            break;
        case 3:
            ladder_bottom();
            break;
        case 4:
            k_fall(0);
            break;
        case 5:
            k_start_state3();
            break;
        }
        break;
    case 4:
        die_hit();
        break;
    }
}

/* @0E62:1165 file:10F85 k_jump — прыжок и падение. karnov_mode: 0 — вверх,
 * 1 — вниз, 2 — ниже точки отрыва на 7+, 3 — срыв */
void k_jump(void)
{
    ActiveObject *o;
    Motion *m;
    switch (cur_state) {
    case 1:
        set_player_state(1);
        plr.jump_y0 = plr.player_y;
        plr.coll.masks = an_1580;
        inp_dir_changed = 1;
        cur_obj->state_list = sl_1560;
        pending_state = plr.player_dir & 1;
        ai_result |= 6;
        /* fallthrough */
    case 2:
        if (plr.karnov_mode == 3) {
            pending_state = (plr.player_dir & 1) + 2;
            ai_result |= 6;
        } else if (inp_dir_changed != 0) {
            k_i = ~plr.plr_D8AE & inp_dir & 3;
            m = &motion_table[k_i];
            o = cur_obj;
            SETLONG(o->vx_hi, o->vx_lo, m->vx);
            SETLONG(o->ax_hi, o->ax_lo, m->ax);
            boost_x();
            pending_state = plr.player_dir & 1;
            ai_result |= 6;
        }
        if (plr.karnov_mode != 2 && plr.karnov_mode != 3 &&
            cur_obj->y_world + cur_obj->vy_hi > plr.jump_y0 + 7) {
            plr.karnov_mode = 2;
            break;
        }
        if (plr.karnov_mode != 0)
            break;
        if (LONG(cur_obj->vy_hi, cur_obj->vy_lo) > 0)
            plr.karnov_mode = 1;
        break;
    case 3:
        for (k_i = 0; k_i < plr.coll.n; k_i++) {
            switch (plr.coll.attr[k_i]) {
            case 0: case 5:
                if (plr.coll.dir[k_i] == 4)
                    k_land(k_i);
                break;
            case 1:
                if (player_ptr->vy_hi < 0 &&
                    (plr.coll.dir[k_i] == 0 || plr.coll.dir[k_i] == 1)) {
                    cur_obj->vy_hi = 0;
                    cur_obj->vy_lo = 0;
                }
                break;
            case 3:
                k_wall(k_i);
                break;
            case 4:
                if (plr.coll.dir[k_i] == 4)
                    k_land(k_i);
                else
                    k_wall(k_i);
                break;
            case 6:
                if (plr.coll.dir[k_i] == 4)
                    slope_land();
                break;
            case 7:
                if (plr.coll.dir[k_i] == 4 &&
                    attr_at(plr.coll.x[k_i], plr.coll.y[k_i] - 10) == 2)
                    k_land(k_i);
                break;
            case 8:
                die_water(k_i);
                break;
            case 9:
                k_bounce_start(k_i);
                break;
            }
        }
        break;
    case 4:
        die_hit();
        break;
    }
}

/* @0E62:13C6 file:111E6 k_fly — AI-тип 2 и предмет 6: свободный полёт,
 * пока plr.plr_D8B6 > 0 */
void k_fly(void)
{
    s32 v;
    switch (cur_state) {
    case 1:
        cur_obj->state_list = sl_15A6;
        set_player_state(4);
        plr.coll.masks = an_15B6;
        cur_obj->box[0].hitflag = 1;
        pending_state = 0;
        ai_result |= 2;
        cur_obj->tick_period = 4;
        /* fallthrough */
    case 2:
        if (plr.plr_D8B6 <= 0) {
            k_fall(0);
            break;
        }
        k_i = ~plr.plr_D8AE & inp_dir;
        set_motion(k_i);
        if (plr.plr_D8B4 != 0) {
            v = LONG(cur_obj->vx_hi, cur_obj->vx_lo);
            SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, (s32)((u32)v << 1));
            v = LONG(cur_obj->vy_hi, cur_obj->vy_lo);
            SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, (s32)((u32)v << 1));
        }
        dir_latch_release();
        pending_state = plr.player_dir & 1;
        ai_result |= 6;
        break;
    case 3:
        for (k_i = 0; k_i < plr.coll.n; k_i++)
            plr.plr_D8AE |= an_15B6[plr.coll.dir[k_i] + 5];
        break;
    case 4:
        die_hit();
        break;
    }
}

/* @0E62:14D7 file:112F7 k_state3 — AI-тип 3: player_state 3, движение во
 * все стороны вдвое медленнее без предмета 8 (plr.plr_D8BA); лестница —
 * выход на неё */
void k_state3(void)
{
    ActiveObject *o;
    switch (cur_state) {
    case 1:
        cur_obj->misc = 0;
        item_set_active(0xF, 1);
        set_player_state(3);
        plr.coll.masks = an_164A;
        cur_obj->box[0].hitflag = 1;
        cur_obj->state_list = sl_160A;
        pending_state = 0;
        ai_result |= 2;
        /* fallthrough */
    case 2:
        o = cur_obj;
        o->active = 0;
        if (--o->misc < 0) {
            o->misc = 6;
            o->active = 1;
        }
        k_i = ~plr.plr_D8AE & inp_dir;
        set_motion(k_i);
        if (plr.plr_D8BA == 0) {
            SETLONG(cur_obj->vx_hi, cur_obj->vx_lo, LONG(cur_obj->vx_hi, cur_obj->vx_lo) >> 1);
            SETLONG(cur_obj->vy_hi, cur_obj->vy_lo, LONG(cur_obj->vy_hi, cur_obj->vy_lo) >> 1);
        }
        cur_obj->vx_lo &= 0x8000;
        dir_latch_release();
        pending_state = (inp_dir & 4 ? 2 : 0) + (plr.player_dir & 1) + (plr.plr_D8BA != 0 ? 4 : 0);
        ai_result |= 6;
        break;
    case 3:
        for (k_i = 0; k_i < plr.coll.n; k_i++) {
            switch (plr.coll.attr[k_i]) {
            case 1: case 2:
                o = cur_obj;
                if (o->vy_hi < 0) {
                    o->vy_hi = 0; o->vy_lo = 0;
                    o->ay_hi = 0; o->ay_lo = 0;
                }
                cur_obj->y_world = plr.coll.y[k_i];
                plr.plr_D8AE |= an_164A[plr.coll.dir[k_i] + 5] & 0xC;
                break;
            case 7:
                item_set_active(0xF, 0);
                plr.plr_D8BA = 0;
                ladder_col = plr.coll.col[k_i];
                ladder_grab();
                break;
            default:
                plr.plr_D8AE |= an_164A[plr.coll.dir[k_i] + 5];
                break;
            }
        }
        break;
    case 4:
        die_hit();
        break;
    }
}

/* @0E62:16F3 file:11513 k_bounce — батут: vy /= 2 и подъём с ay -0.15625,
 * на вершине — состояние 3 */
void k_bounce(void)
{
    ActiveObject *o = cur_obj;
    switch (cur_state) {
    case 1:
        set_player_state(3);
        o->box[0].hitflag = 0;
        o->active = 0;
        o->vx_hi = 0; o->vx_lo = 0;
        o->ax_hi = 0; o->ax_lo = 0;
        SETLONG(o->vy_hi, o->vy_lo, LONG(o->vy_hi, o->vy_lo) / 2);
        o->ay_hi = -1; o->ay_lo = 0xD800;
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->vy_hi < 0) {
            o->vy_hi = 0; o->vy_lo = 0;
            o->box[0].hitflag = 1;
            k_start_state3();
        }
        break;
    }
}

/* @0E62:1791 file:115B1 karnov_death — падение с экрана (или оседание при
 * упоре ступней), музыка смерти; по её концу и в аним-состоянии 2 —
 * end_life */
void karnov_death(void)
{
    ActiveObject *o;
    ai_result |= 4;
    switch (cur_state) {
    case 1:
        o = cur_obj;
        o->tick_period = 8;
        o->state_list = sl_1682;
        plr.coll.masks = an_169A;
        o->num_bboxes = 1;
        o->box[0].hitflag = 0;
        o->vx_hi = 0; o->vx_lo = 0;
        o->ax_hi = 0; o->ax_lo = 0;
        SETLONG(o->ay_hi, o->ay_lo, LONG(o->ay_hi, o->ay_lo) +
                (attr_head() == 9 ? 0x2000 : 0x8000));
        o->misc = 1;
        pending_state = 0;
        ai_result |= 6;
        play_music(MUSIC(0x09D9), 0x64, 0);
        /* fallthrough */
    case 2:
        o = cur_obj;
        if (o->misc != 0) {
            if (o->y_world > view_rect[3] + 0x64) {
                o->num_bboxes = 0;
                o->misc = 0;
                o->frame_ctr = 0;
                pending_state = 1;
                ai_result |= 2;
            }
        } else if (o->frame_idx == 0) {
            pending_state = 2;
            ai_result |= 2;
        }
        if (music_done() != 0 && cur_obj->next_state_id == 2)
            end_life();
        break;
    case 3:
        if (plr.karnov_mode == 7 || cur_obj->misc == 0)
            break;
        for (k_i = 0; k_i < plr.coll.n; k_i++) {
            if (plr.coll.dir[k_i] != 4)
                continue;
            o = cur_obj;
            o->y_world = plr.coll.y[k_i] - sprite_h[o->box[0].sprite];
            o->vy_hi = 0; o->vy_lo = 0;
            o->ay_hi = 0; o->ay_lo = 0;
            o->misc = 0;
            o->frame_ctr = 0;
            pending_state = 1;
            ai_result |= 6;
        }
        break;
    case 4:
        cur_obj->state_byte = 2;
        cur_state = 2;
        ai_dispatch();
        break;
    }
}
