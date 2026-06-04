/* sprite.c — KS v3 cache loader.
 *
 * Кэш сделан tools/sprites_extract_{cga,ega}.py (см. karnov_cpp/).
 * Формат:
 *   u16 magic  = 'K' | 'S'<<8 = 0x534B
 *   u16 version = 3
 *   u16 count
 *   per-sprite: u16 id, u8 w, u8 h, u8 bpp, u8 pixels[w*h]
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
    if (ver != 3) { free(buf); return 6; }
    if (count > MAX_SPRITES) count = MAX_SPRITES;

    /* Освободить предыдущий blob если был. */
    if (g_blob) free(g_blob);
    g_blob = buf;
    g_blob_size = (size_t)sz;
    g_count = 0;
    g_is_cga = false;

    /* Per-sprite header: u16 id, u8 w, u8 h, u8 bpp, u8 reserved = 6 байт. */
    size_t off = 6;
    bool seen_bpp = false;
    for (u16 i = 0; i < count; ++i) {
        if (off + 6 > g_blob_size) break;
        u16 id  = (u16)g_blob[off] | ((u16)g_blob[off+1] << 8);
        u8  w   = g_blob[off+2];
        u8  h   = g_blob[off+3];
        u8  bpp = g_blob[off+4];
        /* reserved at off+5 */
        off += 6;
        if (off + (size_t)w*h > g_blob_size) break;
        if (g_count >= MAX_SPRITES) break;
        g_table[g_count].id     = id;
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
