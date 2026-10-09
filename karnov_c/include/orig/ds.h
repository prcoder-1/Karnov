/* orig/ds.h — данные перевода KARNOV.EXE: типы и глобалы.
 *
 * Глобалы — переменные сегмента данных оригинала (в комментариях — их
 * адреса в DS, для сверки с дизассемблером); far-указатели — обычные
 * указатели Си. Определения и начальные значения — в ds.c. */
#ifndef ORIG_DS_H
#define ORIG_DS_H

#include "karnov.h"

typedef void (*farfn)(void);

/* 32-битные величины, которые оригинал хранит парой слов. */
#define LONG(hi, lo)  ((s32)(((u32)(u16)(hi) << 16) | (u16)(lo)))
#define SETLONG(hi, lo, v) \
    do { s32 v_ = (v); (lo) = (u16)v_; (hi) = (s16)((u32)v_ >> 16); } while (0)

#define ABS16(v) ((v) > 0 ? (v) : -(v))

/* Элемент списка аним-состояний: массив кадров (слово 0 — число кадров,
 * далее номера спрайтов) и смещения кадров (пары dx, dy). */
typedef struct {
    s16 *anim;
    s16 *offs;
} StateEntry;

/* Скорость и ускорение (Q16.16) — таблица motion_table. */
typedef struct { s32 vx, vy, ax, ay; } Motion;

/* Результат move_with_collision: маски проходимости по направлениям
 * (0 вверх, 1 вправо, 2 вниз, 3 влево, 4 — проба «ступни») и до 3 упоров. */
typedef struct {
    s16 *masks;
    s16 any, n;
    s16 dir[3], x[3], y[3], col[3], attr[3];
} CollRes;

/* Бокс спрайта в объекте. */
typedef struct {
    s16 sprite, dx, dy, x0, y0, x1, y1, hitflag, was_vis;
} SpriteBox;

/* Объект (all_objects[]). Две parity-цепочки свободных объектов связаны
 * через next_free. stamped — объект впечатан в фон (список stamp_list) и
 * не перерисовывается покадрово. */
typedef struct ActiveObject {
    struct ActiveObject *next_free;
    s16 self_idx;
    u16 x_lo;
    s16 x_world;
    u16 y_lo;
    s16 y_world;
    u16 vx_lo;
    s16 vx_hi;
    u16 vy_lo;
    s16 vy_hi;
    u16 ax_lo;
    s16 ax_hi;
    u16 ay_lo;
    s16 ay_hi;
    s16 num_bboxes;
    SpriteBox box[8];
    s16 stamped;
    s16 active_pos;
    s16 stamp_pos;
    u8  state_byte;
    farfn handler;
    farfn ai_init;
    farfn ai_next;
    s16 anim_len;
    s16 frame_idx;
    s16 frame_ctr;
    s16 tick_period;
    s16 throttle;
    s16 *state_anim;
    s16 *state_offs;
    StateEntry *state_list;
    s16 next_state_id;
    s16 active;
    s16 hit_flag;
    s16 hit_idx;
    s16 hit_box_other;
    s16 hit_box_self;
    s16 obj_type;
    s16 hit_points;
    s16 misc;
    s16 phase_ctr;
    s16 phase;
    s16 aux;
    struct ActiveObject *parent;
} ActiveObject;

/* Число кадров аним-состояния; у объекта без анимации — 0. */
static inline s16 anim_frames(const ActiveObject *o)
{
    return o->state_anim != NULL ? o->state_anim[0] : 0;
}

/* Записи уровня (из файла, 20 и 12 байт). */
typedef struct {            /* отсортированы по x */
    s16 type;               /* AI-тип: индекс ai_handlers */
    s16 kind;               /* → obj_type; бит 0 — parity-цепочка */
    s16 x, y;               /* точка триггера */
    s16 rx, ry;             /* полуразмеры зоны срабатывания */
    s16 wx, wy;             /* стартовые x_world/y_world объекта */
    u8  init_arg, hp;
    s16 spawned;
} EnemySpawn;

typedef struct {            /* зона и точка возрождения в ней */
    s16 x0, y0, x1, y1;
    s16 spawn_x, spawn_y;
} ObjRegion;

