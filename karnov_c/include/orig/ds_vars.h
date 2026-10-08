/* orig/ds_vars.h — реестр глобалов KARNOV.EXE: тип, имя, адрес в DS.
 * Включается из ds.h (объявления) и ds.c (определения + реестр).
 * Группировка по модулям; пересечения запрещены (ds_check).
 *
 *  DSV(тип, имя, адрес)         скаляр
 *  DSA(тип, имя, адрес, n)      массив
 *  DSS(тип, имя, адрес, n)      массив структур с раскладкой тип_layout
 *  DSP(тип, имя, адрес)         far-указатель на данные
 *  DSPA(тип, имя, адрес, n)     массив far-указателей на данные
 *  DSF(имя, адрес)              far-указатель на код
 *  DSFA(имя, адрес, n)          массив far-указателей на код
 *  DSX(тип, имя, адрес, n, разм) массив вне сверки (разм элемента в оригинале)
 *  DSVX(тип, имя, адрес, разм)  скаляр вне сверки (разм — в оригинале)
 */

/* --- оборудование, консоль (модули 0x0330, 0x0361) */
DSV(s16, machine_kind,   0x0124)    /* detect_machine (@01F3:0007) */
DSV(s16, level_mode,     0x0360)    /* 0 CGA, 1 EGA, 2 Hercules, 3 Tandy */
DSA(char, s_bkinit_failed,      0x03A8, 16)
DSA(char, s_level_init_failed,  0x03B8, 20)
DSA(char, s_sprite_init_failed, 0x03CC, 20)
DSA(char, s_press_key_video,    0x03E0, 39)
DSA(char, s_menu_cga,           0x0407, 18)
DSA(char, s_menu_ega,           0x0419, 18)
DSA(char, s_menu_tga,           0x042B, 18)
DSA(char, s_menu_mono,          0x043D, 34)
DSA(char, s_cant_force_herc,    0x045F, 27)
DSV(u8,  key_pending,    0x047A)    /* скан-код расширенной клавиши */

/* --- рантайм Turbo-C */
DSV(s16, fp_errno,       0x0073)    /* обнуляется в sqrt */
DSV(u32, rand_seed,      0x24DA)
DSV(u16, rtl_psp,        0x006F)    /* сегмент PSP */
DSA(u16, rtl_heapbase,   0x0077, 2) /* far-куча: off, seg */
DSA(u16, rtl_brklvl,     0x007B, 2)
DSA(u16, rtl_heaptop,    0x007F, 2)
DSV(u16, rtl_brk_kb,     0x22B2)    /* размер блока программы в КБ */
DSA(u16, rtl_heap_node,  0x246C, 4) /* фиктивный узел свободного списка */
DSA(u16, rtl_heap_rover, 0x2474, 2)

/* --- видео: детект и буфер (модуль 0x015F) */
DSA(char, s_sprites_ega_a, 0x00A4, 12)
DSA(char, s_rb_a,          0x00B0, 3)
DSA(char, s_crlf_a,        0x00B3, 3)
DSA(char, s_crlf_b,        0x00B6, 3)
DSA(char, s_crlf_c,        0x00B9, 3)
DSA(char, s_sprites_ega_b, 0x00BC, 12)
DSA(char, s_rb_b,          0x00C8, 3)
DSA(char, s_sprites_ega_c, 0x00CB, 12)
DSA(char, s_rb_c,          0x00D7, 3)
DSA(char, s_video_dev_err, 0x00DA, 19)
DSA(char, s_no_mem_buffer, 0x00ED, 29)
DSA(char, s_no_mem_map,    0x010A, 26)
DSA(char, s_insert_prog,   0x0362, 27)
DSA(char, s_insert_disk,   0x037D, 24)
DSA(char, s_any_key,       0x0395, 19)
DSVX(ObjRegion *, regions,  0x047C, 4)     /* куча: region_count записей */
DSA(EnemySpawn, player_spawn, 0x0480, 1)    /* запись спауна игрока */
DSVX(EnemySpawn *, spawns,  0x0494, 4)      /* куча: spawn_count, по x */
DSA(char, s_level_fmt,     0x0498, 13)
DSA(char, s_rb_lvl_a,      0x04A5, 3)
DSA(char, s_rb_lvl_b,      0x04A8, 3)
DSA(char, s_map_err,       0x04AB, 11)
DSA(char, s_crlf_d,        0x04B6, 3)
DSA(char, s_sprite_bummer, 0x04B9, 18)
DSA(char, s_crlf_e,        0x04CB, 3)
DSA(u8,  music_data,      0x04CE, 0x649)    /* скрипты музыки; music_* — смещения */
DSA(u8,  sfx_data,        0x0B17, 0x119)    /* скрипты эффектов */

