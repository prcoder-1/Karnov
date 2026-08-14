/* sprite.cpp — спрайты (SPRITES.CGA / SPRITES.EGA).
 *
 * Файлы SPRITES.{CGA,EGA} в репозитории помечены как ~110-120 КБ
 * iAPX 286 COFF — это не спрайты «как есть», а особый контейнер.
 * Оригинал читает их через bin_fopen (RLE-обёртку) и распаковывает.
 *
 * Соответствующие функции в KARNOV.EXE: cs:0xBFB:0x04 (init),
 * cs:0xBFB:0xB2 (splash).
 */
#include "sprite.h"
#include "globals.h"
#include "file_io.h"

namespace karnov {

/* sprite_init() реализован в src/sprite_render.cpp с полной
 * декомпиляцией cs:0xBFB:0x04. */

/* KARNOV.EXE: cs:0xBFB:0xB2. */
void splash_screen() {
    /* TODO: вывод "Karnov (C) 1987 Data East USA Inc." (DS:0xC6D),
     * "Adaptation by Quicksilver Software Inc" (DS:0xC90),
     * затем кредиты (DS:0x3A0..0x4B7): DATA EAST, Bob Lloyd, Mark Beaumont,
     * QUICKSILVER, Bill/Cindy Fisher, Rob Barris, David Steffen,
     * INTERACTIVE DESIGNS, DATABAR. */
}

} /* namespace karnov */
