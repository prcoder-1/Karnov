/* Рантайм Turbo-C, который нельзя заменить libc напрямую. */
#include <math.h>
#include <stdio.h>
#include "fsopen.h"
#include "orig/fn.h"
#include "orig/hw.h"
#include "orig/ds.h"

/* @1BF7:0203 file:1D973 fopen RTL — имена DOS без учёта регистра */
FILE *rtl_fopen(const char *name, const char *mode)
{
    return karnov_fopen(name, mode);
}

/* @1C92:000E file:1E12E fmemcpy RTL — rep movsw вперёд (при перекрытии
 * dst > src «размазывает», как в оригинале) */
void rtl_fmemcpy(void *dst, const void *src, u16 n)
{
    u8 *d = dst;
    const u8 *s = src;
    u16 i;
    u8 lo, hi;
    for (i = 0; i < n / 2; i++, d += 2, s += 2) {
        lo = s[0];
        hi = s[1];
        d[0] = lo;
        d[1] = hi;
    }
    if (n & 1)
        *d = *s;
}

/* @1B89:0005 file:1D095 exit RTL */
void rtl_exit(s16 code)
{
    hw_exit(code);
}

/* --- far-куча Turbo-C. Указатель — seg:off; память — hw_conv, а DS
 * (фиктивный узел списка) — через реестр. Узел: u32 размер, far next. */
typedef struct { u16 off, seg; } FarP;

static u32 fp_lin(FarP p) { return (u32)p.seg * 16 + p.off; }

static FarP fp_mk(u32 lin)
{
    FarP p;
    p.seg = (u16)(lin >> 4);
    p.off = (u16)(lin & 0xF);
    return p;
}

static int fp_eq(FarP a, FarP b) { return a.seg == b.seg && a.off == b.off; }

/* @0000:04F2 file:01CF2 far_add RTL — p + d, нормализованный */
static FarP far_add(FarP p, s32 d)
{
    return fp_mk((fp_lin(p) + (u32)d) & 0xFFFFF);
}

/* @0000:054D file:01D4D far_cmp RTL — сравнение нормализованных адресов */
static int far_cmp(FarP a, FarP b)
{
    u32 x = fp_lin(a) & 0xFFFFF, y = fp_lin(b) & 0xFFFFF;
    return x < y ? -1 : x > y;
}

static u16 mem_rd16(FarP p, u16 d)
{
    u32 l = fp_lin(p) + d;
    if (l >= DS_LIN && l < DS_LIN + 0x10000)
        return ds_peek16((u16)(l - DS_LIN));
    return (u16)(hw_conv[l] | hw_conv[l + 1] << 8);
}

static void mem_wr16(FarP p, u16 d, u16 v)
{
    u32 l = fp_lin(p) + d;
    if (l >= DS_LIN && l < DS_LIN + 0x10000) {
        ds_poke16((u16)(l - DS_LIN), v);
        return;
    }
    hw_conv[l] = (u8)v;
    hw_conv[l + 1] = (u8)(v >> 8);
}

static u32 node_size(FarP p) { return mem_rd16(p, 0) | (u32)mem_rd16(p, 2) << 16; }

static void node_set_size(FarP p, u32 v)
{
    mem_wr16(p, 0, (u16)v);
    mem_wr16(p, 2, (u16)(v >> 16));
}

static FarP node_next(FarP p)
{
    FarP q;
    q.off = mem_rd16(p, 4);
    q.seg = mem_rd16(p, 6);
    return q;
}

static void node_set_next(FarP p, FarP q)
{
    mem_wr16(p, 4, q.off);
    mem_wr16(p, 6, q.seg);
}

static FarP dsfp(const u16 *a)
{
    FarP p;
    p.off = a[0];
    p.seg = a[1];
    return p;
}

static void dsfp_set(u16 *a, FarP p)
{
    a[0] = p.off;
    a[1] = p.seg;
}

static u8 *fp_c(FarP p) { return &hw_conv[fp_lin(p)]; }

/* @1CF8:000E file:1E78E setblock RTL — INT 21h/4Ah; -1 или максимум.
 * Память DOSBox свободна до 9FFF. */
static u16 dos_setblock(u16 seg, u16 paras)
{
    if ((u32)seg + paras <= 0x9FFF)
        return 0xFFFF;
    return (u16)(0x9FFF - seg);
}

/* @1B8C:0008 file:1D0C8 __brk RTL — блок программы до p (шаг 1 КБ) */
static s16 rtl_brk(FarP p)
{
    u16 si = (u16)((u16)(p.seg + 1 - rtl_psp) + 0x3F) >> 6, di;
    if (si == rtl_brk_kb) {
        dsfp_set(rtl_brklvl, p);
        return 1;
    }
    si <<= 6;
    if ((u16)(si + rtl_psp) > rtl_heaptop[1])
        si = rtl_heaptop[1] - rtl_psp;
    di = dos_setblock(rtl_psp, si);
    if (di == 0xFFFF) {
        rtl_brk_kb = si >> 6;
        dsfp_set(rtl_brklvl, p);
        return 1;
    }
    rtl_heaptop[1] = rtl_psp + di;
    rtl_heaptop[0] = 0;
    return 0;
}

/* @1B8C:009B file:1D15B farbrk RTL */
static s16 rtl_farbrk(FarP p)
{
    if (far_cmp(p, dsfp(rtl_heapbase)) < 0 || far_cmp(p, dsfp(rtl_heaptop)) > 0)
        return -1;
    return rtl_brk(p) != 0 ? 0 : -1;
}

