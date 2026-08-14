/* splash.cpp — загрузка и отображение заставки Karnov (LEVEL0.BIN)
 *               + воспроизведение демо LEVEL0.MOV
 *               + loader-music из cs:0x40E (MUSIC_0A6A, track 6 audio.cpp).
 *
 * Полная декомпиляция следующих подсистем KARNOV.EXE:
 *
 *   File-I/O с прозрачной RLE-распаковкой (маркер 0x5A):
 *     cs:0x58B:0x006   file 0x70B6   bin_fopen
 *     cs:0x58B:0x04C   file 0x70FC   bin_fclose
 *     cs:0x58B:0x077   file 0x7127   bin_fread
 *     cs:0x58B:0x135   file 0x71E5   read_byte (буфер 512 байт)
 *
 *   Загрузка уровня:
 *     file 0x5350      load_level         (читает level%1d.bin)
 *     file 0x51B6      read_object_regions (12-байтные ObjectRegion)
 *     file 0x5216      read_enemies       (20-байтные EnemySpawn)
 *
 *   Тайл-рендер:
 *     file 0x4076      draw_tile (cs:0x287:0x06) для level_mode 0/2 —
 *                      копирует 4 байта × 10 строк (= тайл 16×10 px @ 2bpp).
 *
 *   Демо (attract-mode):
 *     cs:0x6E16       load_demo  (открывает level%d.mov, без RLE)
 *     cs:0x6DC0       tick_demo  (применяет очередное событие к input_state)
 *
 *   Музыка:
 *     audio_play_music(6, true) — track 6 = DS:0x0A6A = LOADER (loop),
 *     соответствует cs:0x40E (см. audio.cpp).
 *
 * ─────────────────────────────────────────────────────────────────────
 * ИНТЕРПРЕТАЦИЯ ФОРМАТА LEVEL%d.BIN (восстановлена дизассемблированием
 * load_level и draw_tile, исправляет ошибочные имена полей в FORMATS.md):
 *
 *   uint16  num_objects
 *   ObjectRegion[num_objects]
 *   uint16  num_enemies
 *   EnemySpawn[num_enemies]
 *   uint16  num_tile_graphics   ; кол-во уникальных тайл-картинок (144 в LEVEL0)
 *   uint16  num_map_rows        ; высота карты в тайлах (12)
 *   uint16  num_map_cols        ; ширина карты в тайлах (20, 1 байт/ячейка)
 *
 *   for graphic_idx in 1..num_tile_graphics:
 *       uint8   tile_idx        ; (1..N)
 *       uint8   attr            ; атрибут (палитра)
 *       uint8   pixels[40]      ; 16×10 пикселей @ 2bpp packed
 *       uint8   row_extra[80]   ; в mode 0/2 — на стек, discarded
 *
 *   uint8  map[num_map_rows][num_map_cols]   ; map cell = tile_idx 0..143
 *
 * Тайл: 4 байта × 10 строк = 40 байт, packed 2bpp, старшие биты слева.
 *
 * ─────────────────────────────────────────────────────────────────────
 * LEVEL%d.MOV формат:
 *   uint16  start_record_idx     ; всегда 0
 *   uint16  num_records
 *   packed { uint8 state; uint16 frame_le } records[num_records]
 *
 * Биты state: 0x01=R 0x02=L 0x04=D 0x08=U 0x10=FIRE 0x20=OPTION
 */
#include "splash.h"
#include "audio.h"
#include "sprites.h"
#include "karnov.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <SDL2/SDL.h>

namespace karnov {
namespace {

constexpr int    RLE_ESCAPE     = 0x5A;
constexpr size_t IO_BUF_SIZE    = 0x200;
constexpr int    TILE_GFX_BYTES = 40;
constexpr int    TILE_W         = 16;
constexpr int    TILE_H         = 10;
constexpr int    MAX_TILE_GFX   = 512;
constexpr int    MAX_MAP_ROWS   = 64;
constexpr int    MAX_MAP_COLS   = 64;
constexpr int    HUD_BAR_H      = 24;

/* ─── RLE-декомпрессор: DS:0xD566..0xD76D ─────────────────────────── */
u8   io_buffer[IO_BUF_SIZE];
u16  io_buf_avail = IO_BUF_SIZE;
u16  io_buf_pos   = IO_BUF_SIZE;
u16  rle_in_run   = 0;
u8   rle_run_byte = 0;
u8   rle_run_left = 0;

int read_byte(u8* dst, FILE* fp)
{
    if (io_buf_pos >= io_buf_avail) {
        if (io_buf_avail != IO_BUF_SIZE) return 1;
        io_buf_pos   = 0;
        io_buf_avail = static_cast<u16>(std::fread(io_buffer, 1, IO_BUF_SIZE, fp));
        if (io_buf_avail == 0) return 1;
    }
    *dst = io_buffer[io_buf_pos++];
    return 0;
}

FILE* bin_fopen(const char* path)
{
    FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        char buf[512]; size_t n = std::strlen(path);
        if (n < sizeof(buf)) {
            for (size_t i = 0; i < n; ++i) buf[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(path[i])));
            buf[n] = 0;
            fp = std::fopen(buf, "rb");
            if (!fp) {
                for (size_t i = 0; i < n; ++i) buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(path[i])));
                buf[n] = 0;
                fp = std::fopen(buf, "rb");
            }
        }
    }
    if (!fp) return nullptr;
    rle_in_run = 0; io_buf_pos = IO_BUF_SIZE; io_buf_avail = IO_BUF_SIZE;
    return fp;
}

size_t bin_fread(void* buf, size_t size, size_t count, FILE* fp)
{
    /* cs:0x58B:0x77, file 0x7127 — bin_fread с прозрачной 0x5A RLE. */
    u8* p = static_cast<u8*>(buf);
    size_t items_done = 0;
    for (size_t i = 0; i < count; ++i) {
        for (size_t b = 0; b < size; ++b) {
            if (rle_in_run) {
                --rle_run_left;
                if (rle_run_left == 0) rle_in_run = 0;
                *p = rle_run_byte;
            } else {
                if (read_byte(p, fp) != 0) return items_done;
                if (*p == RLE_ESCAPE) {
                    if (read_byte(p, fp) != 0) return items_done;
                    if (*p != RLE_ESCAPE) {
                        rle_run_left = static_cast<u8>(*p - 1);
                        rle_in_run   = 1;
                        if (read_byte(p, fp) != 0) return items_done;
                        rle_run_byte = *p;
                    }
                }
            }
            ++p;
        }
        ++items_done;
    }
    return items_done;
}

/* ─── DS-глобалы оригинала ────────────────────────────────────────── */
struct EnemySpawnLocal {                  /* file struct (20 байт) */
    u16 type, subtype;
    u16 cur_x, cur_y;
    u16 range_x, range_y;
    u16 spawn_x, spawn_y;
    u16 trigger, flags;
};

ObjectRegion    level_objects_buf[16];
EnemySpawnLocal level_enemies_buf[256];
u16 num_level_objects = 0;
u16 num_level_enemies = 0;

u8  tile_gfx [MAX_TILE_GFX][TILE_GFX_BYTES];
u8  tile_attr[MAX_TILE_GFX];
u16 num_tile_graphics = 0;
u16 num_map_rows_     = 0;
u16 num_map_cols_     = 0;
u8  map_data[MAX_MAP_ROWS][MAX_MAP_COLS];

/* ─── file 0x51B6 — read_object_regions ───────────────────────────── */
int read_object_regions(FILE* fp)
{
    if (bin_fread(&num_level_objects, 2, 1, fp) != 1) return 1;
    if (num_level_objects == 0) return 0;
    if (bin_fread(level_objects_buf, sizeof(ObjectRegion),
                  num_level_objects, fp) != num_level_objects) return 1;
    return 0;
}

/* ─── file 0x5216 — read_enemies ──────────────────────────────────── */
int read_enemies(FILE* fp)
{
    if (bin_fread(&num_level_enemies, 2, 1, fp) != 1) return 1;
    if (num_level_enemies == 0) return 0;
    if (bin_fread(level_enemies_buf, sizeof(EnemySpawnLocal),
                  num_level_enemies, fp) != num_level_enemies) return 1;
    return 0;
}