typedef struct {            /* демо */
    s16 pos;                /* следующее событие (по кругу 256) */
    s16 count;              /* конец */
    u8  ev[0x300];          /* события по 3 байта: маска, кадр (слово) */
} Demo;

/* Состояние игрока — блок DS:D8AA..D94E; при смене игрока он целиком
 * копируется в player_rec[] и обратно. Счётчики — неупакованные
 * десятичные цифры, младшая первой. */
typedef struct {
    s16 current_level;                  /* D8AA */
    s16 lives;                          /* D8AC */
    s16 plr_D8AE;
    s16 player_dir;                     /* D8B0 */
    s16 fire_cooldown;                  /* D8B2 */
    s16 plr_D8B4;                       /* предмет 1: 250 */
    s16 plr_D8B6;                       /* предмет 6: 200 */
    s16 plr_D8B8;                       /* предмет 5: 150 */
    s16 plr_D8BA;                       /* предмет 8 */
    s16 player_x;                       /* D8BC: копия x_world игрока для AI */
    s16 player_y;                       /* D8BE */
    s16 jump_y0;                        /* D8C0: y в начале прыжка */
    s16 cell_col;                       /* D8C2: клетка игрока (пишет POST_KARNOV) */
    s16 cell_row_head;                  /* D8C4 */
    s16 cell_row_feet;                  /* D8C6 */
    s16 respawn_x;                      /* D8C8 */
    s16 respawn_y;                      /* D8CA */
    s16 respawn_D8CC;
    s16 player_state;                   /* D8CE */
    s16 karnov_mode;                    /* D8D0: фаза прыжка: 0..3; 4, 7 — гибель */
    CollRes coll;                       /* D8D2: упоры игрока (move_with_collision) */
    s16 items_owned;                    /* D8F8: видов предметов в наличии */
    s16 item_sel;                       /* D8FA: выбранный; -1 — нет */
    s16 plr_D8FC[11];
    s16 item_count[11];                 /* D912 */
    s16 item_active[4];                 /* D928: предметы 5..8 в действии */
    u8 score_bcd[8];                    /* D930 */
    s16 score_dirty;                    /* D938 */
    u8 time_digits[3];                  /* D93A: таймер уровня */
    s16 hurry;                          /* D93D */
    u8 k_digits[2];                     /* D93F: собранные «K»: 30 → жизнь */
    s16 k_dirty;                        /* D941 */
    u8 ammo_digits[2];                  /* D943 */
    s16 ammo_dirty;                     /* D945 */
    s16 weapon;                         /* D947 */
    s16 firepower;                      /* D949 */
    ActiveObject *item_chain;           /* D94B: предметы на лестницах (через parent) */
} PlayerRec;

/* --- оборудование, консоль (модули 0x0330, 0x0361) */
extern s16 machine_kind;                /* 0124: detect_machine (@01F3:0007) */
extern s16 level_mode;                  /* 0360: 0 CGA, 1 EGA, 2 Hercules, 3 Tandy */
extern u8 key_pending;                  /* 047A: скан-код расширенной клавиши */

/* --- рантайм Turbo-C */
extern u32 rand_seed;                   /* 24DA */

/* --- уровень и звук: записи и скрипты */
extern ObjRegion regions[5];            /* 047C (там far-указатель): region_count записей */
extern EnemySpawn player_spawn;         /* 0480: запись спауна игрока */
extern EnemySpawn spawns[200];          /* 0494 (там far-указатель): spawn_count записей, по x */
extern u8 music_data[0x649];            /* 04CE: скрипты музыки; music_* — смещения */
extern u8 sfx_data[0x119];              /* 0B17: скрипты эффектов */