/* --- объекты (модуль 0x006E) */
DSV(s16, objloop_chain,  0x258C)    /* индекс parity-цепочки во внешних циклах */
DSV(s16, objloop_k,      0x258E)    /* позиция в active_index (шаг 2) */
DSV(s16, objloop_end,    0x2590)
DSV(s16, obj_2592,       0x2592)
DSA(s16, active_index,   0x2594, 128)    /* по parity-цепочкам: чётные — 0, нечётные — 1 */
DSA(s16, active_count,   0x2694, 2)
DSA(s16, stamp_list,     0x2698, 0x41)   /* индексы впечатанных в фон объектов */
DSV(s16, obj_271A,       0x271A)
DSV(s16, pair_dy,        0x271C)         /* грубый отсев пар по |dy| */
DSV(s16, pair_count,     0x271E)
DSV(s16, pair_dx,        0x2720)         /* грубый отсев пар по |dx| */
DSA(s16, pair_table,     0x2722, 128)    /* пары пересечений (индексы) */
DSV(s16, alloc_idx,      0x2822)
DSS(ActiveObject, all_objects, 0x2824, 128)
DSV(s16, sprite_phase_on, 0xA2A4)        /* учитывать подпиксельную фазу x при блите */
DSPA(ActiveObject, free_head, 0xA2A6, 2)
DSV(s16, stamp_count,    0xA2AE)

/* --- таблицы спрайтов */
DSA(u8,  sprite_w,       0x0D08, 0x152)  /* ширина в единицах x_world */
DSA(u8,  sprite_wb,      0x0E5A, 0x152)  /* ширина в байтах CGA/Tandy */
DSA(u8,  sprite_h,       0x0FAC, 0x152)
DSA(s16, clip_rect,      0xB834, 4)      /* x0, y0, x1, y1 — в колонках/строках буфера */
DSA(s16, view_rect,      0xCC8E, 4)

/* --- RLE-ввод (модуль 0x058B) */
DSV(s16, io_buf_avail,   0xD566)
DSV(s16, rle_in_run,     0xD568)
DSA(u8,  io_buffer,      0xD56A, 0x200)
DSV(u8,  rle_run_byte,   0xD76A)
DSV(s16, io_buf_pos,     0xD76B)
DSV(u8,  rle_run_left,   0xD76D)

/* --- состояние машины (модуль 0x0330) */
DSV(s16, bios_equip_saved,   0xCDE8)
DSV(s16, bios_kbflags_saved, 0xCDEE)
DSV(s16, cpu_type,           0xCDF0) /* 0x86/0x186/0x286/0x386; выбирает ветки блиттеров */
DSV(s16, saved_video_mode,   0xCDF4)

/* --- звук (модуль 0x0CB9) */
DSV(u16, spk_div,            0x1206)    /* последний делитель PIT ch2 */
DSV(u16, mus_pos,            0x1208)
DSV(s16, music_enabled,      0x120A)
DSV(u16, mus_div,            0x120C)    /* 0 = пауза */
DSV(u8,  mus_gap,            0x120E)    /* длительность паузы после ноты */
DSV(u8,  mus_phase,          0x120F)    /* 2 — нота, 1 — пауза */
DSV(u16, mus_prio,           0x1210)
DSP(u8,  mus_script,         0x1212)
DSV(s16, mus_loop,           0x1216)
DSV(u8,  mus_timer,          0x1218)
DSV(u16, sfx_pos,            0x1219)
DSV(u16, sfx_div,            0x121B)
DSV(s16, sfx_enabled,        0x121D)
DSV(u16, sfx_prio,           0x121F)
DSP(u8,  sfx_script,         0x1221)
DSV(u8,  sfx_repeat,         0x1225)
DSV(u8,  sfx_repeat_set,     0x1226)
DSV(u8,  sfx_timer,          0x1227)
DSV(u16, sfx_delta,          0x1228)    /* добавка к делителю на каждом тике */
DSA(u8,  note_len,           0x122A, 0x40) /* пары (нота, пауза), пишет опкод FF */
DSA(u16, pitch_lut,          0x126A, 0x6E)
DSV(u8,  bios_tick_div,      0x1346)    /* 12 тиков → старый INT 8 */
DSV(u8,  tick_phase,         0xD774)    /* нечёт — музыка, чёт — SFX */
DSV(u32, timer_ticks,        0xD775)
DSVX(farfn, old_int8,        0xD779, 4)