/* ─── file 0x5350 — load_level ────────────────────────────────────── */
int load_level_data(u16 level_num)
{
    char filename[16];
    std::snprintf(filename, sizeof(filename), "level%u.bin", level_num);
    FILE* fp = bin_fopen(filename);
    if (!fp) {
        std::fprintf(stderr, "load_level: не могу открыть %s\n", filename);
        return 1;
    }
    /* Оригинал переоткрывает файл для сброса RLE-state. */
    std::fclose(fp);
    fp = bin_fopen(filename);
    if (!fp) return 1;

    if (read_object_regions(fp)) { std::fclose(fp); return 0x0B; }
    if (read_enemies(fp))        { std::fclose(fp); return 0x15; }

    if (bin_fread(&num_tile_graphics, 2, 1, fp) != 1) { std::fclose(fp); return 0x1F; }
    if (bin_fread(&num_map_rows_,     2, 1, fp) != 1) { std::fclose(fp); return 0x29; }
    if (bin_fread(&num_map_cols_,     2, 1, fp) != 1) { std::fclose(fp); return 0x33; }
    if (num_tile_graphics > MAX_TILE_GFX) num_tile_graphics = MAX_TILE_GFX;
    if (num_map_rows_ > MAX_MAP_ROWS) num_map_rows_ = MAX_MAP_ROWS;
    if (num_map_cols_ > MAX_MAP_COLS) num_map_cols_ = MAX_MAP_COLS;

    u8 stack_extra[80];                          /* [bp-0x5C] в оригинале */
    for (u16 i = 0; i < num_tile_graphics; ++i) {
        u8 tile_idx = 0, attr = 0;
        if (bin_fread(&tile_idx, 1, 1, fp) != 1) { std::fclose(fp); return 0x3D; }
        if (bin_fread(&attr,     1, 1, fp) != 1) { std::fclose(fp); return 0x47; }
        tile_attr[tile_idx] = attr;
        if (bin_fread(tile_gfx[tile_idx], TILE_GFX_BYTES, 1, fp) != 1) {
            std::fclose(fp); return 0x51;
        }
        bin_fread(stack_extra, 80, 1, fp);       /* discarded в mode 0/2 */
    }
    /* Карта одним блоком: num_map_rows × num_map_cols байт. */
    size_t mapsz = static_cast<size_t>(num_map_rows_) * num_map_cols_;
    u8 flat[MAX_MAP_ROWS * MAX_MAP_COLS];
    if (bin_fread(flat, mapsz, 1, fp) != 1) { std::fclose(fp); return 0x65; }
    for (u16 r = 0; r < num_map_rows_; ++r)
        std::memcpy(map_data[r], flat + r * num_map_cols_, num_map_cols_);

    std::fclose(fp);
    std::memset(tile_gfx[0], 0, TILE_GFX_BYTES);  /* file 0x57C5: zero-fill */
    return 0;
}

/* ─── LEVEL%d.MOV ─────────────────────────────────────────────────── */
struct MovRec { u8 state; u16 frame; };
MovRec mov_records[1024];
u16    mov_num_records = 0;
u16    mov_curr_idx    = 0;
u8     input_state_    = 0;       /* DS:0xCC9E */

int load_demo_data(u16 level_num)
{
    /* cs:0x6E16 — load_demo. Открывает level%d.mov БЕЗ RLE. */
    char path[16];
    std::snprintf(path, sizeof(path), "level%u.mov", level_num);
    FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        char up[16]; size_t n = std::strlen(path);
        for (size_t i=0; i<n; ++i) up[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(path[i])));
        up[n] = 0;
        fp = std::fopen(up, "rb");
    }
    if (!fp) return 1;
    u16 hdr[2] = {0,0};
    if (std::fread(hdr, 2, 2, fp) != 2) { std::fclose(fp); return 2; }
    mov_curr_idx    = hdr[0];
    mov_num_records = hdr[1];
    if (mov_num_records > sizeof(mov_records)/sizeof(mov_records[0]))
        mov_num_records = sizeof(mov_records)/sizeof(mov_records[0]);
    for (u16 i = 0; i < mov_num_records; ++i) {
        u8 raw[3];
        if (std::fread(raw, 1, 3, fp) != 3) { mov_num_records = i; break; }
        mov_records[i].state = raw[0];
        mov_records[i].frame = static_cast<u16>(raw[1]) | (static_cast<u16>(raw[2]) << 8);
    }
    std::fclose(fp);
    return 0;
}

/* cs:0x6DC0 — tick_demo: при frame >= rec.frame применяет state. */
bool tick_demo(u16 current_frame)
{
    if (mov_curr_idx >= mov_num_records) return true;
    const MovRec* r = &mov_records[mov_curr_idx];
    if (r->frame > current_frame) return false;
    input_state_ = r->state;
    ++mov_curr_idx;
    return false;
}

/* ─── EGA / CGA палитры ───────────────────────────────────────────── */
/* IBM CGA palette 1 high-intensity — для совместимости. */
const uint32_t CGA_PAL_1H[4] = {
    0xFF000000u, 0xFF55FFFFu, 0xFFFF55FFu, 0xFFFFFFFFu,
};

/* EGA-палитра: одна базовая (16 цветов 6-bit RGB → 24-bit). */
const uint32_t EGA_PAL16[16] = {
    0xFF000000u, 0xFF0000AAu, 0xFF00AA00u, 0xFF00AAAAu,
    0xFFAA0000u, 0xFFAA00AAu, 0xFFAA5500u, 0xFFAAAAAAu,
    0xFF555555u, 0xFF5555FFu, 0xFF55FF55u, 0xFF55FFFFu,
    0xFFFF5555u, 0xFFFF55FFu, 0xFFFFFF55u, 0xFFFFFFFFu,
};

/* Под-палитры по 4 цвета: индексируются по tile_attr (0 или 2).
 * В оригинале attr используется как индекс в палитру EGA; точная
 * раскладка не задокументирована, реконструирована по скриншотам
 * Karnov EGA: attr=0 — драконы/горы (бирюза), attr=2 — буквы KARNOV
 * и декор внизу (тёплая красно-розовая гамма). */
/* Палитры из реальной игры (DOSBox-скриншот titlescreen, см.
 * /tmp/karnov_dos/capture/before_debug.png):
 *   • драконы по бокам — светло-серые с белыми бликами
 *   • буквы KARNOV   — жёлтые с тёмной обводкой
 *   • нижняя «верёвка» — красно-жёлтые акценты
 * Тот факт, что и драконы и буквы используют attr=2 в LEVEL0.BIN,
 * означает, что оригинальный движок переключает EGA Attribute Controller
 * палитру между раштер-линиями. В нашей реконструкции — выбираем
 * компромиссную палитру, дающую узнаваемый облик. */
const u8 EGA_SUBPAL_ATTR0[4] = { 0x0, 0x4, 0xC, 0xE };  /* black, dark-red, l-red, yellow */
const u8 EGA_SUBPAL_ATTR2[4] = { 0x0, 0x7, 0xE, 0xF };  /* black, gray, yellow, white  */

inline uint32_t resolve_color(u8 attr, u8 idx2)
{
    const u8* sub = (attr == 0) ? EGA_SUBPAL_ATTR0 : EGA_SUBPAL_ATTR2;
    return EGA_PAL16[sub[idx2 & 3]];
}

/* ─── Рендеринг ───────────────────────────────────────────────────── */
constexpr int LEVEL_W = 20 * TILE_W;   /* 320 */
constexpr int LEVEL_H = 12 * TILE_H;   /* 120 */
constexpr int VIEW_W  = LEVEL_W;
constexpr int VIEW_H  = LEVEL_H + HUD_BAR_H;

void blit_tile(uint32_t* fb, int pitch, int dst_x, int dst_y,
               const u8* gfx, u8 attr, SplashVideoMode vm)
{
    /* draw_tile (file 0x4076): 4 байта × 10 строк, packed 2bpp.
     * Старшие биты = левый пиксель. */
    for (int row = 0; row < TILE_H; ++row) {
        const u8* src = gfx + row * 4;
        uint32_t* d = fb + (dst_y + row) * pitch + dst_x;
        for (int b = 0; b < 4; ++b) {
            u8 byte = src[b];
            u8 p0 = (byte >> 6) & 3, p1 = (byte >> 4) & 3;
            u8 p2 = (byte >> 2) & 3, p3 = (byte >> 0) & 3;
            uint32_t c0, c1, c2, c3;
            if (vm == SPLASH_CGA) {
                c0 = CGA_PAL_1H[p0]; c1 = CGA_PAL_1H[p1];
                c2 = CGA_PAL_1H[p2]; c3 = CGA_PAL_1H[p3];
            } else {
                c0 = resolve_color(attr, p0); c1 = resolve_color(attr, p1);
                c2 = resolve_color(attr, p2); c3 = resolve_color(attr, p3);
            }
            d[0]=c0; d[1]=c1; d[2]=c2; d[3]=c3;
            d += 4;
        }
    }
}

