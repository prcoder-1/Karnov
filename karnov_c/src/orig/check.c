/* check.c — самопроверка реестра DS при старте (только порт). */
#include <stdio.h>
#include <stdlib.h>
#include "orig/ds.h"

static int fail(const char *what, const char *name)
{
    fprintf(stderr, "ds_check: %s: %s\n", what, name);
    return 1;
}

/* Раскладки сплошные и совпадают с размером; переменные не пересекаются;
 * все far-указатели из образа DS попадают на поля вида K_FAR*. */
int ds_check(void)
{
    unsigned i, f, k;
    int bad = 0;
    u32 prev_end = 0;
    for (i = 0; i < ds_nvars; i++) {
        const DsVar *v = &ds_vars[i];
        u32 end = v->addr + (u32)v->count * v->orig_size;
        if (v->addr < prev_end)
            bad |= fail("пересечение/порядок", v->name);
        if (end > 0x10000)
            bad |= fail("выход за DS", v->name);
        prev_end = end;
        if (v->lay) {
            u16 at = 0;
            for (f = 0; f < v->lay->nf; f++) {
                if (v->lay->f[f].off != at)
                    bad |= fail("дыра в раскладке", v->name);
                at = v->lay->f[f].off + v->lay->f[f].size;
            }
            if (at != v->lay->orig_size)
                bad |= fail("размер раскладки", v->name);
        }
    }
    for (k = 0; k < ds_init_nfar; k++) {
        u16 a = ds_init_far[k];
        for (i = 0; i < ds_nvars; i++) {
            const DsVar *v = &ds_vars[i];
            u32 end = v->addr + (u32)v->count * v->orig_size;
            unsigned in, kind = v->kind;
            if (a < v->addr || a >= end)
                continue;
            in = (a - v->addr) % v->orig_size;
            if (v->lay)
                for (f = 0; f < v->lay->nf; f++)
                    if (in >= v->lay->f[f].off &&
                        in < v->lay->f[f].off + v->lay->f[f].size) {
                        kind = v->lay->f[f].kind;
                        in -= v->lay->f[f].off;
                    }
            if ((kind != K_FARDATA && kind != K_FARCODE) || in != 0) {
                fprintf(stderr, "ds_check: far-указатель DS:%04X лежит в "
                        "не-far поле %s\n", a, v->name);
                bad = 1;
            }
        }
    }
    return bad;
}