/* --- внеэкранный буфер и таблицы смещений (bk_init @015F:0646) */
DSVX(u8 *, vbuf_alloc,       0xA2B0, 4)
DSA(s16, new_vis,            0xA2B4, 4)      /* SCROLLVIEW: view_rect ∩ world_rect */
DSA(u16, vb_row_off,         0xA2BC, 0x47E)  /* y → смещение строки пикселей */
DSA(u16, tile_attr,          0xABC0, 200)  /* атрибут тайла по номеру */
DSV(u16, video_offset,       0xAD50)
DSA(u16, map_row,           0xAD52, 0xE6)   /* far-указатели строк карты: off, seg */
DSV(s16, vid_AF1E,           0xAF1E)
DSV(s16, vid_AF20,           0xAF20)
DSA(s16, px_to_trow,         0xAF22, 0x47E)  /* y → строка тайлов (y/10) */
DSA(s16, old_vis,            0xB81E, 4)      /* SCROLLVIEW: prev_view ∩ world_rect */
DSV(s16, vid_B826,           0xB826)
DSV(s16, tile_shift,         0xB828)
DSVX(u8 *, map_buf,          0xB82A, 4)      /* 0x7992 байт */
DSV(s16, vid_B82E,           0xB82E)
DSV(s16, vid_B832,           0xB832)
DSA(u16, vb_col_off,         0xB83C, 0x113)  /* колонка → смещение в строке */
DSVX(u8 *, vbuf,             0xBA62, 4)      /* выровнен на параграф */
DSV(s16, vb_stride,          0xBA66)         /* байт в строке буфера */
DSA(s16, prev_view,          0xBA70, 4)
DSA(u16, vb_trow_off,        0xBA78, 0x73)   /* строка тайлов → смещение */
DSA(s16, px_to_col,          0xBB5E, 0x898)  /* x → колонка буфера */
DSV(s16, vb_view_bytes,      0xCC9A)
DSV(s16, vb_row_bytes,       0xD76E)

/* --- уровень (модуль 0x0365) */
DSVX(u8 *, tile_gfx,         0x0098, 4)      /* куча: 40 (CGA) или 80 (EGA) байт на тайл */
DSV(s16, map_rows,           0xB830)
DSV(s16, map_cols,           0xCC96)
DSV(s16, map_row_bytes,      0xCC98)         /* map_cols * 2: (тайл, атрибут) */
DSA(char, msg_buf,           0xCCCE, 0x40)
DSV(s16, level_loading,      0xCDF2)
DSV(s16, trig_lo,            0xCDF6)         /* двоичный поиск в CHECKTRIGGERS */
DSV(s16, trig_hi,            0xCDF8)
DSV(s16, spawn_count,        0xCDFA)
DSV(s16, loaded_level,       0xCDFC)         /* 0x3E7 — не загружен */
DSV(s16, region_count,       0xCDFE)
DSV(s16, goal_idx,           0xD959)
DSP(ActiveObject, player_ptr, 0xD95F)

/* --- таблица AI-обработчиков */
DSFA(ai_handlers,            0x10FE, 58)
DSV(s16, demo_level_ctr,     0x035E)
DSV(s16, demo_mode,          0xCDEC)

/* --- спрайты (модуль 0x0BFB) */
DSV(u16, ega_page,           0x0D02)         /* EGA: видимая страница (0 / 8000) */
DSVX(u8 *, sprite_arena,     0x0D04, 4)
DSV(s16, sprites_fixed,      0x11E6)         /* последний закреплённый спрайт; -1 — ещё не грузили */
DSA(char, s_sprites_cga,     0x11E8, 12)
DSA(char, s_sprites_ega_d,   0x11F4, 12)
DSA(char, s_rb_spr_a,        0x1200, 3)
DSA(char, s_rb_spr_b,        0x1203, 3)
DSVX(u8 *, sprites_fixed_end, 0xD770, 4)