/* --- объекты (модуль 0x006E) */
extern s16 objloop_chain;               /* 258C: индекс parity-цепочки во внешних циклах */
extern s16 objloop_k;                   /* 258E: позиция в active_index (шаг 2) */
extern s16 objloop_end;                 /* 2590 */
extern s16 active_index[128];           /* 2594: по parity-цепочкам: чётные — 0, нечётные — 1 */
extern s16 active_count[2];             /* 2694 */
extern s16 stamp_list[0x41];            /* 2698: индексы впечатанных в фон объектов */
extern s16 pair_dy;                     /* 271C: грубый отсев пар по |dy| */
extern s16 pair_count;                  /* 271E */
extern s16 pair_dx;                     /* 2720: грубый отсев пар по |dx| */
extern s16 pair_table[128];             /* 2722: пары пересечений (индексы) */
extern ActiveObject all_objects[128];   /* 2824 */
extern s16 sprite_phase_on;             /* A2A4: учитывать подпиксельную фазу x при блите */
extern ActiveObject *free_head[2];      /* A2A6 */
extern s16 stamp_count;                 /* A2AE */

/* --- таблицы спрайтов */
extern u8 sprite_w[0x152];              /* 0D08: ширина в единицах x_world */
extern u8 sprite_wb[0x152];             /* 0E5A: ширина в байтах CGA/Tandy */
extern u8 sprite_h[0x152];              /* 0FAC */
extern s16 clip_rect[4];                /* B834: x0, y0, x1, y1 — в колонках/строках буфера */
extern s16 view_rect[4];                /* CC8E */

/* --- RLE-ввод (модуль 0x058B) */
extern s16 io_buf_avail;                /* D566 */
extern s16 rle_in_run;                  /* D568 */
extern u8 io_buffer[0x200];             /* D56A */
extern u8 rle_run_byte;                 /* D76A */
extern s16 io_buf_pos;                  /* D76B */
extern u8 rle_run_left;                 /* D76D */

/* --- состояние машины (модуль 0x0330) */
extern s16 bios_equip_saved;            /* CDE8 */
extern s16 bios_kbflags_saved;          /* CDEE */
extern s16 saved_video_mode;            /* CDF4 */

/* --- звук (модуль 0x0CB9) */
extern u16 spk_div;                     /* 1206: последний делитель PIT ch2 */
extern u16 mus_pos;                     /* 1208 */
extern s16 music_enabled;               /* 120A */
extern u16 mus_div;                     /* 120C: 0 = пауза */
extern u8 mus_gap;                      /* 120E: длительность паузы после ноты */
extern u8 mus_phase;                    /* 120F: 2 — нота, 1 — пауза */
extern u16 mus_prio;                    /* 1210 */
extern u8 *mus_script;                  /* 1212 */
extern s16 mus_loop;                    /* 1216 */
extern u8 mus_timer;                    /* 1218 */
extern u16 sfx_pos;                     /* 1219 */
extern u16 sfx_div;                     /* 121B */
extern s16 sfx_enabled;                 /* 121D */
extern u16 sfx_prio;                    /* 121F */
extern u8 *sfx_script;                  /* 1221 */
extern u8 sfx_repeat;                   /* 1225 */
extern u8 sfx_repeat_set;               /* 1226 */
extern u8 sfx_timer;                    /* 1227 */
extern u16 sfx_delta;                   /* 1228: добавка к делителю на каждом тике */
extern u8 note_len[0x40];               /* 122A: пары (нота, пауза), пишет опкод FF */
extern u16 pitch_lut[0x6E];             /* 126A */
extern u8 bios_tick_div;                /* 1346: 12 тиков → старый INT 8 */
extern u8 tick_phase;                   /* D774: нечёт — музыка, чёт — SFX */
extern u32 timer_ticks;                 /* D775 */
extern farfn old_int8;                  /* D779 */

/* --- внеэкранный буфер (bk_init @015F:0646) */
extern s16 new_vis[4];                  /* A2B4: SCROLLVIEW: view_rect ∩ world_rect */
extern u8 tile_attr[200];               /* ABC0: атрибут тайла по номеру */
extern u16 video_offset;                /* AD50 */
extern s16 vid_AF1E;
extern s16 vid_AF20;
extern s16 old_vis[4];                  /* B81E: SCROLLVIEW: prev_view ∩ world_rect */
extern s16 vid_B826;
extern s16 tile_shift;                  /* B828 */
extern u8 map_buf[0x7992];              /* B82A (там far-указатель): карта: (тайл, атрибут) по клеткам */
extern s16 vid_B82E;
extern s16 vid_B832;
extern u8 vbuf[0x20000];                /* BA62 (там far-указатель): внеэкранный буфер: два банка по 64 КБ */
extern s16 vb_stride;                   /* BA66: байт в строке буфера */
extern s16 prev_view[4];                /* BA70 */
extern s16 vb_view_bytes;               /* CC9A */
extern s16 vb_row_bytes;                /* D76E */

