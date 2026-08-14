/* level.h — загрузка LEVEL%d.BIN. cs:0x5350. */
#ifndef KARNOV_LEVEL_H
#define KARNOV_LEVEL_H

#include "karnov.h"

namespace karnov {

/* Коды возврата load_level() — извлечены из путей jmp 0x584e в cs:0x5350. */
enum LevelLoadStatus : int {
    LL_OK              = 0,
    LL_REGIONS_FAIL    = 11,   /* B = 0x0B */
    LL_ENEMIES_FAIL    = 21,   /* 0x15 */
    LL_HDR_FAIL_1      = 31,   /* 0x1F */
    LL_HDR_FAIL_2      = 41,   /* 0x29 */
    LL_HDR_FAIL_3      = 51,   /* 0x33 */
    LL_ROW_HDR_FAIL_A  = 61,   /* 0x3D */
    LL_ROW_HDR_FAIL_B  = 71,   /* 0x47 */
    LL_ROW_DATA_FAIL_A = 81,   /* 0x51 */
    LL_ROW_DATA_FAIL_B = 91,   /* 0x5B */
    LL_TILES_FAIL      = 101,  /* 0x65 */
    LL_OPEN_FAIL       = 1,
};

/* KARNOV.EXE: cs:0x5350 — основной загрузчик уровня. */
int load_level(u16 level_number);

/* KARNOV.EXE: cs:0x51B6 — чтение таблицы регионов (внутренняя). */
int read_object_regions(FILE* fp);

/* KARNOV.EXE: cs:0x5216 — чтение таблицы спавна врагов (внутренняя). */
int read_enemies(FILE* fp);

} /* namespace karnov */

#endif
