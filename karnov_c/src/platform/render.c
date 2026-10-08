/* render.c — SDL2 окно + framebuffer + тайл/спрайт-блиттеры + 5×7 HUD-шрифт.
 */
#include "render.h"
#include "palette.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ─── SDL state ──────────────────────────────────────────────────────── */
static SDL_Window   *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static SDL_Texture  *g_tex = NULL;
static u32          *g_fb  = NULL;
static bool          g_headless = false;

int render_init(int scale, bool headless)
{
    g_fb = (u32 *)calloc((size_t)WIN_W * WIN_H, sizeof(u32));
    if (!g_fb) return 1;
    g_headless = headless;
    if (headless) return 0;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_InitSubSystem(VIDEO): %s\n", SDL_GetError());
        return 2;
    }
    g_win = SDL_CreateWindow("Karnov (C99 port)",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             WIN_W * scale, WIN_H * scale, SDL_WINDOW_SHOWN);
    if (!g_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 3; }

    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 4; }
    SDL_RenderSetLogicalSize(g_ren, WIN_W, WIN_H);

    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, WIN_W, WIN_H);
    if (!g_tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return 5; }
    return 0;
}

void render_shutdown(void)
{
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    g_tex = NULL; g_ren = NULL; g_win = NULL;
    free(g_fb); g_fb = NULL;
    if (!g_headless) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

u32 *render_framebuffer(void) { return g_fb; }

void render_clear(u32 color)
{
    for (size_t i = 0; i < (size_t)WIN_W * WIN_H; ++i) g_fb[i] = color;
}

bool render_pump_events(void)
{
    if (g_headless) return false;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return true;
        if (ev.type == SDL_KEYDOWN) {
            SDL_Keycode k = ev.key.keysym.sym;
            if (k == SDLK_ESCAPE || k == SDLK_q) return true;
        }
    }
    return false;
}

void render_present(void)
{
    if (g_headless || !g_tex) return;
    SDL_UpdateTexture(g_tex, NULL, g_fb, WIN_W * (int)sizeof(u32));
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

void render_fill_rect(int x, int y, int w, int h, u32 color)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > WIN_W) x1 = WIN_W;
    if (y1 > WIN_H) y1 = WIN_H;
    for (int r = y; r < y1; ++r) {
        u32 *row = g_fb + r * WIN_W;
        for (int c = x; c < x1; ++c) row[c] = color;
    }
}

void render_blit_tile(int dst_x, int dst_y, const u8 *gfx, u8 attr, bool mode_cga)
{
    /* 4 байта × 10 строк, packed 2bpp. Старшие биты = левый пиксель. */
    for (int row = 0; row < TILE_H; ++row) {
        const u8 *src = gfx + row * 4;
        int yy = dst_y + row;
        if (yy < 0 || yy >= WIN_H) continue;
        u32 *d = g_fb + yy * WIN_W + dst_x;
        int rem_x = WIN_W - dst_x;
        for (int b = 0; b < 4; ++b) {
            u8 byte = src[b];
            u8 p[4] = {
                (u8)((byte >> 6) & 3), (u8)((byte >> 4) & 3),
                (u8)((byte >> 2) & 3), (u8)((byte >> 0) & 3)
            };
            for (int j = 0; j < 4; ++j) {
                int col = b * 4 + j;
                if ((dst_x + col) < 0 || col >= rem_x) continue;
                u32 c = mode_cga ? CGA_PAL_1H[p[j]] : palette_resolve_ega(attr, p[j]);
                d[col] = c;
            }
        }
    }
}

void render_blit_tile_ega(int dst_x, int dst_y, const u8 *gfx_ega, u8 attr)
{
    (void)attr;
    /* EGA planar 4bpp:
     *   80 байт = 4 plane × 20 байт. Каждый plane = 16×10 пикселей @ 1bpp.
     *   Plane stride = 20 байт (2 байта на строку × 10 строк).
     *   pixel_color = P0 | (P1<<1) | (P2<<2) | (P3<<3), 0..15 → EGA_PAL16.
     *
     * Это совпадает с EGA-режимом 0Dh (320×200×16) у IBM PC, где каждый
     * битовый план хранится отдельно. Все 4 плана объединяются per-pixel
     * для получения 4-битного индекса цвета.
     *
     * Бит pixel (col, row) → byte at plane*20 + row*2 + col/8, bit (7 - col%8).
     */
    const u8 *p0 = gfx_ega;
    const u8 *p1 = gfx_ega + 20;
    const u8 *p2 = gfx_ega + 40;
    const u8 *p3 = gfx_ega + 60;
    for (int row = 0; row < TILE_H; ++row) {
        int yy = dst_y + row;
        if (yy < 0 || yy >= WIN_H) continue;
        int row_off = row * 2;
        for (int col = 0; col < TILE_W; ++col) {
            int byte_idx = row_off + (col >> 3);
            int bit = 7 - (col & 7);
            u8 v = (u8)(((p0[byte_idx] >> bit) & 1) |
                        (((p1[byte_idx] >> bit) & 1) << 1) |
                        (((p2[byte_idx] >> bit) & 1) << 2) |
                        (((p3[byte_idx] >> bit) & 1) << 3));
            int xx = dst_x + col;
            if (xx < 0 || xx >= WIN_W) continue;
            g_fb[yy * WIN_W + xx] = EGA_PAL16[v];
        }
    }
}

