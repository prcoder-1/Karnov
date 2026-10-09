/* hw_video.c — видеоадаптеры, видео-BIOS (INT 10h) и вывод в SDL (порт).
 *
 * EGA (режим 0Dh, 320×200×16): четыре плоскости по 64 КБ за A000:0000,
 * регистры секвенсора (map mask) и графического контроллера (set/reset,
 * функция, read map, режим записи, bit mask), защёлки, адрес начала CRTC
 * (страницы) и палитра атрибутов. B800 — 32 КБ общей памяти CGA (режим
 * 4, 320×200×4), Tandy (режим 9, 320×200×16), текста 80×25 и второй
 * страницы Hercules (графика по регистрам 6845 карты). */
#include <SDL2/SDL.h>
#include <string.h>
#include "orig/hw.h"
#include "orig/port.h"

static u8 plane[4][0x10000];
static u8 latch[4];
static u8 seq_idx, seq[8];
static u8 gc_idx, gc[16];
static u8 crtc_idx, crtc[32];
static u8 attr_pal[16] = { 0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63 };
static int attr_flip, attr_idx;
static u8 vram[0x8000];                 /* B800:0000..7FFF */
static u8 cga_sel = 0x30;               /* порт 3D9: палитра, яркость, фон */
static u8 herc_ctl, herc_idx, herc_crtc6845[16];

static u8 bios_mode = 3, cur_row, cur_col;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static int tex_w, tex_h;
static u32 fb[640 * 300];

/* Шрифт ПЗУ 8×8, знаки 20h..7Eh. */
static const u8 font8[95][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ' ' */
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 },   /* '!' */
    { 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '"' */
    { 0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00 },   /* '#' */
    { 0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00 },   /* '$' */
    { 0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00 },   /* '%' */
    { 0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00 },   /* '&' */
    { 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* "'" */
    { 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00 },   /* '(' */
    { 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00 },   /* ')' */
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 },   /* '*' */
    { 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00 },   /* '+' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 },   /* ',' */
    { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 },   /* '-' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 },   /* '.' */
    { 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00 },   /* '/' */
    { 0x38, 0x6C, 0xC6, 0xD6, 0xC6, 0x6C, 0x38, 0x00 },   /* '0' */
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },   /* '1' */
    { 0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x66, 0xFE, 0x00 },   /* '2' */
    { 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00 },   /* '3' */
    { 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00 },   /* '4' */
    { 0xFE, 0xC0, 0xC0, 0xFC, 0x06, 0xC6, 0x7C, 0x00 },   /* '5' */
    { 0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00 },   /* '6' */
    { 0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 },   /* '7' */
    { 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00 },   /* '8' */
    { 0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00 },   /* '9' */
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 },   /* ':' */
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30 },   /* ';' */
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 },   /* '<' */
    { 0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00 },   /* '=' */
    { 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00 },   /* '>' */
    { 0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00 },   /* '?' */
    { 0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00 },   /* '@' */
    { 0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00 },   /* 'A' */
    { 0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00 },   /* 'B' */
    { 0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00 },   /* 'C' */
    { 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00 },   /* 'D' */
    { 0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00 },   /* 'E' */
    { 0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00 },   /* 'F' */
    { 0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3A, 0x00 },   /* 'G' */
    { 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00 },   /* 'H' */
    { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 },   /* 'I' */
    { 0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00 },   /* 'J' */
    { 0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00 },   /* 'K' */
    { 0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00 },   /* 'L' */
    { 0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00 },   /* 'M' */
    { 0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00 },   /* 'N' */
    { 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00 },   /* 'O' */
    { 0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00 },   /* 'P' */
    { 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xCE, 0x7C, 0x0E },   /* 'Q' */
    { 0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00 },   /* 'R' */
    { 0x3C, 0x66, 0x30, 0x18, 0x0C, 0x66, 0x3C, 0x00 },   /* 'S' */
    { 0x7E, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x3C, 0x00 },   /* 'T' */
    { 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00 },   /* 'U' */
    { 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00 },   /* 'V' */
    { 0xC6, 0xC6, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00 },   /* 'W' */
    { 0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00 },   /* 'X' */
    { 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00 },   /* 'Y' */
    { 0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00 },   /* 'Z' */
    { 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00 },   /* '[' */
    { 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00 },   /* '\\' */
    { 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00 },   /* ']' */
    { 0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00 },   /* '^' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF },   /* '_' */
    { 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '`' */
    { 0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00 },   /* 'a' */
    { 0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xDC, 0x00 },   /* 'b' */
    { 0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00 },   /* 'c' */
    { 0x1C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00 },   /* 'd' */
    { 0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00 },   /* 'e' */
    { 0x3C, 0x66, 0x60, 0xF8, 0x60, 0x60, 0xF0, 0x00 },   /* 'f' */
    { 0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8 },   /* 'g' */
    { 0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00 },   /* 'h' */
    { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00 },   /* 'i' */
    { 0x06, 0x00, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C },   /* 'j' */
    { 0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00 },   /* 'k' */
    { 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 },   /* 'l' */
    { 0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xD6, 0x00 },   /* 'm' */
    { 0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00 },   /* 'n' */
    { 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00 },   /* 'o' */
    { 0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0 },   /* 'p' */
    { 0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E },   /* 'q' */
    { 0x00, 0x00, 0xDC, 0x76, 0x60, 0x60, 0xF0, 0x00 },   /* 'r' */
    { 0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00 },   /* 's' */
    { 0x30, 0x30, 0xFC, 0x30, 0x30, 0x36, 0x1C, 0x00 },   /* 't' */
    { 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00 },   /* 'u' */
    { 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00 },   /* 'v' */
    { 0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00 },   /* 'w' */
    { 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00 },   /* 'x' */
    { 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xFC },   /* 'y' */
    { 0x00, 0x00, 0x7E, 0x4C, 0x18, 0x32, 0x7E, 0x00 },   /* 'z' */
    { 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00 },   /* '{' */
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 },   /* '|' */
    { 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00 },   /* '}' */
    { 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '~' */
};