void render_level(uint32_t* fb, SplashVideoMode vm)
{
    for (u16 r = 0; r < num_map_rows_; ++r) {
        for (u16 c = 0; c < num_map_cols_; ++c) {
            u8 id = map_data[r][c];
            blit_tile(fb, VIEW_W, c * TILE_W, r * TILE_H,
                      tile_gfx[id], tile_attr[id], vm);
        }
    }
}

/* ─── Спрайт-плейсхолдеры (без SPRITES.EGA, для визуальной отладки) ─
 *
 * Реальный рендер SPRITES.EGA включает 81-way dispatch в inner_blit
 * (file 0x0BCA8) и формат с заголовком (count, width) на спрайт. Это
 * вынесено в отдельную ветку работы. Здесь — простые плейсхолдеры,
 * чтобы показать что эмулятор демо ТИКАЕТ и Karnov ходит. */

struct DrawnEnemy { u16 type; int x; int y; int anim_phase; };
DrawnEnemy g_enemies[8] = {};
int        g_num_enemies = 0;

void init_enemies_from_level()
{
    g_num_enemies = 0;
    for (int i = 0; i < num_level_enemies && g_num_enemies < 8; ++i) {
        const auto& e = level_enemies_buf[i];
        if (e.type == 0 || e.cur_x >= 320 || e.cur_y >= 120) continue;  /* off-screen или маркер */
        g_enemies[g_num_enemies++] = { e.type, static_cast<int>(e.cur_x),
                                       static_cast<int>(e.cur_y), 0 };
    }
}

/* Forward declarations (draw_box is defined later). */
void draw_box(uint32_t* fb, int x, int y, int w, int h, uint32_t color);

/* ─── Зелёный враг — извлечён из реального title-screen (DOSBox capture)
 *
 * Это аналог Karnov-плейсхолдера, но для зелёного троля/гнома, видимого
 * справа на оригинальной заставке. Размер ~16×24, цвета EGA palette.   */
void draw_green_enemy(uint32_t* fb, int cx, int cy, u16 frame)
{
    const uint32_t SKIN  = EGA_PAL16[0xA];  /* светло-зелёный */
    const uint32_t BODY  = EGA_PAL16[0x2];  /* тёмно-зелёный  */
    const uint32_t HORN  = EGA_PAL16[0x6];  /* коричневый рог */
    const uint32_t EYE   = EGA_PAL16[0xF];  /* белые глаза    */
    const uint32_t MOUTH = EGA_PAL16[0x4];  /* красный рот    */
    int  bob       = (frame >> 4) & 1;       /* медленное дыхание */
    /* Моргает каждые ~64 кадра в течение 4 кадров. */
    bool blinking  = ((frame >> 2) % 16) == 0;

    int x0 = cx - 7, y0 = cy - 22 + bob;
    /* Рога */
    draw_box(fb, x0+2,  y0,    1, 2, HORN);
    draw_box(fb, x0+12, y0,    1, 2, HORN);
    /* Голова — светло-зелёная */
    draw_box(fb, x0+3,  y0+1,  9, 7, SKIN);
    draw_box(fb, x0+2,  y0+2,  1, 5, BODY);
    draw_box(fb, x0+12, y0+2,  1, 5, BODY);

    if (blinking) {
        /* Закрытые глаза — горизонтальные тёмные линии */
        draw_box(fb, x0+5,  y0+5,  2, 1, EGA_PAL16[0x0]);
        draw_box(fb, x0+8,  y0+5,  2, 1, EGA_PAL16[0x0]);
    } else {
        /* Открытые глаза */
        draw_box(fb, x0+5,  y0+4,  2, 2, EYE);
        draw_box(fb, x0+8,  y0+4,  2, 2, EYE);
        fb[(y0+5)*VIEW_W + x0+6] = EGA_PAL16[0x0];
        fb[(y0+5)*VIEW_W + x0+9] = EGA_PAL16[0x0];
    }
    /* Рот */
    draw_box(fb, x0+5,  y0+7,  5, 1, MOUTH);
    /* Тело */
    draw_box(fb, x0+2,  y0+8,  11, 6, BODY);
    draw_box(fb, x0+4,  y0+9,  7,  4, SKIN);    /* живот */
    /* Руки */
    draw_box(fb, x0,    y0+9,  2,  4, BODY);
    draw_box(fb, x0+13, y0+9,  2,  4, BODY);
    /* Ноги */
    draw_box(fb, x0+3,  y0+14, 4,  6, BODY);
    draw_box(fb, x0+8,  y0+14, 4,  6, BODY);
    /* Ступни */
    draw_box(fb, x0+2,  y0+20, 5,  2, HORN);
    draw_box(fb, x0+8,  y0+20, 5,  2, HORN);
}

/* Forward — реальный спрайт #39 из SPRITES.EGA. */
static DecodedSprite g_ladder_sprite;
static bool          g_ladder_loaded = false;

/* ─── Лестница из реального спрайта SPRITES.EGA #39 (16×12 4bpp EGA).
 * Дизассемблированием sprite_blit (cs:0xAA02) подтверждено: сначала
 * рисуется sprite_id 39 в позиции (16, 180) на title screen — это и есть
 * сегмент лестницы. На VIEW мы тайлим его вертикально между cy_top и
 * cy_bot. Спрайт нарисован side-by-side: 16 пикселей в ширину,
 * 12 пикселей в высоту, 4 цвета EGA per pixel. */
void draw_ladder(uint32_t* fb, int cx, int cy_top, int cy_bot)
{
    if (!g_ladder_sprite.valid) {
        /* Fallback к примитиву если спрайт не загружен. */
        const uint32_t RED  = EGA_PAL16[0x4];
        const uint32_t YELL = EGA_PAL16[0xE];
        for (int y = cy_top; y < cy_bot && y < VIEW_H; ++y) {
            if (cx - 3 >= 0)        fb[y * VIEW_W + (cx - 3)] = RED;
            if (cx + 3 < VIEW_W)    fb[y * VIEW_W + (cx + 3)] = RED;
        }
        for (int y = cy_top + 2; y < cy_bot; y += 4) {
            if (y < VIEW_H)
                draw_box(fb, cx - 2, y, 5, 1, YELL);
        }
        return;
    }
    const DecodedSprite& s = g_ladder_sprite;
    int x0 = cx - s.w / 2;
    for (int yy = cy_top; yy < cy_bot && yy < VIEW_H; ++yy) {
        int sr = (yy - cy_top) % s.h;
        for (int x = 0; x < s.w; ++x) {
            int v = s.px[sr * s.w + x];
            if (v < 0) continue;
            int xx = x0 + x;
            if (xx < 0 || xx >= VIEW_W) continue;
            fb[yy * VIEW_W + xx] = EGA_PAL16[v & 15];
        }
    }
}

/* Раскрасить рамку 8×6 поверх существующих пикселей. */
void draw_box(uint32_t* fb, int x, int y, int w, int h, uint32_t color)
{
    for (int dy = 0; dy < h; ++dy) {
        int yy = y + dy; if (yy < 0 || yy >= VIEW_H) continue;
        for (int dx = 0; dx < w; ++dx) {
            int xx = x + dx; if (xx < 0 || xx >= VIEW_W) continue;
            fb[yy * VIEW_W + xx] = color;
        }
    }
}

/* Цвет по типу врага (художественно выбран — реальные цвета зависят
 * от палитры спрайта, не извлечённой из SPRITES.EGA). */
uint32_t enemy_color(u16 type)
{
    switch (type) {
        case 2:  return EGA_PAL16[0x8];     /* тёмно-серый — невидимый маркер  */
        case 6:  return EGA_PAL16[0xE];     /* жёлтый — птицы                  */
        case 13: return EGA_PAL16[0xA];     /* зелёный — наземный враг         */
        default: return EGA_PAL16[0xC];     /* красный — прочие                */
    }
}

/* Летящая «птица»-плейсхолдер 11×5 пикселей: M-образная фигура с двумя
 * фазами анимации (крылья вниз / вверх каждые 8 кадров). */