/* --- уровень (модуль 0x0365) */
extern u8 tile_gfx[0x3E80];             /* 0098 (там far-указатель): графика тайлов: 40 (CGA, Hercules) или 80 байт */
extern s16 map_rows;                    /* B830 */
extern s16 map_cols;                    /* CC96 */
extern s16 map_row_bytes;               /* CC98: map_cols * 2: (тайл, атрибут) */
extern char msg_buf[0x40];              /* CCCE */
extern s16 level_loading;               /* CDF2 */
extern s16 trig_lo;                     /* CDF6: двоичный поиск в CHECKTRIGGERS */
extern s16 trig_hi;                     /* CDF8 */
extern s16 spawn_count;                 /* CDFA */
extern s16 loaded_level;                /* CDFC: 0x3E7 — не загружен */
extern s16 region_count;                /* CDFE */
extern s16 goal_idx;                    /* D959 */
extern ActiveObject *player_ptr;        /* D95F */

/* --- таблица AI-обработчиков */
extern farfn ai_handlers[58];           /* 10FE */
extern s16 demo_level_ctr;              /* 035E */
extern s16 demo_mode;                   /* CDEC */

/* --- спрайты (модуль 0x0BFB) */
extern u16 ega_page;                    /* 0D02: EGA: видимая страница (0 / 8000) */
extern u8 sprite_arena[0x20000];        /* 0D04 (там far-указатель): спрайты уровня (s0bfb_sprites.c) */
extern s16 sprites_fixed;               /* 11E6: последний закреплённый спрайт; -1 — ещё не грузили */
extern u8 *sprites_fixed_end;           /* D770 */

/* --- игра (модуль 0x04E4): опции, игроки */
extern s16 opt_music;                   /* 0354 */
extern s16 opt_sfx;                     /* 0356 */
extern s16 opt_joystick;                /* 0358 */
extern s16 two_players;                 /* 035A */
extern s16 attract_on;                  /* 035C */
extern s16 frame_no;                    /* CDEA: кадр от начала уровня */
extern s16 cur_player;                  /* CE00 */
extern s16 in_game;                     /* CE02 */
extern s16 players_left;                /* CF4E */
extern s16 num_players;                 /* CF50 */
extern s16 quit_game;                   /* CF52 */
extern Demo demo_buf;                   /* D258 */
extern Demo *demo_ptr;                  /* D55C */
extern s16 game_D564;
extern s16 world_rect[4];               /* ABB8 */
extern s16 cam_rect[4];                 /* BA68: x0 — левая колонка вида */

/* --- ввод (модуль 0x020F) */
extern s16 god_mode;                    /* CCA6: чит ZEEK */
extern s16 cheat_pos;                   /* CCCA */

/* --- предметы и HUD (модуль 0x041D) */
extern s16 (*item_use_fn[9])(void);     /* 0C30 */
extern u16 tandy_row_off[4];            /* 0C54 */
extern s16 inp_scan;                    /* CC9C: скан-код последней клавиши */
extern u8 inp_mask;                     /* CC9E: 1 R, 2 L, 4 D, 8 U, 0x10 огонь, 0x20 опция */
extern s16 inp_key;                     /* CC9F: ASCII последней клавиши */
extern u8 inp_prev_mask;                /* CCA1 */
extern s16 inp_option;                  /* CCA2 */
extern s16 inp_prev_fire;               /* CCA4 */
extern s16 kb_mask;                     /* CCA8 */
extern s16 inp_fire;                    /* CCAA */
extern s16 inp_dir;                     /* CCAC */
extern s16 inp_dir_changed;             /* CCAE */
extern s16 joy_x_lo;                    /* CCB0 */
extern s16 joy_x_hi;                    /* CCB2 */
extern s16 joy_y_lo;                    /* CCB4 */
extern s16 joy_y_hi;                    /* CCB6 */
extern s16 inp_prev_option;             /* CCC6 */
extern s16 joy_x;                       /* CDE2 */
extern s16 joy_y;                       /* CDE4 */
extern u8 joy_btn;                      /* CDE6 */