/* --- игра (модуль 0x04E4): опции, игроки */
DSV(s16, opt_music,          0x0354)
DSV(s16, opt_sfx,            0x0356)
DSV(s16, opt_joystick,       0x0358)
DSV(s16, two_players,        0x035A)
DSV(s16, attract_on,         0x035C)
DSA(char, s_ready_player,    0x0C5C, 17)
DSA(char, s_copyright,       0x0C6D, 35)
DSA(char, s_adaptation,      0x0C90, 39)
DSA(char, s_the_end,         0x0CB7, 20)
DSA(char, s_game_over,       0x0CCB, 21)
DSV(s16, game_0CDD6,         0xCDD6)
DSV(s16, frame_total,        0xCDD8)
DSV(s32, play_ticks,         0xCDDA)
DSV(s16, frame_no,           0xCDEA)         /* кадр от начала уровня */
DSV(s16, cur_player,         0xCE00)
DSV(s16, in_game,            0xCE02)
DSA(u8,  player_rec,         0xCE04, 0x14A)  /* 2 × 0xA5: копии блока D8AA */
DSV(s16, players_left,       0xCF4E)
DSV(s16, num_players,        0xCF50)
DSV(s16, quit_game,          0xCF52)
DSA(Demo, demo_buf,          0xD258, 1)
DSP(Demo, demo_ptr,          0xD55C)
DSP(Demo, demo_rec_ptr,      0xD560)
DSA(char, s_mov_fmt_a,       0x0CE0, 12)
DSA(char, s_rb_mov_a,        0x0CEC, 3)
DSA(char, s_rb_mov_b,        0x0CEF, 3)
DSA(char, s_mov_fmt_b,       0x0CF2, 12)
DSA(char, s_wb_mov,          0x0CFE, 3)
DSV(s16, game_D564,          0xD564)
DSA(s16, world_rect,         0xABB8, 4)
DSA(s16, cam_rect,           0xBA68, 4)      /* x0 — левая колонка вида */

/* --- блок текущего игрока D8AA..D94E (0xA5 байт, копируется в player_rec).
 * Счётчики — неупакованные десятичные цифры, младшая первой. */
DSV(s16, current_level,      0xD8AA)
DSV(s16, lives,              0xD8AC)
DSV(s16, plr_D8AE,           0xD8AE)
DSV(s16, player_dir,         0xD8B0)
DSV(s16, fire_cooldown,      0xD8B2)
DSV(s16, plr_D8B4,           0xD8B4)         /* предмет 1: 250 */
DSV(s16, plr_D8B6,           0xD8B6)         /* предмет 6: 200 */
DSV(s16, plr_D8B8,           0xD8B8)         /* предмет 5: 150 */
DSV(s16, plr_D8BA,           0xD8BA)         /* предмет 8 */
DSV(s16, player_x,           0xD8BC)         /* копия x_world игрока для AI */
DSV(s16, player_y,           0xD8BE)
DSV(s16, jump_y0,            0xD8C0)         /* y в начале прыжка */
DSV(s16, cell_col,           0xD8C2)         /* клетка игрока (пишет POST_KARNOV) */
DSV(s16, cell_row_head,      0xD8C4)
DSV(s16, cell_row_feet,      0xD8C6)
DSV(s16, respawn_x,          0xD8C8)
DSV(s16, respawn_y,          0xD8CA)
DSV(s16, respawn_D8CC,       0xD8CC)
DSV(s16, player_state,       0xD8CE)
DSV(s16, karnov_mode,        0xD8D0)         /* фаза прыжка: 0..3; 4, 7 — гибель */
DSS(CollRes, coll,           0xD8D2, 1)      /* упоры игрока (move_with_collision) */
DSV(s16, items_owned,        0xD8F8)         /* видов предметов в наличии */
DSV(s16, item_sel,           0xD8FA)         /* выбранный; -1 — нет */
DSA(s16, plr_D8FC,           0xD8FC, 11)
DSA(s16, item_count,         0xD912, 11)
DSA(s16, item_active,        0xD928, 4)      /* предметы 5..8 в действии */
DSA(u8,  score_bcd,          0xD930, 8)
DSV(s16, score_dirty,        0xD938)
DSA(u8,  time_digits,        0xD93A, 3)      /* таймер уровня */
DSV(s16, hurry,              0xD93D)
DSA(u8,  k_digits,           0xD93F, 2)      /* собранные «K»: 30 → жизнь */
DSV(s16, k_dirty,            0xD941)
DSA(u8,  ammo_digits,        0xD943, 2)
DSV(s16, ammo_dirty,         0xD945)
DSV(s16, weapon,             0xD947)
DSV(s16, firepower,          0xD949)
DSP(ActiveObject, item_chain, 0xD94B)        /* предметы на лестницах (через parent) */

/* --- ввод (модуль 0x020F) */
DSV(s16, god_mode,           0xCCA6)         /* чит ZEEK */
DSV(s16, cheat_pos,          0xCCCA)

