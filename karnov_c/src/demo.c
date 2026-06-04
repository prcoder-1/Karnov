/* demo.c — LEVEL%d.MOV reader + tick.
 *
 * Формат:
 *   uint16 start_idx (обычно 0)
 *   uint16 num_records
 *   N×{ u8 state; u16_le frame } records (3 байта, packed)
 *
 * Биты state: 0x01=R 0x02=L 0x04=D 0x08=U 0x10=FIRE 0x20=OPT.
 */
#include "demo.h"
#include "file_io.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct { u8 state; u16 frame; } mov_rec_t;

#define MOV_CAP 1024
static mov_rec_t g_recs[MOV_CAP];
static u16 g_num   = 0;
static u16 g_idx   = 0;
static u8  g_input = 0;

static const char *DATA_DIRS[] = { "", "data/", "../" };

static FILE *try_open_raw(const char *filename)
{
    char buf[256];
    for (size_t i = 0; i < sizeof(DATA_DIRS)/sizeof(DATA_DIRS[0]); ++i) {
        snprintf(buf, sizeof(buf), "%s%s", DATA_DIRS[i], filename);
        FILE *fp = karnov_fopen(buf, "rb");
        if (fp) return fp;
    }
    return NULL;
}

int demo_load(u16 level_num)
{
    char path[32];
    snprintf(path, sizeof(path), "level%u.mov", level_num);
    FILE *fp = try_open_raw(path);
    if (!fp) return 1;

    u16 hdr[2] = { 0, 0 };
    if (fread(hdr, 2, 2, fp) != 2) { fclose(fp); return 2; }
    g_idx = hdr[0];
    g_num = hdr[1];
    if (g_num > MOV_CAP) g_num = MOV_CAP;

    for (u16 i = 0; i < g_num; ++i) {
        u8 raw[3];
        if (fread(raw, 1, 3, fp) != 3) { g_num = i; break; }
        g_recs[i].state = raw[0];
        g_recs[i].frame = (u16)raw[1] | ((u16)raw[2] << 8);
    }
    fclose(fp);
    g_input = 0;
    return 0;
}

bool demo_tick(u16 current_frame)
{
    while (g_idx < g_num && g_recs[g_idx].frame <= current_frame) {
        g_input = g_recs[g_idx].state;
        ++g_idx;
    }
    return g_idx >= g_num;
}

u8   demo_input_state(void)   { return g_input; }
u16  demo_current_index(void) { return g_idx; }
u16  demo_record_count(void)  { return g_num; }
void demo_reset(void)         { g_idx = 0; g_input = 0; }
void demo_clear(void)         { g_idx = 0; g_input = 0; g_num = 0; }