void draw_bird(uint32_t* fb, int cx, int cy, int phase, uint32_t color)
{
    auto put = [&](int dx, int dy) {
        int xx = cx + dx, yy = cy + dy;
        if (xx >= 0 && xx < VIEW_W && yy >= 0 && yy < VIEW_H)
            fb[yy * VIEW_W + xx] = color;
    };
    if ((phase & 1) == 0) {
        /* крылья вниз */
        put(-5, -1); put(-4, 0); put(-3, 1);
        put(-2, 1); put(-1, 0);  put( 0, 0);
        put( 1, 0); put( 2, 1);  put( 3, 1);
        put( 4, 0); put( 5, -1);
    } else {
        /* крылья вверх */
        put(-5, 1); put(-4, 0); put(-3, -1);
        put(-2, -1); put(-1, 0); put( 0, 0);
        put( 1, 0); put( 2, -1); put( 3, -1);
        put( 4, 0); put( 5, 1);
    }
}

/* Кэш декодированных Karnov-кадров из SPRITES.EGA (4bpp EGA).
 *
 * Karnov — это персонаж с ai_type=513 (RED strongman). Видя визуальный
 * атлас /tmp/karnov_pick_t513.png, он — крупный 32×26 персонаж в красном
 * костюме с белыми сапогами, синим поясом, розовыми бликами на коже.
 * (Атлас ai_type=512 содержит ЗЕЛЁНЫХ стражников с мечами — другие враги.)
 *
 * Из 18 спрайтов ai_type=513 визуально идентифицированы кадры:
 *   #100 (32×26) — стойка (руки по бокам)
 *   #108 (32×26) — шаг (одна нога вперёд)
 *   #109 (32×26) — шаг (вторая нога вперёд)
 *   #102 (32×26) — третий кадр шага
 *   #98  (32×26) — прыжок (руки приподняты)
 *   #104 (32×21) — присед / коленопреклонение
 *   #103 (32×22) — атака (огненное дыхание / удар вперёд)
 *   #112 (32×21) — упавший / повержен
 */
enum KarnovFrameSlot {
    K_STAND = 0,
    K_WALK1 = 1,
    K_WALK2 = 2,
    K_WALK3 = 3,
    K_JUMP  = 4,
    K_CROUCH = 5,
    K_FIRE  = 6,
    K_DEAD  = 7,
    K_FRAME_COUNT = 8,
};

static DecodedSprite g_karnov_decoded[K_FRAME_COUNT];
static int  g_karnov_frames[K_FRAME_COUNT];
static bool g_karnov_loaded = false;
static bool g_karnov_is_cga = false;   /* true если загружен CGA-кэш (bpp=2) */

/* Зелёный каменный человек (ai_type=512): 4-кадровая анимация. */
static DecodedSprite g_stoneman_decoded[4];
static int  g_stoneman_frames[4] = { -1, -1, -1, -1 };

void load_karnov_sprites()
{
    if (g_karnov_loaded) return;
    /* Сначала пробуем CGA-кэш — он содержит чистые силуэты Karnov без
     * проблем с прозрачностью (декодер использует mask=11 → transparent
     * в 2bpp chunky). EGA-кэш работает, но Karnov-кадры имеют ~87%
     * непрозрачных пикселей из-за нерешённого вопроса с mask-каналом
     * в runtime-дампе, и требуют color_key=1 workaround.
     *
     * sprite ID совпадают между EGA и CGA — sprite #76 в обоих файлах
     * относится к одному и тому же логическому кадру (но пиксельные
     * данные разные: EGA = 4bpp планарный, CGA = 2bpp chunky). */
    if (sprites_load_cache("data/karnov_sprites_cga.bin") == 0) {
        g_karnov_is_cga = true;
    } else if (sprites_load_cache("data/karnov_sprites_ega.bin") == 0) {
        g_karnov_is_cga = false;
    } else if (sprites_load_cache("data/karnov_sprites.bin") == 0) {
        g_karnov_is_cga = false;
    } else {
        std::fprintf(stderr, "sprites: ни CGA ни EGA кэш не найден — используется плейсхолдер\n");
        return;
    }

    /* Маппинг slot → sprite_id для ai_type=513 (Karnov, жёлтый качок
     * с красными шортами и ирокезом). Спрайты #76..#85 имеют ai_type=512
     * — это ЗЕЛЁНЫЕ СТРАЖНИКИ с мечами, не Karnov.
     *
     * Идентификация визуальная (см. /tmp/cga_atlas_ai0513.png):
     *   #98  — стойка
     *   #99..#103 — кадры ходьбы / атаки
     *   #104 — присед (низкий)
     *   #105 — бросок огненного шара
     *   #112 — поверженный (горизонтальная поза)
     *   #120..#122 — руки подняты вверх (прыжок/победа) */
    static const int kKarnov[K_FRAME_COUNT] = {
        98,  /* STAND  — основная стойка                         */
        99,  /* WALK1  — кадр шага 1                              */
        100, /* WALK2  — кадр шага 2                              */
        102, /* WALK3  — кадр шага 3                              */
        120, /* JUMP   — руки подняты (прыжок)                    */
        104, /* CROUCH — приседание                                */
        105, /* FIRE   — бросок огненного шара                    */
        112, /* DEAD   — поверженная поза                          */
    };
    for (int k = 0; k < K_FRAME_COUNT; ++k) {
        g_karnov_frames[k] = -1;
        int idx = sprites_find_by_id(kKarnov[k]);
        if (idx < 0) continue;
        sprites_decode(idx, g_karnov_decoded[k]);
        if (g_karnov_decoded[k].valid) g_karnov_frames[k] = idx;
    }
    /* Зелёный каменный человек (ai_type=512, sword-guard). В CGA = зелёный,
     * в EGA = красный (зависит от палитры). 4-кадровый цикл анимации:
     * стойка, ходьба × 2, атака мечом. */
    static const int kStoneMan[4] = {
        76, /* STAND   — стойка с мечом                       */
        77, /* WALK1   — шаг                                   */
        78, /* WALK2   — шаг                                   */
        79, /* ATTACK  — взмах мечом (расширенная поза)         */
    };
    for (int k = 0; k < 4; ++k) {
        g_stoneman_frames[k] = -1;
        int idx = sprites_find_by_id(kStoneMan[k]);
        if (idx < 0) continue;
        sprites_decode(idx, g_stoneman_decoded[k]);
        if (g_stoneman_decoded[k].valid) g_stoneman_frames[k] = idx;
    }
    /* Лестница — sprite #24 (16×20 4bpp EGA).
     *
     * Это РАЗВЁРНУТАЯ ЛЕСТНИЦА, а не ICON предмета. Sprite #39 (который
     * мы использовали раньше) — это значок предмета "Ladder" в инвентаре
     * (квадратная панель с лестницей внутри). Sprite #24 — это сам
     * графический ассет лестницы без обрамления: вертикальные перекладины
     * + горизонтальные ступени, тайлится вертикально для нужной высоты. */
    int ladder_idx = sprites_find_by_id(24);
    if (ladder_idx >= 0) {
        sprites_decode(ladder_idx, g_ladder_sprite);
        if (g_ladder_sprite.valid) g_ladder_loaded = true;
    }
    int loaded = 0;
    for (int k = 0; k < K_FRAME_COUNT; ++k) if (g_karnov_frames[k] >= 0) ++loaded;
    std::printf("sprites: Karnov frames loaded=%d/%d, ladder=%s\n",
                loaded, K_FRAME_COUNT, g_ladder_loaded ? "ok" : "MISSING");
    g_karnov_loaded = true;
}

/* Рисует декодированный спрайт (силуэт) в кадровый буфер с заданными
 * цветами (color_outline для bit=1, color_fill для тёмных, transparent
 * для bit-1 в mask). Сохранена для будущего использования, когда формат
 * SPRITES.EGA будет полностью декодирован. */
[[maybe_unused]]
bool draw_decoded_sprite(uint32_t* fb, int cx, int cy, const DecodedSprite& s,
                         bool flip_x, uint32_t color_silhouette, uint32_t color_shadow)
{
    if (!s.valid) return false;
    int x0 = cx - s.w/2;
    int y0 = cy - s.h;
    for (int r = 0; r < s.h; ++r) {
        for (int x = 0; x < s.w; ++x) {
            int sx = flip_x ? (s.w - 1 - x) : x;
            int v = s.px[r * s.w + sx];
            if (v < 0) continue;  /* прозрачный */
            int xx = x0 + x, yy = y0 + r;
            if (xx < 0 || xx >= VIEW_W || yy < 0 || yy >= VIEW_H) continue;
            fb[yy * VIEW_W + xx] = (v == 0) ? color_shadow : color_silhouette;
        }
    }
    return true;
}

