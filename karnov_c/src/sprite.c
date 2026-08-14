/* sprite.c — KS v3/v4 cache loader.
 *
 * Кэш сделан docs/reference/tools/sprites_extract_{cga,ega_disk}.py.
 * Формат:
 *   u16 magic  = 'K' | 'S'<<8 = 0x534B
 *   u16 version = 3 | 4
 *   u16 count
 *   per-sprite v3: u16 id, u8 w, u8 h, u8 bpp, u8 reserved, u8 pixels[w*h]
 *   per-sprite v4: ... + u16 ai_type перед pixels
 *   pixel: 0..15 (EGA bpp=4) или 0..3 (CGA bpp=2); 0xFF = прозрачный
 */
#include "sprite.h"
#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SPRITES 512

static u8       *g_blob = NULL;
static size_t    g_blob_size = 0;
static sprite_t  g_table[MAX_SPRITES];
static int       g_count = 0;
static bool      g_is_cga = false;

int sprite_cache_count(void) { return g_count; }
bool sprite_is_cga(void)     { return g_is_cga; }

int sprite_cache_load(const char *path)
{
    FILE *fp = karnov_fopen(path, "rb");
    if (!fp) return 1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 6) { fclose(fp); return 2; }

    u8 *buf = (u8 *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return 3; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); fclose(fp); return 4; }
    fclose(fp);

    /* magic 'KS' (LE byte order: 0x4B 0x53). */
    if (!(buf[0] == 'K' && buf[1] == 'S')) { free(buf); return 5; }
    u16 ver   = (u16)buf[2] | ((u16)buf[3] << 8);
    u16 count = (u16)buf[4] | ((u16)buf[5] << 8);
    if (ver != 3 && ver != 4) { free(buf); return 6; }
    if (count > MAX_SPRITES) count = MAX_SPRITES;
    size_t hdr_len = (ver >= 4) ? 8 : 6;

    /* Освободить предыдущий blob если был. */
    if (g_blob) free(g_blob);
    g_blob = buf;
    g_blob_size = (size_t)sz;
    g_count = 0;
    g_is_cga = false;

    /* Per-sprite header: u16 id, u8 w, u8 h, u8 bpp, u8 reserved (+u16 ai_type в v4). */
    size_t off = 6;
    bool seen_bpp = false;
    for (u16 i = 0; i < count; ++i) {
        if (off + hdr_len > g_blob_size) break;
        u16 id  = (u16)g_blob[off] | ((u16)g_blob[off+1] << 8);
        u8  w   = g_blob[off+2];
        u8  h   = g_blob[off+3];
        u8  bpp = g_blob[off+4];
        /* reserved at off+5 */
        u16 ai  = (ver >= 4) ? ((u16)g_blob[off+6] | ((u16)g_blob[off+7] << 8))
                             : (u16)0xFFFF;
        off += hdr_len;
        if (off + (size_t)w*h > g_blob_size) break;
        if (g_count >= MAX_SPRITES) break;
        g_table[g_count].id      = id;
        g_table[g_count].ai_type = ai;
        g_table[g_count].w      = w;
        g_table[g_count].h      = h;
        g_table[g_count].bpp    = bpp;
        g_table[g_count].pixels = &g_blob[off];
        if (!seen_bpp) { g_is_cga = (bpp == 2); seen_bpp = true; }
        /* Pre-compute body horizontal center for seamless facing-flip. */
        int min_c = w, max_c = -1;
        for (int r = 0; r < h; ++r) {
            for (int c = 0; c < w; ++c) {
                if (g_blob[off + r*w + c] != 0xFF) {
                    if (c < min_c) min_c = c;
                    if (c > max_c) max_c = c;
                }
            }
        }
        g_table[g_count].body_cx = (max_c >= 0) ? (u8)((min_c + max_c) / 2) : (u8)(w / 2);
        off += (size_t)w*h;
        ++g_count;
    }
    return 0;
}

const sprite_t *sprite_find(u16 id)
{
    for (int i = 0; i < g_count; ++i) {
        if (g_table[i].id == id) return &g_table[i];
    }
    return NULL;
}

/* Манифест sprite→AI type из директории SPRITES.EGA/CGA (слово 0 каждой
 * записи, в кэше с KS v4). Id спрайтов одного типа идут непрерывным
 * диапазоном (пров. по дампу манифеста: ai=14 → 149..152, ai=29 →
 * 211..217, ...), так что (base, count) полностью описывают анимацию.
 * Возвращает false, если для типа спрайтов нет (или кэш v3). */
bool sprite_manifest(u16 ai_type, u16 *base, u16 *count)
{
    u16 b = 0xFFFF, n = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_table[i].ai_type != ai_type) continue;
        if (g_table[i].id < b) b = g_table[i].id;
        ++n;
    }
    if (n == 0) return false;
    *base = b; *count = n;
    return true;
}
