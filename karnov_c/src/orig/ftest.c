/* ftest.c — дифференциальная проверка (порт): снимок памяти оригинала
 * (DS из дампа DOSBox + far-куча) → переменные перевода, вызов функций по
 * имени (void(void)), после каждой — снимок DS и кучи в файл.
 * Сторона оригинала — tools/emu (unicorn). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "orig/fn.h"
#include "orig/hw.h"
#include "orig/port.h"

#define LOAD 0x1A3
#define HEAP_LIN 0x2DE30
#define HEAP_N 0x50000

static u16 rw(const u8 *d, u16 a) { return (u16)(d[a] | d[a + 1] << 8); }

static u8 *heap_ptr(const u8 *d, u16 a)
{
    u16 off = rw(d, a), seg = rw(d, (u16)(a + 2));
    return seg == 0 && off == 0 ? NULL : &hw_conv[((u32)seg * 16 + off) & 0xFFFFF];
}

/* ftest_run: ds_file/heap_file — образы одного кадра; out — DS (64К)
 * + маска (64К, первым) + куча на каждую функцию */
int ftest_run(const char *ds_file, const char *heap_file, const char *out, int nfn, char **fns)
{
    static u8 img[0x10000], snap[0x10000], mask[0x10000];
    FILE *f;
    int i, k;
    unsigned j;
    if ((f = fopen(ds_file, "rb")) == NULL || fread(img, 1, sizeof img, f) != sizeof img)
        return 2;
    fclose(f);
    if ((f = fopen(heap_file, "rb")) == NULL || fread(&hw_conv[HEAP_LIN], 1, HEAP_N, f) != HEAP_N)
        return 2;
    fclose(f);
    /* сырые слова с сегментом DS: свободные объекты, узел кучи RTL */
    for (k = 0; k < 128; k++) {
        u16 a = (u16)(0x2824 + k * 0xF5 + 2);
        if (rw(img, a) == 0x1D99 + LOAD) {
            img[a] = 0x99;
            img[a + 1] = 0x1D;
        }
    }
    for (j = 0; j < 2; j++) {
        u16 a = j == 0 ? 0x2472 : 0x2476;
        if (rw(img, a) == 0x1D99 + LOAD) {
            img[a] = 0x99;
            img[a + 1] = 0x1D;
        }
    }
    ds_load(img, LOAD);
    vbuf = heap_ptr(img, 0xBA62);
    vbuf_alloc = heap_ptr(img, 0xA2B0);
    map_buf = heap_ptr(img, 0xB82A);
    tile_gfx = heap_ptr(img, 0x0098);
    sprite_arena = heap_ptr(img, 0x0D04);
    regions = (ObjRegion *)heap_ptr(img, 0x047C);
    spawns = (EnemySpawn *)heap_ptr(img, 0x0494);
    sprites_fixed_end = heap_ptr(img, 0xD770);
    if ((f = fopen(out, "wb")) == NULL)
        return 2;
    ds_snapshot(snap, mask);
    fwrite(mask, 1, sizeof mask, f);
    for (i = 0; i < nfn; i++) {
        farfn fn = NULL;
        for (j = 0; j < fn_nreg; j++)
            if (!strcmp(fn_reg[j].name, fns[i]))
                fn = fn_reg[j].fn;
        if (fn == NULL) {
            fprintf(stderr, "ftest: нет функции %s\n", fns[i]);
            return 2;
        }
        fn();
        ds_snapshot(snap, mask);
        fwrite(snap, 1, sizeof snap, f);
        fwrite(&hw_conv[HEAP_LIN], 1, HEAP_N, f);
        fwrite(hw_conv, 1, HW_LOAD_LIN, f);
    }
    fclose(f);
    return 0;
}