/* Рисует декодированный 4bpp EGA-спрайт.
 *
 * Якорь: x0 = cx - anchor_x, y0 = cy - s.h (пиксель (anchor_x, s.h-1)
 * совпадает с (cx, cy) — то есть anchor_x/cy — это «нижний центр».
 * Если anchor_x < 0, используется s.w/2 (геометрический центр спрайта).
 *
 * Для Karnov-кадров t=513 фигура занимает рамку x ∈ [0..28-29] из 32
 * пикселей ширины (bbox центрирован на колонке 14). Передаём anchor_x=14
 * чтобы видимый центр персонажа оказался под cx (а не у левого края). */
static bool draw_sprite_ega(uint32_t* fb, int cx, int cy,
                            const DecodedSprite& s, bool flip_x,
                            int anchor_x = -1,
                            int color_key = -1)
{
    if (!s.valid) return false;
    int ax = (anchor_x >= 0 && anchor_x < s.w) ? anchor_x : s.w / 2;
    int x0 = cx - (flip_x ? (s.w - 1 - ax) : ax);
    int y0 = cy - s.h;
    /* bpp=2 → CGA-палитра (4 цвета), bpp=4 → EGA-палитра (16 цветов). */
    const bool is_cga = (s.bpp == 2);
    for (int r = 0; r < s.h; ++r) {
        for (int x = 0; x < s.w; ++x) {
            int sx = flip_x ? (s.w - 1 - x) : x;
            int v = s.px[r * s.w + sx];
            if (v < 0) continue;
            /* color_key: дополнительная прозрачность по индексу палитры.
             * Для Карнов-кадров фон спрайта закодирован как color 1 (синий)
             * без mask=1 (mask декодера читает не тот байт-канал, но это
             * не критично для splash-экрана — chroma-key даёт правильный
             * визуал). */
            if (color_key >= 0 && (v & 15) == color_key) continue;
            int xx = x0 + x, yy = y0 + r;
            if (xx < 0 || xx >= VIEW_W || yy < 0 || yy >= VIEW_H) continue;
            fb[yy * VIEW_W + xx] = is_cga ? CGA_PAL_1H[v & 3] : EGA_PAL16[v & 15];
        }
    }
    return true;
}

/* Рисует «зелёного каменного человека» (ai_type=512). 4-кадровый цикл:
 * stand → walk1 → walk2 → attack → … . Кадр выбирается по (frame >> 3) & 3. */
void draw_stone_man(uint32_t* fb, int cx, int cy, u16 frame, bool facing_left)
{
    int slot = (frame >> 3) & 3;
    if (g_stoneman_frames[slot] < 0) return;
    const DecodedSprite& s = g_stoneman_decoded[slot];
    /* В EGA фон закодирован как color 1 (синий) — отсекаем chroma-key.
     * В CGA mask декодера работает корректно, фон уже прозрачный. */
    const int color_key = g_karnov_is_cga ? -1 : 1;
    draw_sprite_ega(fb, cx, cy, s, facing_left, /*anchor_x=*/-1, color_key);
}

void draw_karnov(uint32_t* fb, int cx, int cy, u8 input, u16 frame)
{
    /* Анимация ходьбы — цикл из 3 кадров шага, переключается по (frame>>2)%3.
     * Плюс особые позы для UP (прыжок), DOWN (присед), FIRE (атака). */
    bool facing_left = (input & 0x02) != 0;
    bool walking     = (input & 0x03) != 0;
    bool jumping     = (input & 0x08) != 0;
    bool crouching   = (input & 0x04) != 0;
    bool firing      = (input & 0x10) != 0;

    /* Реальные спрайты SPRITES.EGA #76..#85 (ai_type=512 = Karnov normal). */
    if (g_karnov_loaded) {
        int slot;
        if      (firing)    slot = K_FIRE;
        else if (jumping)   slot = K_JUMP;
        else if (crouching) slot = K_CROUCH;
        else if (walking) {
            /* 3-кадровый цикл: WALK1, WALK2, WALK3 */
            static const int cycle[3] = { K_WALK1, K_WALK2, K_WALK3 };
            slot = cycle[(frame >> 2) % 3];
        }
        else slot = K_STAND;

        if (g_karnov_frames[slot] >= 0) {
            const DecodedSprite& s = g_karnov_decoded[slot];
            /* Якорь — геометрический центр спрайта (s.w/2):
             *   CGA: спрайт 24×N целиком — фигура персонажа, центр на col 12.
             *   EGA: спрайт 32×N с фигурой в strip 0 (cols 0..15), центр col 8.
             * Передаём -1 чтобы draw_sprite_ega использовал s.w/2 для CGA,
             * либо явный anchor=8 для EGA (тело в левом strip). */
            const int kKarnovAnchorX = g_karnov_is_cga ? -1 : 8;
            /* color_key=1 только для EGA (синий фон). В CGA mask работает
             * корректно — фон уже прозрачный (v=-1). */
            const int kColorKey = g_karnov_is_cga ? -1 : 1;
            if (s.valid && draw_sprite_ega(fb, cx, cy, s, facing_left,
                                            kKarnovAnchorX, kColorKey)) {
                if (input & 0x20) {
                    /* USE-индикатор над головой */
                    draw_box(fb, cx-3, cy - s.h - 5, 6, 4, EGA_PAL16[0xE]);
                    draw_box(fb, cx-2, cy - s.h - 4, 4, 2, EGA_PAL16[0xC]);
                }
                return;
            }
        }
    }

    /* ─── FALLBACK plate (если кэш спрайтов недоступен). ───────────── */
    int  walk_phase  = walking ? ((frame >> 2) & 3) : 0;
    int  leg_offset[4] = {0, 1, 0, -1};
    int  body_dy     = jumping ? -2 : (crouching ? 2 : 0);

    /* Цвета Карнова из реальных скриншотов KARNOV.EXE EGA. */
    const uint32_t SKIN     = EGA_PAL16[0xC];  /* розово-красный  */
    const uint32_t SKIN_HI  = EGA_PAL16[0xE];  /* светлая блик    */
    const uint32_t HAIR     = EGA_PAL16[0x0];  /* чёрный мохавк   */
    const uint32_t BODY     = EGA_PAL16[0x4];  /* тёмно-красное   */
    const uint32_t SHADOW   = EGA_PAL16[0x4];  /* тёмно-красный   */
    const uint32_t SHORTS   = EGA_PAL16[0x4];  /* красные шорты   */
    const uint32_t BELT     = EGA_PAL16[0x6];  /* коричневый пояс */
    const uint32_t BOOT     = EGA_PAL16[0x6];  /* коричневые сапоги */
    const uint32_t SWEAT    = EGA_PAL16[0xE];  /* жёлтая искра    */

    int x0 = cx - 6, y0 = cy - 23 + body_dy;
    int dl = leg_offset[walk_phase];
    int dr = -dl;

    /* Мохавк/ирокез — небольшой треугольный гребень */
    draw_box(fb, x0+5, y0,    3, 1, HAIR);
    draw_box(fb, x0+4, y0+1,  5, 1, HAIR);

    /* Лицо */
    draw_box(fb, x0+3, y0+2,  7, 4, SKIN);
    /* Линия рта/нос */
    fb[(y0+4) * VIEW_W + (x0 + (facing_left ? 4 : 8))] = HAIR; /* глаз */
    draw_box(fb, x0+4, y0+5,  5, 1, BODY);                    /* рот  */

    /* Шея + плечи */
    draw_box(fb, x0+4, y0+6,  5, 1, SKIN);
    draw_box(fb, x0+1, y0+7,  11, 1, BODY);

    /* Голый красный торс (3 строки мускулов) */
    draw_box(fb, x0+1, y0+8,  11, 5, BODY);
    /* Светлые блики — грудные мышцы */
    draw_box(fb, x0+3, y0+9,  2, 2, SKIN_HI);
    draw_box(fb, x0+8, y0+9,  2, 2, SKIN_HI);
    /* Линия пупка */
    fb[(y0+12) * VIEW_W + (x0+6)] = SHADOW;

    /* Руки — двигаются противофазно с ногами */
    int arm_left_dy  = (walk_phase == 1) ? -1 : (walk_phase == 3 ?  1 : 0);
    int arm_right_dy = -arm_left_dy;
    if (facing_left) { int t = arm_left_dy; arm_left_dy = arm_right_dy; arm_right_dy = t; }
    /* Голые руки = красные */
    draw_box(fb, x0,    y0+8 + arm_left_dy,  2, 5, BODY);
    draw_box(fb, x0+11, y0+8 + arm_right_dy, 2, 5, BODY);
    /* Кулаки (skin) */
    draw_box(fb, x0,    y0+12 + arm_left_dy, 2, 2, SKIN);
    draw_box(fb, x0+11, y0+12 + arm_right_dy, 2, 2, SKIN);

    /* Пояс */
    draw_box(fb, x0+1, y0+13, 11, 2, BELT);

    /* Шорты (1-2 строки) + ноги */
    draw_box(fb, x0+2, y0+15, 9, 2, SHORTS);

    if (crouching) {
        /* Присед — короткие ноги */
        draw_box(fb, x0+2, y0+17, 4, 3, SKIN);
        draw_box(fb, x0+7, y0+17, 4, 3, SKIN);
        draw_box(fb, x0+2, y0+20, 4, 2, BOOT);
        draw_box(fb, x0+7, y0+20, 4, 2, BOOT);
    } else if (jumping) {
        /* Прыжок — обе ноги поджаты вперёд */
        draw_box(fb, x0+3, y0+17, 4, 4, SKIN);
        draw_box(fb, x0+7, y0+17, 4, 4, SKIN);
        draw_box(fb, x0+3, y0+21, 4, 2, BOOT);
        draw_box(fb, x0+7, y0+21, 4, 2, BOOT);
    } else {
        /* Шаг — ноги расходятся по фазе walk_phase */
        draw_box(fb, x0+2+dl, y0+17, 4, 5, SKIN);
        draw_box(fb, x0+7+dr, y0+17, 4, 5, SKIN);
        /* Сапоги */
        draw_box(fb, x0+2+dl, y0+21, 4, 2, BOOT);
        draw_box(fb, x0+7+dr, y0+21, 4, 2, BOOT);
    }

    /* Кулак FIRE — мускулистая рука выбрасывается */
    if (input & 0x10) {
        int fx = facing_left ? x0-5 : x0+13;
        draw_box(fb, fx, y0+8,  5, 4, BODY);
        draw_box(fb, fx+(facing_left?0:3), y0+9, 2, 2, SKIN);
        /* Жёлто-красная искра огня */
        int sx = facing_left ? fx-3 : fx+5;
        if (sx >= 0 && sx < VIEW_W) {
            fb[(y0+9)*VIEW_W + sx]  = EGA_PAL16[0xE];
            fb[(y0+10)*VIEW_W + sx] = EGA_PAL16[0xC];
            fb[(y0+11)*VIEW_W + sx] = EGA_PAL16[0x4];
        }
    }
    /* USE (бомба/опция) — над головой */
    if (input & 0x20) {
        draw_box(fb, x0+4, y0-5, 6, 4, EGA_PAL16[0xE]);
        draw_box(fb, x0+5, y0-4, 4, 2, EGA_PAL16[0xC]);
    }

    /* Искра пота при беге */
    if (walking && walk_phase == 2) {
        int sx = facing_left ? cx + 8 : cx - 9;
        if (sx >= 0 && sx < VIEW_W)
            fb[(y0+6) * VIEW_W + sx] = SWEAT;
    }
}

