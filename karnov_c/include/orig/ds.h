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

/* Бокс спрайта в объекте (18 байт, +0x20 + 18*i). */
typedef struct {
    s16 sprite, dx, dy, x0, y0, x1, y1, hitflag, was_vis;
} SpriteBox;

/* ActiveObject — 245 байт, all_objects[] в DS:0x2824. */
#define OBJ_FIELDS(R, A, P, C) \
    R(0x00, s16, x_screen)      \
    R(0x02, s16, y_screen)      \
    R(0x04, s16, f04)           \
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
    R(0xB0, s16, alive)         \
    R(0xB2, s16, next_idx)      \
    R(0xB4, s16, prev_idx)      \
    R(0xB6, u8,  state_byte)    \
    C(0xB7, handler)            \
    C(0xBB, ai_init)            \
    C(0xBF, ai_next)            \
    R(0xC3, s16, anim_len)      \
    R(0xC5, s16, frame_idx)     \
    R(0xC7, s16, frame_ctr)     \
    R(0xC9, s16, tick_period)   \
    R(0xCB, s16, throttle)      \
    P(0xCD, u8,  state_anim)    \
    P(0xD1, u8,  state_offs)    \
    P(0xD5, u8,  state_list)    \
    R(0xD9, s16, next_state_id) \
    R(0xDB, s16, active)        \
    R(0xDD, s16, hit_flag)      \
    R(0xDF, s16, hit_idx)       \
    R(0xE1, s16, hit_box_self)  \
    R(0xE3, s16, hit_box_other) \
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

/* --- глобалы ------------------------------------------------------ */

#define DSV(t, n, a)        extern t n;
#define DSA(t, n, a, k)     extern t n[k];
#define DSS(t, n, a, k)     extern t n[k];
#define DSP(t, n, a)        extern t *n;
#define DSPA(t, n, a, k)    extern t *n[k];
#define DSF(n, a)           extern farfn n;
#define DSFA(n, a, k)       extern farfn n[k];
#define DSX(t, n, a, k)     extern t n[k];
#include "orig/ds_vars.h"
#undef DSV
#undef DSA
#undef DSS
#undef DSP
#undef DSPA
#undef DSF
#undef DSFA
#undef DSX

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
void ds_snapshot(u8 *img, u8 *mask);

/* far-указатели: seg:off (seg в старшем слове) ↔ C-указатель. */
void *ds_resolve(u32 segoff);
u32   ds_unresolve(const void *p);
farfn fn_resolve(u32 segoff);
u32   fn_unresolve(farfn f);

#define DS_SEG 0x1D99u

/* Реестр функций (генерируется tools/gen_fn.py в fnreg.c). */
typedef struct { u16 seg, off; farfn fn; const char *name; } FnReg;
extern const FnReg fn_reg[];
extern const unsigned fn_nreg;

#endif