/* 16 цветов CGA (IRGB) — Tandy и фон CGA. */
static const u32 rgbi[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static void ega_reset(void)
{
    memset(plane, 0, sizeof plane);
    memset(gc, 0, sizeof gc);
    seq[2] = 0x0F;
    gc[8] = 0xFF;
    crtc[0x0C] = crtc[0x0D] = 0;
    crtc[0x13] = 20;
}

/* --- INT 10h */

void hw_int10_set_mode(u8 mode)
{
    u16 i;
    bios_mode = mode;
    cur_row = cur_col = 0;
    ega_reset();
    cga_sel = 0x30;
    for (i = 0; i < sizeof vram; i += 2) {
        vram[i] = mode == 3 ? ' ' : 0;
        vram[i + 1] = mode == 3 ? 7 : 0;
    }
}

u8 hw_int10_get_mode(void) { return bios_mode; }
void hw_int10_set_cursor(u8 row, u8 col) { cur_row = row; cur_col = col; }
u16 hw_int10_get_cursor(void) { return (u16)(cur_row << 8 | cur_col); }

/* AH=0Bh, BH=0: фон и яркость, палитра сохраняется */
void hw_int10_set_bg(u8 color) { cga_sel = (u8)((cga_sel & 0x20) | (color & 0x1F)); }

/* Знак в позиции курсора: в тексте — код, в графике —
 * XOR с цветом 7Fh по маске режима (BL = 0FFh у оригинала). */
static void put_glyph(u8 ch)
{
    const u8 *g = ch >= 0x20 && ch < 0x7F ? font8[ch - 0x20] : font8[0];
    u16 x = (u16)(cur_col * 8), l, k;
    if (bios_mode == 3) {               /* атрибут — как у приглашения DOS */
        if (cur_row < 25) {
            vram[(cur_row * 80 + cur_col) * 2] = ch;
            vram[(cur_row * 80 + cur_col) * 2 + 1] = 7;
        }
        return;
    }
    for (l = 0; l < 8; l++) {
        u16 y = (u16)(cur_row * 8 + l);
        for (k = 0; k < 8; k++) {
            u16 px = (u16)(x + k), off;
            if (!(g[l] & (0x80 >> k)))
                continue;
            switch (bios_mode) {
            case 4:
                off = (u16)(0x2000 * (y & 1) + 80 * (y >> 1) + (px >> 2));
                if (off < sizeof vram)
                    vram[off] ^= (u8)(3 << ((3 - (px & 3)) * 2));
                break;
            case 9:
                off = (u16)(0x2000 * (y & 3) + 160 * (y >> 2) + (px >> 1));
                if (off < sizeof vram)
                    vram[off] ^= (u8)(px & 1 ? 0x0F : 0xF0);
                break;
            case 0x0D:
                off = (u16)(y * 40 + (px >> 3));
                plane[0][off] ^= (u8)(0x80 >> (px & 7));
                plane[1][off] ^= (u8)(0x80 >> (px & 7));
                plane[2][off] ^= (u8)(0x80 >> (px & 7));
                plane[3][off] ^= (u8)(0x80 >> (px & 7));
                break;
            }
        }
    }
}

/* перевод строки; текст прокручивается */
static void newline(void)
{
    if (++cur_row < 25 || bios_mode != 3)
        return;
    cur_row = 24;
    memmove(vram, vram + 160, 24 * 160);
    for (cur_col = 0; cur_col < 80; cur_col++) {
        vram[(24 * 80 + cur_col) * 2] = ' ';
        vram[(24 * 80 + cur_col) * 2 + 1] = 7;
    }
    cur_col = 0;
}

/* AH=0Eh */
void hw_int10_teletype(u8 ch)
{
    switch (ch) {
    case '\r':
        cur_col = 0;
        break;
    case '\n':
        newline();
        break;
    case '\b':
        if (cur_col > 0)
            cur_col--;
        break;
    case 7:
        break;
    default:
        put_glyph(ch);
        if (++cur_col >= (bios_mode == 3 ? 80 : 40)) {
            cur_col = 0;
            newline();
        }
        break;
    }
}

/* --- порты */

int hw_video_port_out(u16 port, u8 v)
{
    switch (port) {
    case 0x3C4: seq_idx = v & 7; return 1;
    case 0x3C5: seq[seq_idx] = v; return 1;
    case 0x3CE: gc_idx = v & 15; return 1;
    case 0x3CF: gc[gc_idx] = v; return 1;
    case 0x3D4: crtc_idx = v & 31; return 1;
    case 0x3D5: crtc[crtc_idx] = v; return 1;
    case 0x3D9: cga_sel = v; return 1;
    case 0x3B4: herc_idx = v & 15; return 1;
    case 0x3B5: herc_crtc6845[herc_idx] = v; return 1;
    case 0x3B8: herc_ctl = v; return 1;
    case 0x3BF: return 1;
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

/* --- видеопамять */

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
        vram[(a - 0xB8000) & 0x7FFF] = v;
}

u8 hw_vmem_read(u16 seg, u16 off)
{
    u32 a = (u32)seg * 16 + off;
    if (a >= 0xA0000 && a < 0xB0000)
        return ega_read((u16)(a - 0xA0000));
    if (a >= 0xB8000 && a < 0xC0000)
        return vram[(a - 0xB8000) & 0x7FFF];
    return 0xFF;
}

void hw_vmem_fill(u16 seg, u16 off, u8 v, u16 n)
{
    while (n--)
        hw_vmem_write(seg, off++, v);
}

void hw_vmem_copy(u16 dseg, u16 doff, u16 sseg, u16 soff, u16 n)
{
    while (n--)
        hw_vmem_write(dseg, doff++, hw_vmem_read(sseg, soff++));
}

/* --- вывод кадра */

/* 6-битный цвет атрибута EGA (rgbRGB) → RGB. */
static u32 ega64(u8 c)
{
    u32 r = ((c >> 2) & 1) * 0xAA + ((c >> 5) & 1) * 0x55;
    u32 g = ((c >> 1) & 1) * 0xAA + ((c >> 4) & 1) * 0x55;
    u32 b = ((c >> 0) & 1) * 0xAA + ((c >> 3) & 1) * 0x55;
    return r << 16 | g << 8 | b;
}

static void render_ega(void)
{
    u16 start = (u16)(crtc[0x0C] << 8 | crtc[0x0D]);
    int pitch = crtc[0x13] ? crtc[0x13] * 2 : 40;
    u32 pal[16];
    int x, y, i;
    for (i = 0; i < 16; i++)
        pal[i] = ega64(attr_pal[i]);
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++) {
            u16 off = (u16)(start + y * pitch + (x >> 3));
            int bit = 7 - (x & 7);
            fb[y * 320 + x] = pal[((plane[0][off] >> bit) & 1) | (((plane[1][off] >> bit) & 1) << 1) |
                                  (((plane[2][off] >> bit) & 1) << 2) | (((plane[3][off] >> bit) & 1) << 3)];
        }
}

/* CGA 320×200×4: строки через банк 2000h; палитра по порту 3D9 */
static void render_cga(void)
{
    static const u32 pals[2][3] = { { 0x00AA00, 0xAA0000, 0xAA5500 }, { 0x00AAAA, 0xAA00AA, 0xAAAAAA } };
    u32 pal[4];
    int x, y, i;
    pal[0] = rgbi[cga_sel & 0xF];
    for (i = 0; i < 3; i++)
        pal[i + 1] = pals[(cga_sel >> 5) & 1][i] | (cga_sel & 0x10 ? 0x555555 : 0);
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++)
            fb[y * 320 + x] = pal[(vram[0x2000 * (y & 1) + 80 * (y >> 1) + (x >> 2)] >> ((3 - (x & 3)) * 2)) & 3];
}