/* ─── HUD ─────────────────────────────────────────────────────────── */
/* 5×7 шрифт (только используемые символы). */
const u8 FONT5x7[] = {
 /*0*/0x0E,0x11,0x13,0x15,0x19,0x11,0x0E, /*1*/0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,
 /*2*/0x0E,0x11,0x01,0x06,0x08,0x10,0x1F, /*3*/0x0E,0x11,0x01,0x06,0x01,0x11,0x0E,
 /*4*/0x02,0x06,0x0A,0x12,0x1F,0x02,0x02, /*5*/0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,
 /*6*/0x06,0x08,0x10,0x1E,0x11,0x11,0x0E, /*7*/0x1F,0x01,0x02,0x04,0x08,0x08,0x08,
 /*8*/0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E, /*9*/0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,
 /*':'*/0x00,0x04,0x04,0x00,0x04,0x04,0x00,/*' '*/0,0,0,0,0,0,0,
 /*'A'*/0x0E,0x11,0x11,0x1F,0x11,0x11,0x11, /*'B'*/0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,
 /*'C'*/0x0E,0x11,0x10,0x10,0x10,0x11,0x0E, /*'D'*/0x1E,0x11,0x11,0x11,0x11,0x11,0x1E,
 /*'E'*/0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F, /*'F'*/0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,
 /*'G'*/0x0E,0x11,0x10,0x17,0x11,0x11,0x0E, /*'H'*/0x11,0x11,0x11,0x1F,0x11,0x11,0x11,
 /*'I'*/0x0E,0x04,0x04,0x04,0x04,0x04,0x0E, /*'J'*/0x07,0x02,0x02,0x02,0x02,0x12,0x0C,
 /*'K'*/0x11,0x12,0x14,0x18,0x14,0x12,0x11, /*'L'*/0x10,0x10,0x10,0x10,0x10,0x10,0x1F,
 /*'M'*/0x11,0x1B,0x15,0x11,0x11,0x11,0x11, /*'N'*/0x11,0x19,0x15,0x13,0x11,0x11,0x11,
 /*'O'*/0x0E,0x11,0x11,0x11,0x11,0x11,0x0E, /*'P'*/0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,
 /*'Q'*/0x0E,0x11,0x11,0x11,0x15,0x12,0x0D, /*'R'*/0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,
 /*'S'*/0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E, /*'T'*/0x1F,0x04,0x04,0x04,0x04,0x04,0x04,
 /*'U'*/0x11,0x11,0x11,0x11,0x11,0x11,0x0E, /*'V'*/0x11,0x11,0x11,0x11,0x11,0x0A,0x04,
 /*'W'*/0x11,0x11,0x11,0x15,0x15,0x15,0x0A, /*'X'*/0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,
 /*'Y'*/0x11,0x11,0x0A,0x04,0x04,0x04,0x04, /*'Z'*/0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,
 /*'/'*/0x01,0x01,0x02,0x04,0x08,0x10,0x10, /*'#'*/0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,
 /*'.'*/0x00,0x00,0x00,0x00,0x00,0x00,0x04, /*'-'*/0x00,0x00,0x00,0x0E,0x00,0x00,0x00,
};
const char* FONT_CHARS = "0123456789: ABCDEFGHIJKLMNOPQRSTUVWXYZ/#.-";
int find_char(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    for (int i = 0; FONT_CHARS[i]; ++i) if (FONT_CHARS[i] == c) return i;
    return 11; /* space */
}
void put_char(uint32_t* fb, int pitch, int x, int y, char c, uint32_t col)
{
    int idx = find_char(c);
    const u8* g = &FONT5x7[idx * 7];
    for (int r = 0; r < 7; ++r) {
        u8 row = g[r];
        for (int b = 0; b < 5; ++b)
            if (row & (1 << (4-b)))
                fb[(y+r) * pitch + (x+b)] = col;
    }
}
void put_text(uint32_t* fb, int pitch, int x, int y, const char* s, uint32_t col)
{
    for (; *s; ++s, x += 6) put_char(fb, pitch, x, y, *s, col);
}

void render_hud(uint32_t* fb, u16 frame, bool demo_done, int karnov_x)
{
    for (int y = LEVEL_H; y < VIEW_H; ++y)
        for (int x = 0; x < VIEW_W; ++x)
            fb[y * VIEW_W + x] = 0xFF202020u;

    char line[80];
    std::snprintf(line, sizeof(line), "FRAME:%u  REC:%u/%u  X:%d",
                  static_cast<unsigned>(frame),
                  static_cast<unsigned>(mov_curr_idx),
                  static_cast<unsigned>(mov_num_records),
                  karnov_x);
    put_text(fb, VIEW_W, 2, LEVEL_H + 2, line, 0xFFFFFFFFu);

    std::snprintf(line, sizeof(line), "INPUT:%02X %s%s%s%s%s%s%s",
                  input_state_,
                  (input_state_ & 0x01) ? "R":"-",
                  (input_state_ & 0x02) ? "L":"-",
                  (input_state_ & 0x04) ? "D":"-",
                  (input_state_ & 0x08) ? "U":"-",
                  (input_state_ & 0x10) ? "F":"-",
                  (input_state_ & 0x20) ? "B":"-",
                  demo_done ? "  DEMO END" : "");
    put_text(fb, VIEW_W, 2, LEVEL_H + 11, line, 0xFFFFFFFFu);
}

} /* namespace (anonymous) */