/* @1B8C:00E2 file:1D1A2 farsbrk RTL — сдвиг brk; старый brk или -1 */
static FarP rtl_farsbrk(s32 incr)
{
    FarP p = far_add(dsfp(rtl_brklvl), incr), old, fail = { 0xFFFF, 0xFFFF };
    if (far_cmp(p, dsfp(rtl_heapbase)) < 0 || far_cmp(p, dsfp(rtl_heaptop)) > 0)
        return fail;
    old = dsfp(rtl_brklvl);
    if (!rtl_brk(p))
        return fail;
    return old;
}

/* @1BE0:0008 file:1D608 farmalloc RTL — первый подходящий из свободного
 * списка (с отрезанием хвоста), иначе farsbrk */
u8 *rtl_farmalloc(u32 size)
{
    FarP prev, cur, rover;
    u32 n;
    if (size == 0)
        return NULL;
    n = (size + 9) & 0xFFFFFFFEUL;
    prev = dsfp(rtl_heap_rover);
    cur = node_next(prev);
    for (;;) {
        if (node_size(cur) >= n) {
            if (node_size(cur) <= n + 8) {
                node_set_next(prev, node_next(cur));
            } else {
                node_set_size(cur, node_size(cur) - n);
                cur = far_add(cur, (s32)node_size(cur));
                node_set_size(cur, n);
            }
            dsfp_set(rtl_heap_rover, prev);
            cur.off += 4;
            return fp_c(cur);
        }
        rover = dsfp(rtl_heap_rover);
        if (fp_eq(cur, rover))
            break;
        prev = cur;
        cur = node_next(cur);
    }
    cur = rtl_farsbrk((s32)n);
    if (cur.seg == 0xFFFF && cur.off == 0xFFFF)
        return NULL;
    node_set_size(cur, n);
    cur.off += 4;
    return fp_c(cur);
}

/* @1BBC:000A file:1D3CA farfree RTL — вставка в упорядоченный список со
 * слиянием соседей; блок у brk возвращается системе */
void rtl_farfree(u8 *ptr)
{
    FarP bp, q, top;
    u32 l;
    if (ptr == NULL)
        return;
    l = (u32)(ptr - hw_conv);
    bp.seg = hw_heap_seg(ptr);
    bp.off = (u16)(l - (u32)bp.seg * 16);
    bp = far_add(bp, -4);
    q = dsfp(rtl_heap_rover);
    for (;;) {
        if (far_cmp(bp, q) > 0 && far_cmp(node_next(q), bp) > 0)
            break;
        if (far_cmp(node_next(q), q) <= 0 &&
            (far_cmp(bp, q) > 0 || far_cmp(node_next(q), bp) > 0))
            break;
        q = node_next(q);
    }
    if (far_cmp(far_add(bp, (s32)node_size(bp)), node_next(q)) == 0) {
        node_set_size(bp, node_size(bp) + node_size(node_next(q)));
        node_set_next(bp, node_next(node_next(q)));
    } else {
        node_set_next(bp, node_next(q));
    }
    if (far_cmp(far_add(q, (s32)node_size(q)), bp) == 0) {
        node_set_size(q, node_size(q) + node_size(bp));
        node_set_next(q, node_next(bp));
        bp = q;
    } else {
        node_set_next(q, bp);
    }
    top = rtl_farsbrk(0);
    if (far_cmp(far_add(bp, (s32)node_size(bp)), top) == 0) {
        for (q = bp; far_cmp(node_next(q), bp) != 0; q = node_next(q))
            ;
        node_set_next(q, node_next(bp));
        rtl_farbrk(bp);
    }
    dsfp_set(rtl_heap_rover, q);
}

/* @0000:069E file:01E9E RTL — массив указателей на окружение в куче
 * (в DOSBox — 4 слова far: 3 строки и NULL); содержимое не используется */
void rtl_startup_heap(void)
{
    rtl_psp = 0x193;                    /* DOSBox: PSP 0193 */
    rtl_heapbase[0] = 0;
    rtl_heapbase[1] = 0x2DE3;           /* конец стека программы */
    rtl_brklvl[0] = 0;
    rtl_brklvl[1] = 0x2DE3;
    rtl_heaptop[0] = 0;
    rtl_heaptop[1] = 0x9FFF;
    rtl_farmalloc(0x10);
}

/* @1C31:0053 file:1DB63 fseek RTL */
s16 rtl_fseek(FILE *fp, s32 off, s16 whence)
{
    return (s16)fseek(fp, off, whence);
}

/* @1B7A:0000 file:1CFA0 bioskey RTL — INT 16h: 0 — чтение (ожидание),
 * 1 — проверка (0, если пусто) */
s16 rtl_bioskey(s16 cmd)
{
    if (cmd == 1)
        return (s16)hw_int16_peek();
    return (s16)hw_int16_read();
}

/* @1CE7:0017 file:1E687 rand RTL — LCG Turbo-C */
s16 rtl_rand(void)
{
    rand_seed = rand_seed * 0x15A4E35UL + 1;
    return (s16)((rand_seed >> 16) & 0x7FFF);
}

/* @1B4D:0000 file:1CCD0 sqrt RTL — через эмулятор 8087 */
double rtl_sqrt(double x)
{
    fp_errno = 0;
    return sqrt(x);
}