void render_blit_ladder(int cx, int cy_top, int cy_bot,
                        const u8 *pixels, int w, int h, const u32 *palette16)
{
    int x0 = cx - w / 2;
    for (int yy = cy_top; yy < cy_bot && yy < WIN_H; ++yy) {
        if (yy < 0) continue;
        int sr = (yy - cy_top) % h;
        u32 *d = g_fb + yy * WIN_W;
        for (int x = 0; x < w; ++x) {
            u8 v = pixels[sr * w + x];
            if (v == 0xFF) continue;
            int xx = x0 + x;
            if (xx < 0 || xx >= WIN_W) continue;
            d[xx] = palette16[v & 15];
        }
    }
}

void render_blit_sprite(int dst_x, int dst_y, const u8 *pixels, int w, int h,
                        bool mirror, const u32 *palette16)
{
    for (int r = 0; r < h; ++r) {
        int yy = dst_y + r;
        if (yy < 0 || yy >= WIN_H) continue;
        u32 *d = g_fb + yy * WIN_W;
        for (int c = 0; c < w; ++c) {
            int sc = mirror ? (w - 1 - c) : c;
            u8 v = pixels[r * w + sc];
            if (v == 0xFF) continue;  /* transparent */
            int xx = dst_x + c;
            if (xx < 0 || xx >= WIN_W) continue;
            d[xx] = palette16[v & 15];
        }
    }
}

int render_save_ppm(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 1;
    fprintf(fp, "P6\n%d %d\n255\n", WIN_W, WIN_H);
    u8 *line = (u8 *)malloc((size_t)WIN_W * 3);
    if (!line) { fclose(fp); return 2; }
    for (int y = 0; y < WIN_H; ++y) {
        for (int x = 0; x < WIN_W; ++x) {
            u32 c = g_fb[y * WIN_W + x];
            line[x * 3 + 0] = (u8)((c >> 16) & 0xFF);
            line[x * 3 + 1] = (u8)((c >>  8) & 0xFF);
            line[x * 3 + 2] = (u8)((c >>  0) & 0xFF);
        }
        fwrite(line, 1, (size_t)WIN_W * 3, fp);
    }
    free(line);
    fclose(fp);
    return 0;
}

