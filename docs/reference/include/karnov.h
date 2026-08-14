/* karnov.h — типы, константы, базовые декларации.
 *
 * Реконструкция KARNOV.EXE (Data East USA / Quicksilver Software, 1987).
 * Оригинал — 16-битный DOS-бинарник Borland Turbo-C 1987, compact-модель.
 *
 * В оригинальных far-указателях offset = младший WORD, segment = старший.
 * В реконструкции — обычные указатели; смысл сохраняется.
 */
#ifndef KARNOV_H
#define KARNOV_H

#include <cstdint>
#include <cstdio>
#include <cstring>

/* ─── Базовые типы оригинала ────────────────────────────────────────── */

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;

/* В оригинале — far void*; здесь обычный указатель. */
template <typename T> using Far = T*;

/* ─── Игровые константы (из дизассемблирования) ─────────────────────── */

namespace karnov {

constexpr int NUM_LEVELS         = 10;     /* 0..9 */
constexpr int FINAL_LEVEL        = 9;      /* DS:0xD8CE сравнивается с 9 */
constexpr int MAP_COLUMNS        = 40;     /* 0x28 — байт в строке карты */
constexpr int MAP_ROW_EXT_BYTES  = 80;     /* 0x50 — режим [DS:0x360]==3 */
constexpr int RLE_ESCAPE         = 0x5A;   /* 'Z' — маркер RLE */
constexpr int IO_BUF_SIZE        = 0x200;  /* 512 — размер буфера fread */
constexpr int MAX_PLAYERS        = 2;      /* проверка `cmp si, 2` в update_players */

/* Биты состояния ввода (из 0x39CA, 0x3A60..0x3B08) */
constexpr u8 INPUT_RIGHT  = 0x01;
constexpr u8 INPUT_LEFT   = 0x02;
constexpr u8 INPUT_DOWN   = 0x04;
constexpr u8 INPUT_UP     = 0x08;
constexpr u8 INPUT_FIRE   = 0x10;
constexpr u8 INPUT_OPTION = 0x20;

/* Видеорежимы (из видео-меню по 0x1F573 и 0x1F597..) */
enum VideoMode : u8 {
    VM_CGA  = 0,
    VM_EGA  = 1,
    VM_TGA  = 2,    /* Tandy 16-color */
    VM_HERC = 3,    /* Hercules / Monochrome Graphics */
};

/* Опции из инвентаря (из KARNOV.TXT — справочно) */
enum OptionItem : u8 {
    OPT_BOOTS    = 0,
    OPT_BOMB     = 1,
    OPT_LADDER   = 2,
    OPT_BOOMER   = 3,
    OPT_CLAPPER  = 4,
    OPT_GLASSES  = 5,
    OPT_MASK     = 6,
    OPT_WINGS    = 7,
    OPT_SHIELD   = 8,
    OPT_K_MARK   = 9,
};

/* ─── Структуры файлов (см. FORMATS.md) ────────────────────────────── */

/* MOV: BYTE state + WORD frame_le; на диске 3 байта (НЕ выравнено) */
struct MovRecord {
    u8  input_state;
    u16 frame_number;       /* в файле — little-endian, на диске не выровнено */
} __attribute__((packed));

/* Заголовок MOV-файла */
struct MovHeader {
    u16 start_record_idx;   /* всегда 0 */
    u16 num_records;
};

/* В оперативной памяти структуры демо лежат по дальнему указателю DS:0xD55C
 * (воспроизведение) и DS:0xD560 (запись). Их формат идентичен файлу плюс
 * первое слово используется как «текущий индекс». */
struct DemoState {
    u16 curr_idx;           /* в памяти: курсор */
    u16 num_records;        /* копия из заголовка */
    MovRecord records[1];   /* динамически — реальный размер num_records */
};
static_assert(sizeof(MovRecord) == 3, "MovRecord must be packed 3 bytes");

/* BIN: запись региона (12 байт) */
struct ObjectRegion {
    u16 x0, y0;
    u16 x1, y1;
    u16 vx, vy;
};
static_assert(sizeof(ObjectRegion) == 12, "ObjectRegion = 12 bytes");

/* BIN: запись спавна врага/объекта (20 байт) */
struct EnemySpawn {
    u16 type;
    u16 subtype;
    u16 cur_x,   cur_y;
    u16 range_x, range_y;
    u16 spawn_x, spawn_y;
    u16 trigger;
    u16 flags;
};
static_assert(sizeof(EnemySpawn) == 20, "EnemySpawn = 20 bytes");

/* ─── Утилиты ───────────────────────────────────────────────────────── */

/* Little-endian чтение из массива байт (compile-time помощник). */
inline u16 read_u16_le(const u8* p) {
    return static_cast<u16>(p[0]) | (static_cast<u16>(p[1]) << 8);
}

inline void write_u16_le(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>(v >> 8);
}

} /* namespace karnov */

#endif /* KARNOV_H */
