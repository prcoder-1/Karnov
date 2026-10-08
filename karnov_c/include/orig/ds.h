/* orig/ds.h — модель данных перевода KARNOV.EXE.
 *
 * Глобалы — обычные типизированные переменные. Каждая перечислена в
 * orig/ds_vars.h со своим адресом в DS оригинала; по этому реестру
 * переменные инициализируются из образа DS (ds_init.c), а DS-образ
 * перевода собирается для покадровой сверки с DSDUMP.EXE.
 *
 * Far-указатели оригинала (seg:off) хранятся как C-указатели:
 * на данные — внутрь зарегистрированных переменных, на код — farfn.
 */
#ifndef ORIG_DS_H
#define ORIG_DS_H

#include "karnov.h"

typedef void (*farfn)(void);

/* 32-битные величины, которые оригинал хранит парой слов. */
#define LONG(hi, lo)  ((s32)(((u32)(u16)(hi) << 16) | (u16)(lo)))
#define SETLONG(hi, lo, v) \
    do { s32 v_ = (v); (lo) = (u16)v_; (hi) = (s16)((u32)v_ >> 16); } while (0)

/* Виды полей/переменных для сериализации. */
enum { K_RAW, K_FARDATA, K_FARCODE, K_NOCMP };

typedef struct {
    u16 off;        /* смещение в записи оригинала */
    u16 size;       /* размер в оригинале */
    u8  kind;
    size_t c_off;   /* offsetof в C-структуре */
} FieldDesc;

typedef struct {
    const FieldDesc *f;
    unsigned nf;
    u16 orig_size;
} Layout;

typedef struct {
    const char *name;
    u16 addr;
    u16 count;          /* элементов */
    u16 orig_size;      /* размер элемента в оригинале */
    size_t c_size;      /* sizeof элемента в C */
    u8  kind;           /* для скаляров/массивов без раскладки */
    const Layout *lay;  /* для структур */
    void *ptr;
} DsVar;

/* --- структуры с раскладкой --------------------------------------- */

/* Поля описываются X-макросами:
 *   R(off, тип, имя)        — сырое поле
 *   A(off, тип, имя, n)     — сырой массив
 *   P(off, тип, имя)        — far-указатель на данные (тип *имя)
 *   C(off, имя)             — far-указатель на код (farfn имя)      */
#define ST_R(o, t, n)       t n;
#define ST_A(o, t, n, k)    t n[k];
#define ST_P(o, t, n)       t *n;
#define ST_C(o, n)          farfn n;

/* Элемент списка аним-состояний: far-указатели на массив кадров
 * (слово 0 — число кадров, далее номера спрайтов) и на смещения
 * кадров (пары dx, dy). */
#define STATEENTRY_FIELDS(R, A, P, C) \
    P(0x00, s16, anim)          \
    P(0x04, s16, offs)
typedef struct StateEntry {
    STATEENTRY_FIELDS(ST_R, ST_A, ST_P, ST_C)
} StateEntry;
#define StateEntry_SIZE 8
extern const Layout StateEntry_layout;

/* Скорость и ускорение (Q16.16) — таблица motion_table, DS:0x1348. */
typedef struct { s32 vx, vy, ax, ay; } Motion;
#define Motion_SIZE 16

/* Результат move_with_collision: маски проходимости по направлениям
 * (0 вверх, 1 вправо, 2 вниз, 3 влево, 4 — проба «ступни») и до 3 упоров. */
#define COLLRES_FIELDS(R, A, P, C) \
    P(0x00, s16, masks)         \
    R(0x04, s16, any)           \
    R(0x06, s16, n)             \
    A(0x08, s16, dir, 3)        \
    A(0x0E, s16, x, 3)          \
    A(0x14, s16, y, 3)          \
    A(0x1A, s16, col, 3)        \
    A(0x20, s16, attr, 3)
typedef struct CollRes {
    COLLRES_FIELDS(ST_R, ST_A, ST_P, ST_C)
} CollRes;
#define CollRes_SIZE 0x26
extern const Layout CollRes_layout;

/* Бокс спрайта в объекте (18 байт, +0x20 + 18*i). */
typedef struct {
    s16 sprite, dx, dy, x0, y0, x1, y1, hitflag, was_vis;
} SpriteBox;