/* ─── Публичная точка входа ───────────────────────────────────────── */
int splash_run(const SplashOptions& opt)
{
    /* --- 1. Загрузка данных уровня. ----------------------------------- */
    int rc = load_level_data(opt.level_index);
    if (rc != 0) {
        std::fprintf(stderr, "load_level rc=0x%X\n", rc);
        return 1;
    }
    int demo_rc = load_demo_data(opt.level_index);
    if (demo_rc) {
        std::fprintf(stderr, "load_demo: LEVEL%u.MOV не найден (rc=%d), демо отключено\n",
                     opt.level_index, demo_rc);
    }
    init_enemies_from_level();
    load_karnov_sprites();    /* SPRITES.EGA → силуэты Karnov */

    std::printf("LEVEL%u: %u objects, %u enemies, %u tile_graphics, "
                "карта %u×%u, %u demo events\n",
                opt.level_index, num_level_objects, num_level_enemies,
                num_tile_graphics, num_map_rows_, num_map_cols_,
                mov_num_records);

    /* --- 2. Loader-music: track 6 = DS:0x0A6A на повтор. ------------- *
     * Запускаем СРАЗУ при первой загрузке уровня. Splash-окно открывается
     * только после того, как loader-music проиграл минимум один цикл
     * (≈14 сек @ 110 Гц для track 6). После этого переключаемся на
     * фоновую музыку игры (track 0 = DS:0x04CE) на повтор.
     *
     * В оригинале это управляется cs:0x40E (loader entry) + main loop в
     * cs:0xCB9:0x7D play_music(off, seg, loop). См. audio.cpp. */
    if (opt.with_audio) {
        audio_play_music(6, /*loop=*/true);
        std::printf("audio: track 6 (LOADER) starting...\n");
        std::fflush(stdout);
    }

    /* --- 2.5. PPM-режим: рендер одного кадра без окна. ---------------- */
    if (opt.ppm_path) {
        static uint32_t fb[VIEW_W * VIEW_H];
        /* Стартовая позиция: левее лестницы (которая на x=160), чтобы
         * персонаж был виден отдельно. */
        int karnov_x = 120, karnov_y = 105;
        bool demo_done = false;
        u16 cf = 0;
        while (cf < static_cast<u16>(opt.ppm_frame)) {
            if (tick_demo(cf)) { demo_done = true; break; }
            if (input_state_ & 0x01) karnov_x += 1;
            if (input_state_ & 0x02) karnov_x -= 1;
            if (input_state_ & 0x04) karnov_y += 1;
            if (input_state_ & 0x08) karnov_y -= 1;
            if (karnov_x < 8) karnov_x = 8;
            if (karnov_x > 312) karnov_x = 312;
            if (karnov_y < 75) karnov_y = 75;
            if (karnov_y > 115) karnov_y = 115;
            ++cf;
        }
        render_level(fb, opt.video_mode);
        for (int i = 0; i < g_num_enemies; ++i) {
            const auto& ge = g_enemies[i];
            if (ge.type == 6) draw_bird(fb, ge.x, ge.y, (cf>>3)&1, enemy_color(ge.type));
            else              draw_box (fb, ge.x-3, ge.y-3, 6, 6, enemy_color(ge.type));
        }
        draw_ladder(fb, 160, 60, 110);
        draw_green_enemy(fb, 260, 113, cf);
        /* Зелёный каменный человек (ai_type=512) — анимированно слева от Karnov. */
        draw_stone_man(fb, 60, 113, cf, /*facing_left=*/false);
        draw_karnov(fb, karnov_x, karnov_y, input_state_, cf);
        render_hud(fb, cf, demo_done, karnov_x);
        FILE* f = std::fopen(opt.ppm_path, "wb");
        if (!f) return 1;
        std::fprintf(f, "P6\n%d %d\n255\n", VIEW_W, VIEW_H);
        for (int i = 0; i < VIEW_W * VIEW_H; ++i) {
            u8 rgb[3] = {static_cast<u8>(fb[i] >> 16),
                         static_cast<u8>(fb[i] >> 8),
                         static_cast<u8>(fb[i])};
            std::fwrite(rgb, 3, 1, f);
        }
        std::fclose(f);
        std::printf("PPM записан в %s (frame=%u)\n", opt.ppm_path, cf);
        return 0;
    }

    /* --- 3. SDL2 окно. ----------------------------------------------- */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init VIDEO: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow(
        "Karnov — splash (attract demo)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VIEW_W * opt.win_scale, VIEW_H * opt.win_scale, SDL_WINDOW_SHOWN);
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, VIEW_W, VIEW_H);
    SDL_Texture* tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H);

    /* --- 3.5. Loading screen — пока loader-music не сыграл хотя бы один
     * цикл. После этого переключаемся на background music (track 0). */
    if (opt.with_audio) {
        static uint32_t loading_fb[VIEW_W * VIEW_H];
        std::printf("waiting for loader-music to complete one cycle (~14 s)...\n");
        std::fflush(stdout);
        Uint32 t_start = SDL_GetTicks();
        bool skipped = false;
        while (audio_cycle_count() < 1) {
            /* Слушаем события — ESC/SPACE/Q пропускает ожидание. */
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) return 0;
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                    case SDLK_SPACE: case SDLK_RETURN: skipped = true; break;
                    case SDLK_ESCAPE: case SDLK_q:   return 0;
                    default: break;
                    }
                }
            }
            if (skipped) break;

            /* Анимированная "LOADING…" заставка. */
            std::memset(loading_fb, 0, sizeof(loading_fb));
            Uint32 dt = SDL_GetTicks() - t_start;
            int dots = static_cast<int>((dt / 250) & 3);
            char line[64];
            std::snprintf(line, sizeof(line), "LOADING LEVEL %u%s",
                          static_cast<unsigned>(opt.level_index),
                          (dots==0)?"":(dots==1)?"."  :(dots==2)?".." :"...");
            put_text(loading_fb, VIEW_W, VIEW_W/2 - 60, VIEW_H/2 - 12, line, 0xFFFFFFFFu);
            std::snprintf(line, sizeof(line), "(LOADER MUSIC %u SEC / 14 SEC)",
                          static_cast<unsigned>(dt / 1000));
            put_text(loading_fb, VIEW_W, VIEW_W/2 - 90, VIEW_H/2 + 2, line, 0xFFAAAAAAu);
            put_text(loading_fb, VIEW_W, VIEW_W/2 - 75, VIEW_H/2 + 14,
                     "PRESS SPACE TO SKIP", 0xFF555555u);

            /* Прогресс-бар. */
            int bar_w = 200, bar_x = (VIEW_W - bar_w) / 2, bar_y = VIEW_H/2 + 28;
            int fill = static_cast<int>((static_cast<long>(dt) * bar_w) / 14000);
            if (fill > bar_w) fill = bar_w;
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < bar_w; ++x) {
                    loading_fb[(bar_y+y)*VIEW_W + bar_x+x] =
                        (x < fill) ? 0xFF55FFFFu : 0xFF333333u;
                }
            }

            SDL_UpdateTexture(tex, NULL, loading_fb, VIEW_W * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            SDL_Delay(16);
        }
        std::printf("loader cycle completed (%u). Switching to background music (track 0).\n",
                    audio_cycle_count());
        std::fflush(stdout);
        audio_play_music(0, /*loop=*/true);   /* DS:0x04CE — in-game background */
    }

    /* --- 4. Karnov-физика + AI врагов. -------------------------------- *
     *
     * Карнов в оригинале (cs:0xFFA:0x1A1A PRE-KARNOV + cs:0xFFA:0x1C04
     * POST-KARNOV, file 0x125BA/0x127A4):
     *   - горизонтальная скорость накапливается при удержании L/R
     *   - UP — прыжок (только если на земле)
     *   - DOWN — присед
     *   - гравитация: vy += g каждый кадр
     *   - столкновение с уровнем земли (ground_y = obj0_y1 из LEVEL%d.BIN)
     *
     * Восстановленные физические константы (по наблюдениям демо в DOSBox):
     *   - JUMP_VY = -4 (начальная скорость прыжка)
     *   - GRAVITY = +0.25 (за кадр)
     *   - WALK_SPEED = 1 px/кадр
     *   - GROUND_Y определён первым ObjectRegion в LEVEL0.BIN (по vy=70). */
    constexpr int GROUND_Y     = 115;    /* нижняя граница «игрового поля» */
    constexpr int CEILING_Y    = 75;
    constexpr int JUMP_VY_Q4   = -64;    /* -4.0 в формате Q4 (×16)        */
    constexpr int GRAVITY_Q4   = 4;      /* +0.25 в формате Q4             */
    constexpr int WALK_SPEED   = 1;      /* px/кадр                        */

    int karnov_x  = 160;
    int karnov_y  = GROUND_Y;
    int karnov_vy_q4 = 0;                /* Q4: y velocity × 16            */
    int karnov_facing = 1;               /* +1 = right, -1 = left          */
    int karnov_pose   = 0;               /* 0=stand,1=walk,2=jump,3=crouch */
    bool on_ground = true;

    /* Зелёный враг — тип 13 (Y-snap tracker), из LEVEL0.BIN spawn[4]:
     *   cur=(128,110), range=(160,150), trigger=5120
     * В оригинале AI обработчик медленно «прыгает» к Karnov-у по Y.
     * Здесь — простая bobbing-анимация (мирный покой на заставке). */
    int  enemy_x = 260;
    int  enemy_y = 113;
    int  enemy_phase = 0;
    bool enemy_alive = true;

    SplashVideoMode vm = opt.video_mode;

    /* --- 5. Главный цикл. -------------------------------------------- */
    static uint32_t fb[VIEW_W * VIEW_H];
    u16 current_frame = 0;
    bool demo_done    = false;
    bool paused       = false;
    Uint32 next_tick  = SDL_GetTicks();
    const Uint32 FRAME_MS = 1000 / 60;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: case SDLK_q: running = 0; break;
                case SDLK_SPACE:  paused = !paused; break;
                case SDLK_r:
                    current_frame = 0; mov_curr_idx = 0;
                    demo_done = false; input_state_ = 0;
                    karnov_x = 160; karnov_y = 105;
                    break;
                case SDLK_c:      vm = SPLASH_CGA; break;
                case SDLK_e:      vm = SPLASH_EGA; break;
                default: break;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        if (now >= next_tick) {
            next_tick = now + FRAME_MS;
            if (!paused && !demo_done) {
                if (tick_demo(current_frame)) demo_done = true;

                /* ─── Karnov-физика (декомпилировано из PRE/POST-KARNOV, см. memory). ── */
                u8 in = input_state_;
                bool wants_left  = (in & 0x02) != 0;
                bool wants_right = (in & 0x01) != 0;
                bool wants_down  = (in & 0x04) != 0;
                bool wants_up    = (in & 0x08) != 0;

                /* Горизонтальная скорость */
                if (wants_right) { karnov_x += WALK_SPEED; karnov_facing =  1; }
                if (wants_left ) { karnov_x -= WALK_SPEED; karnov_facing = -1; }

                /* Прыжок: UP только если на земле */
                if (wants_up && on_ground) {
                    karnov_vy_q4 = JUMP_VY_Q4;
                    on_ground = false;
                }

                /* Гравитация: vy += g */
                karnov_vy_q4 += GRAVITY_Q4;
                if (karnov_vy_q4 >  64) karnov_vy_q4 =  64;   /* terminal velocity */
                if (karnov_vy_q4 < -96) karnov_vy_q4 = -96;
                karnov_y += karnov_vy_q4 / 16;

                /* Столкновение с потолком/полом */
                if (karnov_y > GROUND_Y) {
                    karnov_y = GROUND_Y;
                    karnov_vy_q4 = 0;
                    on_ground = true;
                }
                if (karnov_y < CEILING_Y) {
                    karnov_y = CEILING_Y;
                    karnov_vy_q4 = 0;
                }
                if (karnov_x < 8)   karnov_x = 8;
                if (karnov_x > 312) karnov_x = 312;

                /* Поза */
                if      (!on_ground)             karnov_pose = 2;    /* jump   */
                else if (wants_down)             karnov_pose = 3;    /* crouch */
                else if (wants_left||wants_right)karnov_pose = 1;    /* walk   */
                else                             karnov_pose = 0;    /* stand  */

                /* ─── AI type=13 (Y-tracker) — упрощённый порт обработчика
                 * cs:0x11D6:0x06A8 (file 0x13C08). В оригинале:
                 *   state 0: obj.y -= [DS:0x1002]  (Y-snap к игроку),
                 *            ai_result |= 0x10 (spawn FX);
                 *   state 1: state_list 0x18CC, sub_timer_max=4.
                 *
                 * Здесь — упрощённое поведение: каждые ~64 кадра делает
                 * один шаг по Y в сторону Karnov, и медленно идёт к
                 * Karnov по X (когда тот ближе предела дистанции). */
                enemy_phase = (current_frame >> 4) & 3;
                if ((current_frame & 63) == 0) {
                    /* Y-snap step */
                    if (karnov_y < enemy_y - 2)      enemy_y -= 1;
                    else if (karnov_y > enemy_y + 2) enemy_y += 1;
                    /* X-tracking: следует за Karnov при удалении */
                    int dx = karnov_x - enemy_x;
                    if (dx >  40)      enemy_x += 1;
                    else if (dx < -40) enemy_x -= 1;
                }
                /* Удерживаем врага в игровой зоне */
                if (enemy_x < 20)  enemy_x = 20;
                if (enemy_x > 300) enemy_x = 300;
                if (enemy_y < 90)  enemy_y = 90;
                if (enemy_y > 115) enemy_y = 115;
                /* AI handler в оригинале также проверяет столкновение с
                 * Karnov для нанесения урона. На заставке мы не считаем
                 * жизни — просто визуально показываем взаимодействие. */
                if (enemy_alive) {
                    int dx = enemy_x - karnov_x;
                    int dy = enemy_y - karnov_y;
                    if (dx*dx + dy*dy < 100) {
                        /* «контакт» — на заставке просто мигаем,
                         * не убираем врага */
                        enemy_phase = (current_frame >> 1) & 3;
                    }
                }

                /* Анимация птиц (enemy type 6). */
                for (int i = 0; i < g_num_enemies; ++i) {
                    if (g_enemies[i].type == 6)
                        g_enemies[i].anim_phase = (current_frame >> 3) & 1;
                }
                ++current_frame;
            }
        }

        /* Рендер. */
        render_level(fb, vm);
        for (int i = 0; i < g_num_enemies; ++i) {
            const auto& ge = g_enemies[i];
            if (ge.type == 6) {
                draw_bird(fb, ge.x, ge.y, ge.anim_phase, enemy_color(ge.type));
            } else if (ge.type == 2) {
                /* Type 2 — невидимый триггер, не рисуем. */
            } else {
                /* «наземный враг» — 8×8 квадрат с обводкой */
                draw_box(fb, ge.x - 4, ge.y - 4, 8, 8, EGA_PAL16[0xF]);
                draw_box(fb, ge.x - 3, ge.y - 3, 6, 6, enemy_color(ge.type));
            }
        }
        /* Зелёный враг и лестница из оригинальной заставки (LEVEL0
         * был задуман как титульный экран; в оригинале справа стоит
         * зелёный тролль, между ним и Карновом — лестница).
         *
         * Враг моргает (закрывает глаза каждый 4-й кадр enemy_phase). */
        draw_ladder(fb, 160, 60, 110);
        if (enemy_alive) draw_green_enemy(fb, enemy_x, enemy_y, current_frame);
        /* Зелёный каменный человек (ai_type=512) — анимированный спрайт слева. */
        draw_stone_man(fb, 60, 113, current_frame, /*facing_left=*/false);
        draw_karnov(fb, karnov_x, karnov_y, input_state_, current_frame);
        render_hud(fb, current_frame, demo_done, karnov_x);

        SDL_UpdateTexture(tex, NULL, fb, VIEW_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(1);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    /* Не вызываем SDL_Quit() — это сделает audio_close(). */
    return 0;
}

} /* namespace karnov */
