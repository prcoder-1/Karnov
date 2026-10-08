/* hw_video.c — видеокарта EGA (режим 0Dh, 320×200×16) и вывод в SDL (порт).
 *
 * Эмулируется то, чем пользуется игра: четыре плоскости по 64 КБ за
 * A000:0000, регистры секвенсора (map mask) и графического контроллера
 * (set/reset, функция, read map, режим записи, bit mask), защёлки,
 * адрес начала CRTC (страницы) и палитра атрибутов. Видеопамять CGA
 * (B800) — линейные 32 КБ. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "orig/hw.h"
#include "orig/port.h"
#include "palette.h"

static u8 plane[4][0x10000];
static u8 latch[4];
static u8 seq_idx, seq[8];
static u8 gc_idx, gc[16];
static u8 crtc_idx, crtc[32];
static u8 attr_pal[16] = { 0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63 };
static int attr_flip, attr_idx;
static u8 cga_mem[0x8000];

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static u32 fb[320 * 200];

void hw_video_reset(void)
{
    memset(plane, 0, sizeof plane);
    memset(gc, 0, sizeof gc);
    seq[2] = 0x0F;
    gc[8] = 0xFF;
    crtc[0x0C] = crtc[0x0D] = 0;
    crtc[0x13] = 20;
}

int hw_video_port_out(u16 port, u8 v)
{
    switch (port) {
    case 0x3C4: seq_idx = v & 7; return 1;
    case 0x3C5: seq[seq_idx] = v; return 1;
    case 0x3CE: gc_idx = v & 15; return 1;
    case 0x3CF: gc[gc_idx] = v; return 1;
    case 0x3D4: crtc_idx = v & 31; return 1;
    case 0x3D5: crtc[crtc_idx] = v; return 1;
    case 0x3C0:
        if (!attr_flip)
            attr_idx = v & 0x1F;
        else if (attr_idx < 16)
            attr_pal[attr_idx] = v & 0x3F;
        attr_flip ^= 1;
        return 1;
    default:
        return 0;
    }
}

int hw_video_port_in(u16 port, u8 *v)
{
    if (port == 0x3DA) {
        attr_flip = 0;
        *v = 0x09;      /* обратный ход по вертикали — всегда «есть» */
        return 1;
    }
    return 0;
}

static u8 rotate(u8 v)
{
    u8 n = gc[3] & 7;
    return (u8)((v >> n) | (v << ((8 - n) & 7)));
}

static void ega_write(u16 off, u8 v)
{
    u8 mask = seq[2], bm = gc[8], mode = gc[5] & 3, fn = (gc[3] >> 3) & 3;
    int p;
    for (p = 0; p < 4; p++) {
        u8 d;
        if (!(mask & (1 << p)))
            continue;
        if (mode == 1) {
            plane[p][off] = latch[p];
            continue;
        }
        if (mode == 2)
            d = (v & (1 << p)) ? 0xFF : 0x00;
        else if (gc[1] & (1 << p))
            d = (gc[0] & (1 << p)) ? 0xFF : 0x00;
        else
            d = rotate(v);
        switch (fn) {
        case 1: d &= latch[p]; break;
        case 2: d |= latch[p]; break;
        case 3: d ^= latch[p]; break;
        default: break;
        }
        plane[p][off] = (u8)((d & bm) | (latch[p] & ~bm));
    }
}

static u8 ega_read(u16 off)
{
    int p;
    for (p = 0; p < 4; p++)
        latch[p] = plane[p][off];
    return plane[gc[4] & 3][off];
}

void hw_vmem_write(u16 seg, u16 off, u8 v)
{
    u32 a = (u32)seg * 16 + off;
    if (a >= 0xA0000 && a < 0xB0000)
        ega_write((u16)(a - 0xA0000), v);
    else if (a >= 0xB8000 && a < 0xC0000)
        cga_mem[(a - 0xB8000) & 0x7FFF] = v;
}

u8 hw_vmem_read(u16 seg, u16 off)
{
    u32 a = (u32)seg * 16 + off;
    if (a >= 0xA0000 && a < 0xB0000)
        return ega_read((u16)(a - 0xA0000));
    if (a >= 0xB8000 && a < 0xC0000)
        return cga_mem[(a - 0xB8000) & 0x7FFF];
    return 0xFF;
}

void hw_vmem_fill(u16 seg, u16 off, u8 v, u16 n)
{
    while (n--)
        hw_vmem_write(seg, off++, v);
}

/* 6-битный цвет атрибута EGA (rgbRGB) → ARGB. */
static u32 ega64(u8 c)
{
    u32 r = ((c >> 2) & 1) * 0xAA + ((c >> 5) & 1) * 0x55;
    u32 g = ((c >> 1) & 1) * 0xAA + ((c >> 4) & 1) * 0x55;
    u32 b = ((c >> 0) & 1) * 0xAA + ((c >> 3) & 1) * 0x55;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

static void render_frame(void)
{
    u16 start = (u16)(crtc[0x0C] << 8 | crtc[0x0D]);
    int pitch = crtc[0x13] ? crtc[0x13] * 2 : 40;
    u32 pal[16];
    int x, y, i;
    for (i = 0; i < 16; i++)
        pal[i] = ega64(attr_pal[i]);
    (void)EGA_PAL16;
    for (y = 0; y < 200; y++) {
        for (x = 0; x < 320; x++) {
            u16 off = (u16)(start + y * pitch + (x >> 3));
            int bit = 7 - (x & 7), c;
            c = ((plane[0][off] >> bit) & 1) | (((plane[1][off] >> bit) & 1) << 1) |
                (((plane[2][off] >> bit) & 1) << 2) | (((plane[3][off] >> bit) & 1) << 3);
            fb[y * 320 + x] = pal[c];
        }
    }
}

void hw_video_present(void)
{
    if (port_opt.headless)
        return;
    if (!win) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
            return;
        win = SDL_CreateWindow("Karnov", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               320 * 3, 240 * 3, SDL_WINDOW_RESIZABLE);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        SDL_RenderSetLogicalSize(ren, 320, 240);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 320, 200);
    }
    render_frame();
    SDL_UpdateTexture(tex, NULL, fb, 320 * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

/* Снимок экрана в PPM (для визуальной сверки). */
int hw_video_save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f)
        return -1;
    render_frame();
    fprintf(f, "P6\n320 200\n255\n");
    for (i = 0; i < 320 * 200; i++) {
        u8 rgb[3] = { (u8)(fb[i] >> 16), (u8)(fb[i] >> 8), (u8)fb[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

void hw_vmem_copy(u16 dseg, u16 doff, u16 sseg, u16 soff, u16 n)
{
    while (n--)
        hw_vmem_write(dseg, doff++, hw_vmem_read(sseg, soff++));
}
