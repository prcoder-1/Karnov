/* ds.c — реестр глобалов DS: определения, инициализация из образа
 * оригинала, сборка DS-образа для сверки, разрешение far-указателей. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "orig/ds.h"

/* --- определения */
#define DSV(t, n, a)        t n;
#define DSA(t, n, a, k)     t n[k];
#define DSS(t, n, a, k)     t n[k];
#define DSP(t, n, a)        t *n;
#define DSPA(t, n, a, k)    t *n[k];
#define DSF(n, a)           farfn n;
#define DSFA(n, a, k)       farfn n[k];
#define DSX(t, n, a, k)     t n[k];
#include "orig/ds_vars.h"
#undef DSV
#undef DSA
#undef DSS
#undef DSP
#undef DSPA
#undef DSF
#undef DSFA
#undef DSX

/* --- раскладки структур */
#define FD_R(o, t, n)    { o, sizeof(t), K_RAW, offsetof(LAYOUT_T, n) },
#define FD_A(o, t, n, k) { o, sizeof(t) * (k), K_RAW, offsetof(LAYOUT_T, n) },
#define FD_P(o, t, n)    { o, 4, K_FARDATA, offsetof(LAYOUT_T, n) },
#define FD_C(o, n)       { o, 4, K_FARCODE, offsetof(LAYOUT_T, n) },

#define LAYOUT_T ActiveObject
static const FieldDesc obj_fields[] = { OBJ_FIELDS(FD_R, FD_A, FD_P, FD_C) };
const Layout ActiveObject_layout = {
    obj_fields, sizeof obj_fields / sizeof obj_fields[0], ActiveObject_SIZE };
#undef LAYOUT_T

/* --- реестр */
#define DSV(t, n, a)     { #n, a, 1, sizeof(t), sizeof(t), K_RAW, NULL, &n },
#define DSA(t, n, a, k)  { #n, a, k, sizeof(t), sizeof(t), K_RAW, NULL, n },
#define DSS(t, n, a, k)  { #n, a, k, t##_SIZE, sizeof(t), K_RAW, &t##_layout, n },
#define DSP(t, n, a)     { #n, a, 1, 4, sizeof(void *), K_FARDATA, NULL, &n },
#define DSPA(t, n, a, k) { #n, a, k, 4, sizeof(void *), K_FARDATA, NULL, n },
#define DSF(n, a)        { #n, a, 1, 4, sizeof(farfn), K_FARCODE, NULL, &n },
#define DSFA(n, a, k)    { #n, a, k, 4, sizeof(farfn), K_FARCODE, NULL, n },
#define DSX(t, n, a, k)  { #n, a, k, sizeof(t), sizeof(t), K_NOCMP, NULL, n },
const DsVar ds_vars[] = {
#include "orig/ds_vars.h"
};
const unsigned ds_nvars = sizeof ds_vars / sizeof ds_vars[0];

static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void wr32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* Поле элемента: (отсчёт от начала элемента в оригинале) → вид/C-адрес. */
typedef struct { u8 kind; u16 size; u8 *c; } Slot;

static unsigned var_slots(const DsVar *v, unsigned idx, u8 *elem, Slot *out)
{
    unsigned n = 0, i;
    (void)idx;
    if (!v->lay) {
        out[n].kind = v->kind; out[n].size = v->orig_size; out[n].c = elem;
        return 1;
    }
    for (i = 0; i < v->lay->nf; i++) {
        const FieldDesc *f = &v->lay->f[i];
        out[n].kind = f->kind; out[n].size = f->size; out[n].c = elem + f->c_off;
        n++;
    }
    return n;
}

static u16 slot_off(const DsVar *v, unsigned i)
{
    return v->lay ? v->lay->f[i].off : 0;
}

void ds_init(void)
{
    static Slot sl[64];
    unsigned i, e, s, n;
    for (i = 0; i < ds_nvars; i++) {
        const DsVar *v = &ds_vars[i];
        if (v->addr >= ds_init_size)
            continue;
        for (e = 0; e < v->count; e++) {
            u8 *elem = (u8 *)v->ptr + e * v->c_size;
            u32 base = v->addr + (u32)e * v->orig_size;
            n = var_slots(v, e, elem, sl);
            for (s = 0; s < n; s++) {
                u32 a = base + slot_off(v, s);
                u8 tmp[512];
                unsigned k;
                if (a >= ds_init_size)
                    break;
                for (k = 0; k < sl[s].size && k < sizeof tmp; k++)
                    tmp[k] = a + k < ds_init_size ? ds_init_image[a + k] : 0;
                if (sl[s].kind == K_FARDATA) {
                    void *p = ds_resolve(rd32(tmp));
                    memcpy(sl[s].c, &p, sizeof p);
                } else if (sl[s].kind == K_FARCODE) {
                    farfn f = fn_resolve(rd32(tmp));
                    memcpy(sl[s].c, &f, sizeof f);
                } else {
                    memcpy(sl[s].c, tmp, sl[s].size);
                }
            }
        }
    }
}