/* Tandy 320×200×16: строки через банк 2000h по четыре */
static void render_tandy(void)
{
    int x, y;
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++) {
            u8 b = vram[0x2000 * (y & 3) + 160 * (y >> 2) + (x >> 1)];
            fb[y * 320 + x] = rgbi[x & 1 ? b & 0xF : b >> 4];
        }
}

/* текст 80×25 */
static void render_text(void)
{
    int r, c, l, k;
    for (r = 0; r < 25; r++)
        for (c = 0; c < 80; c++) {
            u8 ch = vram[(r * 80 + c) * 2], at = vram[(r * 80 + c) * 2 + 1];
            const u8 *g = ch >= 0x20 && ch < 0x7F ? font8[ch - 0x20] : font8[0];
            for (l = 0; l < 8; l++)
                for (k = 0; k < 8; k++)
                    fb[(r * 8 + l) * 640 + c * 8 + k] = rgbi[g[l] & (0x80 >> k) ? at & 0xF : (at >> 4) & 7];
        }
}

/* Hercules: ширина R1 слов, R6 строк по R9+1 развёрток; развёртка
 * выбирает банк 2000h; вторая страница — B800 */
static void render_herc(int *w, int *h)
{
    const u8 *r = herc_crtc6845;
    int scan = (r[9] & 3) + 1, x, y;
    u16 start = (u16)((r[12] << 8 | r[13]) * 2);
    *w = r[1] * 16;
    *h = r[6] * scan;
    if (*w > 640) *w = 640;
    if (*h > 300) *h = 300;
    for (y = 0; y < *h; y++)
        for (x = 0; x < *w; x++) {
            u16 off = (u16)(0x2000 * (y % scan) + start + (y / scan) * r[1] * 2 + (x >> 3));
            int on = herc_ctl & 0x80 && off < sizeof vram && (vram[off] & (0x80 >> (x & 7)));
            fb[y * *w + x] = on ? 0xFFFFFF : 0;
        }
}

/* Кадр в fb по текущему режиму; размер — в *w, *h. */
static void render(int *w, int *h)
{
    *w = 320;
    *h = 200;
    if ((herc_ctl & 0x0A) == 0x0A) {
        render_herc(w, h);
        return;
    }
    switch (bios_mode) {
    case 0x0D: render_ega(); break;
    case 4: render_cga(); break;
    case 9: render_tandy(); break;
    case 3: render_text(); *w = 640; break;
    default: memset(fb, 0, sizeof fb); break;
    }
}

void hw_video_present(void)
{
    int w, h;
    if (port_opt.headless)
        return;
    if (!win) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
            return;
        win = SDL_CreateWindow("Karnov", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               320 * 3, 240 * 3, SDL_WINDOW_RESIZABLE);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        SDL_RenderSetLogicalSize(ren, 640, 480);
    }
    render(&w, &h);
    if (w != tex_w || h != tex_h) {
        if (tex)
            SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
        tex_w = w;
        tex_h = h;
    }
    SDL_UpdateTexture(tex, NULL, fb, w * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}