/* ActiveObject — 245 байт, all_objects[] в DS:0x2824.
 * Свободный объект хранит в x_screen/y_screen far-указатель (off, seg)
 * на следующий свободный той же parity-цепочки. stamped — объект
 * впечатан в фон (список stamp_list) и не перерисовывается покадрово. */
#define OBJ_FIELDS(R, A, P, C) \
    R(0x00, s16, x_screen)      \
    R(0x02, s16, y_screen)      \
    R(0x04, s16, self_idx)           \
    R(0x06, u16, x_lo)          \
    R(0x08, s16, x_world)       \
    R(0x0A, u16, y_lo)          \
    R(0x0C, s16, y_world)       \
    R(0x0E, u16, vx_lo)         \
    R(0x10, s16, vx_hi)         \
    R(0x12, u16, vy_lo)         \
    R(0x14, s16, vy_hi)         \
    R(0x16, u16, ax_lo)         \
    R(0x18, s16, ax_hi)         \
    R(0x1A, u16, ay_lo)         \
    R(0x1C, s16, ay_hi)         \
    R(0x1E, s16, num_bboxes)    \
    A(0x20, SpriteBox, box, 8)  \
    R(0xB0, s16, stamped)         \
    R(0xB2, s16, active_pos)      \
    R(0xB4, s16, stamp_pos)      \
    R(0xB6, u8,  state_byte)    \
    C(0xB7, handler)            \
    C(0xBB, ai_init)            \
    C(0xBF, ai_next)            \
    R(0xC3, s16, anim_len)      \
    R(0xC5, s16, frame_idx)     \
    R(0xC7, s16, frame_ctr)     \
    R(0xC9, s16, tick_period)   \
    R(0xCB, s16, throttle)      \
    P(0xCD, s16, state_anim)    \
    P(0xD1, s16, state_offs)    \
    P(0xD5, StateEntry, state_list) \
    R(0xD9, s16, next_state_id) \
    R(0xDB, s16, active)        \
    R(0xDD, s16, hit_flag)      \
    R(0xDF, s16, hit_idx)       \
    R(0xE1, s16, hit_box_other)  \
    R(0xE3, s16, hit_box_self) \
    R(0xE5, s16, obj_type)      \
    R(0xE7, s16, hit_points)    \
    R(0xE9, s16, misc)          \
    R(0xEB, s16, phase_ctr)     \
    R(0xED, s16, phase)         \
    R(0xEF, s16, aux)           \
    P(0xF1, struct ActiveObject, parent)

typedef struct ActiveObject {
    OBJ_FIELDS(ST_R, ST_A, ST_P, ST_C)
} ActiveObject;
#define ActiveObject_SIZE 0xF5
extern const Layout ActiveObject_layout;

/* Записи уровня в куче (не в DS). */
#define EnemySpawn_SIZE 20
typedef struct {            /* 20 байт, отсортированы по x */
    s16 type;               /* AI-тип: индекс ai_handlers */
    s16 kind;               /* → obj_type; бит 0 — parity-цепочка */
    s16 x, y;               /* точка триггера */
    s16 rx, ry;             /* полуразмеры зоны срабатывания */
    s16 wx, wy;             /* стартовые x_world/y_world объекта */
    u8  init_arg, hp;
    s16 spawned;
} EnemySpawn;

#define Demo_SIZE 0x304
typedef struct {            /* демо: 0x304 байта в DS */
    s16 pos;                /* следующее событие (по кругу 256) */
    s16 count;              /* конец */
    u8  ev[0x300];          /* события по 3 байта: маска, кадр (слово) */
} Demo;

typedef struct {            /* 12 байт: зона и точка возрождения в ней */
    s16 x0, y0, x1, y1;
    s16 spawn_x, spawn_y;
} ObjRegion;

/* --- глобалы ------------------------------------------------------ */