void ds_snapshot(u8 *img, u8 *mask)
{
    static Slot sl[64];
    unsigned i, e, s, n;
    memset(img, 0, 0x10000);
    memset(mask, 0, 0x10000);
    for (i = 0; i < ds_nvars; i++) {
        const DsVar *v = &ds_vars[i];
        for (e = 0; e < v->count; e++) {
            u8 *elem = (u8 *)v->ptr + e * v->c_size;
            u32 base = v->addr + (u32)e * v->orig_size;
            n = var_slots(v, e, elem, sl);
            for (s = 0; s < n; s++) {
                u32 a = base + slot_off(v, s);
                u8 tmp[512];
                if (a + sl[s].size > 0x10000)
                    continue;
                if (sl[s].kind == K_FARDATA) {
                    void *p;
                    memcpy(&p, sl[s].c, sizeof p);
                    wr32(tmp, ds_unresolve(p));
                } else if (sl[s].kind == K_FARCODE) {
                    farfn f;
                    memcpy(&f, sl[s].c, sizeof f);
                    wr32(tmp, fn_unresolve(f));
                } else {
                    memcpy(tmp, sl[s].c, sl[s].size);
                }
                memcpy(img + a, tmp, sl[s].size);
                if (sl[s].kind != K_NOCMP)
                    memset(mask + a, 1, sl[s].size);
            }
        }
    }
}

/* DS-адрес → C-адрес. Внутри структуры попадаем в поле (или его середину). */
void *ds_resolve(u32 segoff)
{
    u16 seg = segoff >> 16, off = (u16)segoff;
    unsigned i, f;
    if (segoff == 0)
        return NULL;
    if (seg != DS_SEG)
        return NULL;
    for (i = 0; i < ds_nvars; i++) {
        const DsVar *v = &ds_vars[i];
        u32 end = v->addr + (u32)v->count * v->orig_size;
        unsigned e, in;
        if (off < v->addr || off >= end)
            continue;
        e = (off - v->addr) / v->orig_size;
        in = (off - v->addr) % v->orig_size;
        if (!v->lay)
            return (u8 *)v->ptr + e * v->c_size + in;
        for (f = 0; f < v->lay->nf; f++) {
            const FieldDesc *d = &v->lay->f[f];
            if (in >= d->off && in < d->off + d->size)
                return (u8 *)v->ptr + e * v->c_size + d->c_off + (in - d->off);
        }
    }
    /* Указатель на конец массива (типичный предел цикла) тоже законен. */
    for (i = 0; i < ds_nvars; i++) {
        const DsVar *v = &ds_vars[i];
        if (!v->lay && off == v->addr + (u32)v->count * v->orig_size)
            return (u8 *)v->ptr + v->count * v->c_size;
    }
    fprintf(stderr, "ds_resolve: %04X:%04X вне реестра\n", seg, off);
    return NULL;
}

u32 ds_unresolve(const void *p)
{
    const u8 *q = p;
    unsigned i, f;
    if (!p)
        return 0;
    for (i = 0; i < ds_nvars; i++) {
        const DsVar *v = &ds_vars[i];
        const u8 *b = v->ptr, *end = b + v->count * v->c_size;
        size_t e, in;
        if (q < b || q > end)
            continue;
        if (q == end && v->lay)
            continue;
        e = (size_t)(q - b) / v->c_size;
        in = (size_t)(q - b) % v->c_size;
        if (!v->lay)
            return (u32)DS_SEG << 16 | (u16)(v->addr + e * v->orig_size + in);
        for (f = 0; f < v->lay->nf; f++) {
            const FieldDesc *d = &v->lay->f[f];
            size_t csz = d->kind == K_RAW ? d->size
                       : d->kind == K_FARCODE ? sizeof(farfn) : sizeof(void *);
            if (in >= d->c_off && in < d->c_off + csz)
                return (u32)DS_SEG << 16 |
                       (u16)(v->addr + e * v->orig_size + d->off + (in - d->c_off));
        }
    }
    return 0xFFFFFFFFu;     /* не из DS: в сверке проявится как расхождение */
}

farfn fn_resolve(u32 segoff)
{
    unsigned i;
    if (segoff == 0)
        return NULL;
    for (i = 0; i < fn_nreg; i++)
        if (fn_reg[i].seg == segoff >> 16 && fn_reg[i].off == (u16)segoff)
            return fn_reg[i].fn;
    fprintf(stderr, "fn_resolve: %04X:%04X не переведена\n",
            (unsigned)(segoff >> 16), (unsigned)(u16)segoff);
    return NULL;
}

u32 fn_unresolve(farfn f)
{
    unsigned i;
    if (!f)
        return 0;
    for (i = 0; i < fn_nreg; i++)
        if (fn_reg[i].fn == f)
            return (u32)fn_reg[i].seg << 16 | fn_reg[i].off;
    return 0xFFFFFFFFu;
}
