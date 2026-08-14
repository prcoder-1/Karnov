/* globals.h — глобальное состояние игры (соответствует переменным в DS).
 *
 * Адреса в комментариях — это смещения от DS=0x1D99 в KARNOV.EXE.
 */
#ifndef KARNOV_GLOBALS_H
#define KARNOV_GLOBALS_H

#include "karnov.h"
#include <cstdio>

namespace karnov {

/* ─── Ввод ─────────────────────────────────────────────────────────── */

/* DS:0xCC9E — текущая байт-маска ввода (одна для клавиатуры/джойстика/демо). */
extern u8 input_state;

/* DS:0xCCA0 — предыдущая маска (для определения rising-edge). */
extern u8 prev_input_state;

/* DS:0xCCA1 — некое отражение последнего нажатия. */
extern u8 input_history;

/* DS:0xCCA2 — старший бит option-кнопки. */
extern u16 option_pressed;

/* DS:0xCCAA — старший бит fire-кнопки. */
extern u16 fire_pressed;

/* DS:0xCCAC — bits 0..3 направление. */
extern u16 direction_bits;

/* DS:0xCCA8 — рабочий накопитель битов в keyboard_read (cs:0x39CA). */
extern u16 raw_keyboard_state;

/* DS:0xCC9F — последняя ASCII-клавиша. */
extern u16 last_key_code;

/* DS:0xCC9C — некий счётчик/флаг (используется в обработке клавиш). */
extern u16 key_counter;

/* DS:0xCCAE — флаг изменения направления. */
extern u16 direction_changed;

/* DS:0xCCCA — счётчик подсказки «нажмите клавишу». */
extern u16 hint_counter;

/* ─── Игровое состояние ────────────────────────────────────────────── */

/* DS:0xCDEA — текущий игровой кадр (с него тикает сравнение в демо). */
extern u16 current_frame;

/* DS:0xCDEC — флаг «в игре, не в меню». */
extern u16 in_game;

/* DS:0xCDEE — выбранный видеорежим (см. VideoMode). */
extern u8 video_mode;

/* DS:0xCDF0 — некий счётчик (используется при scroll-update). */
extern u16 scroll_counter;

/* DS:0xCDF4 — фоновый цвет/палитра. */
extern u16 bg_palette;

/* DS:0xCE00, 0xCE02, 0xCE04 — буферы UI/HUD. */
extern u16 hud_state[3];

/* DS:0xCF4E — счётчик жизней? (декрементируется при THE_END logic). */
extern u16 lives_counter;

/* DS:0xD8AA — номер текущего уровня (0..9). */
extern u16 current_level;

/* DS:0xD8C8/CA/CC — обнуляются при reset_level (см. cs:0x3FC0). */
extern u16 level_state_a, level_state_b, level_state_c;

/* DS:0xD8CE — индекс «последнего пройденного» уровня (==9 → конец игры). */
extern u16 max_level_reached;

/* DS:0xD8B0 — кэшированные биты вертикального направления (UP/DOWN). */
extern u16 vert_dir_cache;

/* DS:0x35A — флаг «1 player / 2 players» (toggle по '1'). */
extern u16 num_players;

/* DS:0x358 — флаг ввода: 0=клавиатура, 1=джойстик. */
extern u16 use_joystick;

/* DS:0x354 — флаг музыки on/off. */
extern u16 music_on;

/* DS:0x356 — флаг звука on/off. */
extern u16 sound_on;

/* DS:0x35C — флаг «находимся в игре» (для пауз). */
extern u16 in_play;

/* DS:0x360 — режим обработки уровня (0/2 — обычный, 3 — особый, см. level.cpp). */
extern u16 level_mode;

/* ─── Указатели на динамически выделенные структуры ────────────────── */

/* DS:0xD55C — указатель на структуру воспроизводимого демо. */
extern DemoState* demo_play;

/* DS:0xD560 — указатель на структуру записываемого демо. */
extern DemoState* demo_record;

/* DS:0x47C — указатель на массив ObjectRegion (заполняется в level.cpp). */
extern ObjectRegion* level_objects;

/* DS:0xCDFE — количество элементов в level_objects. */
extern u16 num_level_objects;

/* DS:0x494 — указатель на массив EnemySpawn. */
extern EnemySpawn* level_enemies;

/* DS:0xCDFA — количество элементов в level_enemies. */
extern u16 num_level_enemies;

/* DS:0x98 — указатель на буфер строк карты. */
extern u8* map_buffer;

/* DS:0xB82A — указатель на буфер графики тайлов. */
extern u8* tile_pixels;

/* DS:0xB830 — количество уникальных тайлов в уровне. */
extern u16 num_unique_tiles;

/* DS:0xCC98 — размер одного тайла в БАЙТАХ. Считывается прямо из третьего
 * WORD-а заголовка карты в LEVEL%d.BIN (несмотря на историческое имя
 * `tile_size_words`, поле в файле — байты, что проверено по сумме
 * размеров блоков всех LEVEL*.BIN). */
extern u16 tile_size_bytes;

/* DS:0xCC96 — то же в WORD-ах = tile_size_bytes / 2 (для совместимости). */
extern u16 tile_size_words;

/* DS:0x48C/48E — параметры из первого ObjectRegion (см. level.cpp). */
extern u16 obj0_x1, obj0_y1;

/* ─── RLE-декомпрессор: внутреннее состояние (cs:0x7127) ────────────── */

/* DS:0xD568 — флаг «находимся в режиме повтора». */
extern u16 rle_in_run;

/* DS:0xD76A — байт, выдаваемый в режиме повтора. */
extern u8 rle_run_byte;

/* DS:0xD76D — оставшийся счётчик повторов. */
extern u8 rle_run_left;

/* DS:0xD566 — сколько байт в буфере осталось не выданным. */
extern u16 io_buf_avail;

/* DS:0xD76B — позиция в буфере. */
extern u16 io_buf_pos;

/* DS:0xD56A — собственно 512-байтный буфер. */
extern u8 io_buffer[IO_BUF_SIZE];

/* Текущий FILE* для bin_fread (заменяет реальный FP в обёртке). */
extern FILE* bin_stream;

} /* namespace karnov */

#endif /* KARNOV_GLOBALS_H */