/* --- предметы и HUD (модуль 0x041D) */
DSFA(item_use_fn,            0x0C30, 9)
DSA(u16, tandy_row_off,      0x0C54, 4)
DSV(s16, inp_scan,           0xCC9C)         /* скан-код последней клавиши */
DSV(u8,  inp_mask,           0xCC9E)         /* 1 R, 2 L, 4 D, 8 U, 0x10 огонь, 0x20 опция */
DSV(s16, inp_key,            0xCC9F)         /* ASCII последней клавиши */
DSV(u8,  inp_prev_mask,      0xCCA1)
DSV(s16, inp_option,         0xCCA2)
DSV(s16, inp_prev_fire,      0xCCA4)
DSV(s16, kb_mask,            0xCCA8)
DSV(s16, inp_fire,           0xCCAA)
DSV(s16, inp_dir,            0xCCAC)
DSV(s16, inp_dir_changed,    0xCCAE)
DSV(s16, joy_x_lo,           0xCCB0)
DSV(s16, joy_x_hi,           0xCCB2)
DSV(s16, joy_y_lo,           0xCCB4)
DSV(s16, joy_y_hi,           0xCCB6)
DSP(u8,  joy_data,           0xCCC0)
DSV(s16, inp_prev_option,    0xCCC6)
DSV(s16, joy_autotune,       0x0352)
DSV(s16, joy_delay,          0xCDE0)
DSV(s16, joy_x,              0xCDE2)
DSV(s16, joy_y,              0xCDE4)
DSV(u8,  joy_btn,            0xCDE6)
DSA(char, s_exit_to_dos,     0x0146, 19)

/* --- ядро объектов (модуль 0x0D4B) */
DSA(u16, attr_props,         0x0084, 10)     /* атрибут клетки → маска свойств */
DSA(Motion, motion_table,    0x1348, 11)
DSA(s16, sfx_prio_tab,       0x146A, 10)
DSPA(u8, sfx_scripts,        0x147E, 10)
DSA(s16, remove_list,        0xD780, 128)    /* индексы на удаление в конце THINKCYCLE */
DSP(ActiveObject, sky_chain, 0xD880)         /* лёд (тип 8): цепочка через parent */
DSP(ActiveObject, t51_obj,     0xD886)         /* тип 51 (ворота) */
DSV(s16, pending_state,      0xD88C)
DSV(s16, cur_state,          0xD8A2)
DSP(ActiveObject, cur_obj,   0xD8A6)
DSP(ActiveObject, goal_watch, 0xD94F)        /* по смерти goal-объекта: phase = 1 */
DSV(s16, remove_count,       0xD953)
DSP(ActiveObject, handoff_obj, 0xD95B)       /* ai_result & 8: кому передать управление */
DSV(s16, ai_result,          0xD963)

#include "orig/ds_anim.h"

/* --- Карнов (модуль 0x0E62) */
DSV(s16, shots_alive,        0x1424)
DSA(s32, level_time,         0x14A6, 10)     /* BCD: начальный таймер уровня */
DSV(s16, cam_dx,             0xD88A)
DSA(s16, cam_hist,           0xD88E, 8)      /* 4 последние позиции игрока (x, y) */
DSV(s16, ladder_col,         0xD89E)
DSV(s16, sky_x0,             0xD8A0)         /* x игрока при последнем сбросе sky-таймера */
DSV(s16, sky_timer,          0xD8A4)
DSV(s16, cam_dy,             0xD955)
DSV(s16, k_i,                0xD968)         /* счётчик циклов обработчиков игрока */
DSV(s16, k_kind,             0xD96A)
DSV(s16, scroll_i,           0xD96E)         /* свиток: обход врагов */
DSV(s16, scroll_k,           0xD970)

/* --- AI-модули 0x1348.. */
DSA(s16, cam_saved,          0xD972, 4)      /* cam_rect на время боя тип 30 */
DSV(s16, t25_count,          0xD97A)         /* живые тип 25 (держат тип 30) */
DSV(s16, gold_balls,         0xD97C)         /* шары золотого человека */
DSA(s16, t34_trail,          0xD97E, 40)     /* тип 34: след головы (x, y) ×20 */
DSA(s16, t56_pos,            0x1F8E, 6)      /* тип 56: точки появления (x, y) ×3 */

/* --- рекорды (модуль 0x02D3) */
DSA(u8,  herc_crtc,          0x0204, 12)     /* регистры 6845 для графики Hercules */
DSA(u8,  hiscores,           0x0210, 0x136)  /* 10 × (имя[27], u32 BCD-очки) */
DSA(char, s_high_scores,     0x0346, 12)