#define DSV(t, n, a)        extern t n;
#define DSA(t, n, a, k)     extern t n[k];
#define DSS(t, n, a, k)     extern t n[k];
#define DSP(t, n, a)        extern t *n;
#define DSPA(t, n, a, k)    extern t *n[k];
#define DSF(n, a)           extern farfn n;
#define DSFA(n, a, k)       extern farfn n[k];
#define DSX(t, n, a, k, o)  extern t n[k];
#define DSVX(t, n, a, o)    extern t n;
#include "orig/ds_vars.h"
#undef DSV
#undef DSA
#undef DSS
#undef DSP
#undef DSPA
#undef DSF
#undef DSFA
#undef DSX
#undef DSVX

/* --- реестр ------------------------------------------------------- */

extern const DsVar ds_vars[];
extern const unsigned ds_nvars;
extern const u8 ds_init_image[];
extern const unsigned ds_init_size;
extern const u16 ds_init_far[];
extern const unsigned ds_init_nfar;

/* Инициализация всех переменных из образа DS оригинала. */
void ds_init(void);
/* Образ DS перевода; mask[i] = 1 там, где байт описан реестром. */
void ds_load(const u8 *img, u16 load);
void ds_snapshot(u8 *img, u8 *mask);

/* fmemcpy между диапазоном DS и буфером в раскладке оригинала. */
void ds_copy_out(u16 addr, u16 n, u8 *dst);
void ds_copy_in(u16 addr, u16 n, const u8 *src);
u16 ds_peek16(u16 addr);
void ds_poke16(u16 addr, u16 v);

/* Индекс массива s16 в DS: вне массива оригинал читает соседние
 * переменные — то же через ds_peek16. */
#define DS_IDX16(arr, addr, i) \
    ((u16)(i) < sizeof(arr) / sizeof((arr)[0]) ? (arr)[(u16)(i)] \
                                              : (s16)ds_peek16((u16)((addr) + (i) * 2)))
#define PX_COL(i)  DS_IDX16(px_to_col, 0xBB5E, (i))
#define PX_TROW(i) DS_IDX16(px_to_trow, 0xAF22, (i))
#define DS_SET16(arr, addr, i, v) \
    ((u16)(i) < sizeof(arr) / sizeof((arr)[0]) ? (void)((arr)[(u16)(i)] = (v)) \
                                              : ds_poke16((u16)((addr) + (i) * 2), (u16)(v)))
/* Байт по far-адресу: обычная память (hw_conv) или DS (через реестр). */
u8   far_peekb(u16 seg, u16 off);
void far_pokeb(u16 seg, u16 off, u8 v);
/* Клетка карты: map_row[r] (seg:off) + смещение в 16 битах, как les/add */
#define MAP_OFF(r) ((u16)DS_IDX16(map_row, 0xAD52, (r) * 2))
#define MAP_SEG(r) ((u16)DS_IDX16(map_row, 0xAD52, (r) * 2 + 1))
#define MAP_RD(r, o) far_peekb(MAP_SEG(r), (u16)(MAP_OFF(r) + (u16)(o)))
#define MAP_WR(r, o, v) far_pokeb(MAP_SEG(r), (u16)(MAP_OFF(r) + (u16)(o)), (u8)(v))
#define ATTR_PROP(i) ((u16)DS_IDX16(attr_props, 0x0084, (i)))
#define TILE_ATTR(i) ((u16)DS_IDX16(tile_attr, 0xABC0, (i)))

/* far-указатели: seg:off (seg в старшем слове) ↔ C-указатель. */
void *ds_resolve(u32 segoff);
u32   ds_unresolve(const void *p);
farfn fn_resolve(u32 segoff);
u32   fn_unresolve(farfn f);

#define DS_SEG 0x1D99u

/* Скрипты музыки по адресам DS. */
#define MUSIC(addr) (&music_data[(addr) - 0x04CE])
#define music_loader MUSIC(0x0A6A)

/* Скрипт-«стоп» для play_music/play_sfx (в оригинале far-указатель 0000:FFFF). */
extern u8 snd_stop_marker;
#define SND_STOP (&snd_stop_marker)

/* Реестр функций (генерируется tools/gen_fn.py в fnreg.c). */
typedef struct { u16 seg, off; farfn fn; const char *name; } FnReg;
extern const FnReg fn_reg[];
extern const unsigned fn_nreg;

#endif