/* --- ядро объектов (модуль 0x0D4B) */
extern u16 attr_props[10];              /* 0084: атрибут клетки → маска свойств */
extern Motion motion_table[11];         /* 1348 */
extern s16 sfx_prio_tab[10];            /* 146A */
extern u8 *sfx_scripts[10];             /* 147E */
extern s16 remove_list[128];            /* D780: индексы на удаление в конце THINKCYCLE */
extern ActiveObject *sky_chain;         /* D880: лёд (тип 8): цепочка через parent */
extern ActiveObject *t51_obj;           /* D886: тип 51 (ворота) */
extern s16 pending_state;               /* D88C */
extern s16 cur_state;                   /* D8A2 */
extern ActiveObject *cur_obj;           /* D8A6 */
extern ActiveObject *goal_watch;        /* D94F: по смерти goal-объекта: phase = 1 */
extern s16 remove_count;                /* D953 */
extern ActiveObject *handoff_obj;       /* D95B: ai_result & 8: кому передать управление */
extern s16 ai_result;                   /* D963 */

/* --- анимации: аним-состояния (кадры, смещения) и массивы кадров */
extern s16 an_1426[5];
extern StateEntry sl_1440[1];
extern StateEntry sl_1462[1];
extern StateEntry sl_14E0[1];
extern StateEntry sl_1500[2];
extern s16 an_1510[5];
extern s16 an_151A[5];
extern StateEntry sl_153E[1];
extern s16 an_1546[5];
extern StateEntry sl_1560[4];
extern s16 an_1580[5];
extern StateEntry sl_15A6[2];
extern s16 an_15B6[10];
extern StateEntry sl_160A[8];
extern s16 an_164A[10];
extern StateEntry sl_1682[3];
extern s16 an_169A[5];
extern StateEntry sl_16B2[2];
extern s16 an_16C2[5];
extern s16 an_16CC[10];
extern StateEntry sl_16EA[1];
extern StateEntry sl_1706[1];
extern StateEntry sl_171C[2];
extern StateEntry sl_1784[12];
extern StateEntry sl_180A[1];
extern StateEntry sl_1856[4];
extern StateEntry sl_1880[1];
extern StateEntry sl_18CC[4];
extern StateEntry sl_18F4[1];
extern StateEntry sl_1918[6];
extern StateEntry sl_1958[4];
extern StateEntry sl_1984[2];
extern StateEntry sl_19B4[8];
extern s16 an_19F4[14];
extern StateEntry sl_1A16[1];
extern StateEntry sl_1A2C[2];
extern StateEntry sl_1A58[2];
extern s16 an_1A68[2];
extern StateEntry sl_1A78[2];
extern StateEntry sl_1AA4[4];
extern StateEntry sl_1AF0[4];
extern StateEntry sl_1B46[6];
extern StateEntry sl_1B92[2];
extern StateEntry sl_1BC4[2];
extern StateEntry sl_1BE0[2];
extern s16 an_1BF0[6];
extern s16 an_1BFE[7];
extern s16 an_1C0C[4];
extern s16 an_1C14[6];
extern s16 an_1C20[6];
extern StateEntry sl_1C40[5];
extern s16 an_1C68[8];
extern StateEntry sl_1C86[2];
extern StateEntry sl_1CA4[2];
extern StateEntry sl_1CBE[1];
extern s16 an_1CCE[9];
extern StateEntry sl_1CF2[4];
extern StateEntry sl_1D22[2];
extern s16 an_1D32[12];
extern StateEntry sl_1D56[2];
extern StateEntry sl_1D7E[4];
extern StateEntry sl_1DA4[1];
extern StateEntry sl_1DC0[2];
extern StateEntry sl_1DE4[4];
extern StateEntry sl_1E18[2];
extern StateEntry sl_1E70[3];
extern StateEntry sl_1E96[2];
extern StateEntry sl_1EB0[3];
extern StateEntry sl_1EF4[3];
extern s16 an_1F0C[12];
extern s16 an_1F24[8];
extern s16 an_1F34[10];
extern s16 an_1F48[5];
extern s16 an_1F62[10];
extern StateEntry sl_1F76[3];

