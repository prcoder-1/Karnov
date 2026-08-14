/* sprites.cpp — загрузчик кэша спрайтов Karnov.
 *
 * Кэш data/karnov_sprites.bin производится скриптом
 * tools/sprites_extract.py из runtime-дампа DOSBox.
 *
 * Формат кэша (version 3, 4bpp EGA):
 *   uint16 magic   = 'KS' (0x534B)
 *   uint16 version = 3
 *   uint16 count
 *   per sprite:
 *     uint16 sprite_id   — on-disk dir index
 *     uint8  width       — count * 16 (16..96)
 *     uint8  height      — width field of strip header
 *     uint8  bpp = 4
 *     uint8  reserved
 *     uint8  pixels[width * height]   — 0..15 EGA-индекс или 0xFF = прозрачный
 */
#include "sprites.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace karnov {
namespace {

struct CacheEntry {
    int sprite_id;
    int w, h, bpp;
    unsigned char pixels[SPR_MAX_WIDTH * SPR_MAX_HEIGHT];
};

CacheEntry g_cache[512];
int g_count = 0;

u16 rd16(const u8* p) { return (u16)p[0] | ((u16)p[1] << 8); }

} /* namespace */

int sprites_load_cache(const char* path)
{
    g_count = 0;
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return 1;

    u8 hdr[6];
    if (std::fread(hdr, 1, 6, fp) != 6) { std::fclose(fp); return 2; }
    u16 magic   = rd16(hdr);
    u16 version = rd16(hdr + 2);
    u16 count   = rd16(hdr + 4);
    if (magic != 0x534B || version != 3) { std::fclose(fp); return 3; }
    if (count > 512) count = 512;

    for (int i = 0; i < count; ++i) {
        u8 entry_hdr[6];
        if (std::fread(entry_hdr, 1, 6, fp) != 6) break;
        int sid = rd16(entry_hdr);
        int w   = entry_hdr[2];
        int h   = entry_hdr[3];
        int bpp = entry_hdr[4];
        int sz  = w * h;
        if (w < 1 || w > SPR_MAX_WIDTH || h < 1 || h > SPR_MAX_HEIGHT) {
            std::fseek(fp, sz, SEEK_CUR);
            continue;
        }
        CacheEntry& e = g_cache[g_count++];
        e.sprite_id = sid;
        e.w = w; e.h = h; e.bpp = bpp;
        if (std::fread(e.pixels, 1, sz, fp) != (size_t)sz) {
            --g_count;
            break;
        }
    }
    std::fclose(fp);
    return 0;
}

int sprites_count() { return g_count; }

void sprites_decode(int idx, DecodedSprite& out)
{
    out.valid = false;
    out.w = 0; out.h = 0; out.bpp = 0;
    if (idx < 0 || idx >= g_count) return;
    const CacheEntry& e = g_cache[idx];
    out.w = e.w; out.h = e.h; out.bpp = e.bpp;
    for (int y = 0; y < e.h; ++y) {
        for (int x = 0; x < e.w; ++x) {
            u8 v = e.pixels[y * e.w + x];
            out.px[y * e.w + x] = (v == 0xFF) ? -1 : (int)v;
        }
    }
    out.valid = true;
}

int sprites_find_by_id(int sprite_id)
{
    for (int i = 0; i < g_count; ++i) {
        if (g_cache[i].sprite_id == sprite_id) return i;
    }
    return -1;
}

} /* namespace karnov */