/* ─── 5×7 битмапный шрифт ────────────────────────────────────────────── */
static const u8 FONT5x7[] = {
 /*0*/ 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E, /*1*/ 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,
 /*2*/ 0x0E,0x11,0x01,0x06,0x08,0x10,0x1F, /*3*/ 0x0E,0x11,0x01,0x06,0x01,0x11,0x0E,
 /*4*/ 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02, /*5*/ 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,
 /*6*/ 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E, /*7*/ 0x1F,0x01,0x02,0x04,0x08,0x08,0x08,
 /*8*/ 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E, /*9*/ 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,
 /*':'*/0x00,0x04,0x04,0x00,0x04,0x04,0x00, /*' '*/0,0,0,0,0,0,0,
 /*A*/  0x0E,0x11,0x11,0x1F,0x11,0x11,0x11, /*B*/  0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,
 /*C*/  0x0E,0x11,0x10,0x10,0x10,0x11,0x0E, /*D*/  0x1E,0x11,0x11,0x11,0x11,0x11,0x1E,
 /*E*/  0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F, /*F*/  0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,
 /*G*/  0x0E,0x11,0x10,0x17,0x11,0x11,0x0E, /*H*/  0x11,0x11,0x11,0x1F,0x11,0x11,0x11,
 /*I*/  0x0E,0x04,0x04,0x04,0x04,0x04,0x0E, /*J*/  0x07,0x02,0x02,0x02,0x02,0x12,0x0C,
 /*K*/  0x11,0x12,0x14,0x18,0x14,0x12,0x11, /*L*/  0x10,0x10,0x10,0x10,0x10,0x10,0x1F,
 /*M*/  0x11,0x1B,0x15,0x11,0x11,0x11,0x11, /*N*/  0x11,0x19,0x15,0x13,0x11,0x11,0x11,
 /*O*/  0x0E,0x11,0x11,0x11,0x11,0x11,0x0E, /*P*/  0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,
 /*Q*/  0x0E,0x11,0x11,0x11,0x15,0x12,0x0D, /*R*/  0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,
 /*S*/  0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E, /*T*/  0x1F,0x04,0x04,0x04,0x04,0x04,0x04,
 /*U*/  0x11,0x11,0x11,0x11,0x11,0x11,0x0E, /*V*/  0x11,0x11,0x11,0x11,0x11,0x0A,0x04,
 /*W*/  0x11,0x11,0x11,0x15,0x15,0x15,0x0A, /*X*/  0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,
 /*Y*/  0x11,0x11,0x0A,0x04,0x04,0x04,0x04, /*Z*/  0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,
 /*'/'*/ 0x01,0x01,0x02,0x04,0x08,0x10,0x10, /*'#'*/ 0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,
 /*'.'*/ 0x00,0x00,0x00,0x00,0x00,0x00,0x04, /*'-'*/ 0x00,0x00,0x00,0x0E,0x00,0x00,0x00,
 /*a*/   0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F, /*b*/   0x10,0x10,0x1E,0x11,0x11,0x11,0x1E,
 /*c*/   0x00,0x00,0x0E,0x11,0x10,0x11,0x0E, /*d*/   0x01,0x01,0x0F,0x11,0x11,0x11,0x0F,
 /*e*/   0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E, /*f*/   0x06,0x09,0x08,0x1E,0x08,0x08,0x08,
 /*g*/   0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E, /*h*/   0x10,0x10,0x1E,0x11,0x11,0x11,0x11,
 /*i*/   0x04,0x00,0x0C,0x04,0x04,0x04,0x0E, /*j*/   0x02,0x00,0x06,0x02,0x02,0x12,0x0C,
 /*k*/   0x10,0x10,0x12,0x14,0x18,0x14,0x12, /*l*/   0x0C,0x04,0x04,0x04,0x04,0x04,0x0E,
 /*m*/   0x00,0x00,0x1A,0x15,0x15,0x15,0x15, /*n*/   0x00,0x00,0x1E,0x11,0x11,0x11,0x11,
 /*o*/   0x00,0x00,0x0E,0x11,0x11,0x11,0x0E, /*p*/   0x00,0x00,0x1E,0x11,0x1E,0x10,0x10,
 /*q*/   0x00,0x00,0x0F,0x11,0x0F,0x01,0x01, /*r*/   0x00,0x00,0x16,0x19,0x10,0x10,0x10,
 /*s*/   0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E, /*t*/   0x08,0x08,0x1E,0x08,0x08,0x09,0x06,
 /*u*/   0x00,0x00,0x11,0x11,0x11,0x13,0x0D, /*v*/   0x00,0x00,0x11,0x11,0x11,0x0A,0x04,
 /*w*/   0x00,0x00,0x11,0x11,0x15,0x15,0x0A, /*x*/   0x00,0x00,0x11,0x0A,0x04,0x0A,0x11,
 /*y*/   0x00,0x00,0x11,0x11,0x0F,0x01,0x0E, /*z*/   0x00,0x00,0x1F,0x02,0x04,0x08,0x1F,
 /*'('*/ 0x02,0x04,0x08,0x08,0x08,0x04,0x02, /*')'*/ 0x08,0x04,0x02,0x02,0x02,0x04,0x08,
};
static const char *FONT_CHARS =
    "0123456789: ABCDEFGHIJKLMNOPQRSTUVWXYZ/#.-"
    "abcdefghijklmnopqrstuvwxyz()";

static int find_char(char c)
{
    for (int i = 0; FONT_CHARS[i]; ++i) if (FONT_CHARS[i] == c) return i;
    /* Lowercase requested но glyph отсутствует — fallback на uppercase. */
    if (c >= 'a' && c <= 'z') {
        char up = (char)(c - 32);
        for (int i = 0; FONT_CHARS[i]; ++i) if (FONT_CHARS[i] == up) return i;
    }
    return 11;  /* space */
}

static void put_char(int x, int y, char c, u32 color)
{
    int idx = find_char(c);
    const u8 *g = &FONT5x7[idx * 7];
    for (int r = 0; r < 7; ++r) {
        if (y + r < 0 || y + r >= WIN_H) continue;
        u8 row = g[r];
        for (int b = 0; b < 5; ++b) {
            if (row & (1 << (4 - b))) {
                int xx = x + b;
                if (xx >= 0 && xx < WIN_W) g_fb[(y + r) * WIN_W + xx] = color;
            }
        }
    }
}

void render_draw_text(int x, int y, const char *s, u32 color)
{
    for (; *s; ++s, x += 6) put_char(x, y, *s, color);
}