/* --- Карнов (модуль 0x0E62) */
extern s16 shots_alive;                 /* 1424 */
extern s32 level_time[10];              /* 14A6: BCD: начальный таймер уровня */
extern s16 cam_dx;                      /* D88A */
extern s16 cam_hist[8];                 /* D88E: 4 последние позиции игрока (x, y) */
extern s16 ladder_col;                  /* D89E */
extern s16 sky_x0;                      /* D8A0: x игрока при последнем сбросе sky-таймера */
extern s16 sky_timer;                   /* D8A4 */
extern s16 cam_dy;                      /* D955 */
extern s16 k_i;                         /* D968: счётчик циклов обработчиков игрока */
extern s16 k_kind;                      /* D96A */
extern s16 scroll_i;                    /* D96E: свиток: обход врагов */
extern s16 scroll_k;                    /* D970 */

/* --- AI-модули 0x1348.. */
extern s16 cam_saved[4];                /* D972: cam_rect на время боя тип 30 */
extern s16 t25_count;                   /* D97A: живые тип 25 (держат тип 30) */
extern s16 gold_balls;                  /* D97C: шары золотого человека */
extern s16 t34_trail[40];               /* D97E: тип 34: след головы (x, y) ×20 */
extern s16 t56_pos[6];                  /* 1F8E: тип 56: точки появления (x, y) ×3 */

/* --- рекорды (модуль 0x02D3) */
extern u8 herc_crtc[12];                /* 0204: регистры 6845 для графики Hercules */
extern u8 hiscores[0x136];              /* 0210: 10 × (имя[27], u32 BCD-очки) */

/* --- игрок */
extern PlayerRec plr;                   /* D8AA: текущий игрок */
extern PlayerRec player_rec[2];         /* CE04: сохранённые записи игроков */

/* Смещение строки пикселей r во внеэкранном буфере. */
#define VB_ROW(r) ((u16)((r) * vb_row_bytes))

/* Пиксели → колонка буфера и строка тайлов (вне мира — вне карты). */
#define PX_COL(i)  ((s16)((u16)(i) / (8 >> tile_shift)))
#define PX_TROW(i) ((s16)((u16)(i) / 10))

/* Клетка карты (тайл, атрибут): строки лежат подряд, так что выход за
 * колонку попадает в соседнюю строку, как в оригинале. Вне карты —
 * пустой тайл 0, запись туда ничего не делает. Его атрибут — пусто (2),
 * а ниже предела камеры (карта + 3 строки) — пол (0): в оригинале там
 * обычно нулевая память, и упавшие объекты не летят бесконечно. */
static inline u8 *map_cell(s16 r, u16 o)
{
    s32 i = (s32)r * map_row_bytes + (s16)o;
    if (r < 0 || r >= map_rows || i < 0 || i >= (s32)map_rows * map_row_bytes)
        return NULL;
    return &map_buf[i];
}
#define TILE_ATTR(i) ((u16)(i) < 200 ? tile_attr[(u16)(i)] : 0)
#define ATTR_PROP(i) ((u16)(i) < 10 ? attr_props[(u16)(i)] : 0)
static inline u8 map_rd(s16 r, u16 o)
{
    u8 *c = map_cell(r, o);
    if (c != NULL)
        return *c;
    return (o & 1) && r < map_rows + 3 ? (u8)TILE_ATTR(0) : 0;
}
static inline void map_wr(s16 r, u16 o, u8 v)
{
    u8 *c = map_cell(r, o);
    if (c != NULL)
        *c = v;
}

/* Скрипты музыки по адресам DS. */
#define MUSIC(addr) (&music_data[(addr) - 0x04CE])
#define music_loader MUSIC(0x0A6A)

/* Скрипт-«стоп» для play_music/play_sfx (в оригинале far-указатель 0000:FFFF). */
extern u8 snd_stop_marker;
extern u16 mus_cycles;
#define SND_STOP (&snd_stop_marker)

#endif
