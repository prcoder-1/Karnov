/* video.cpp — выбор и инициализация видеорежима.
 *
 * В оригинале KARNOV.EXE поддерживает четыре графических адаптера:
 *  • CGA  (320×200, 4 цвета) — DS:0x597
 *  • EGA  (320×200, 16 цветов) — DS:0x5A9
 *  • TGA  (Tandy 16-color) — DS:0x5BB
 *  • Hercules / Mono — DS:0x5CD
 *
 * Авто-определение использует строки-подсказки:
 *  • "Tandy"   (DS:0x2B6) — для Tandy 1000
 *  • "01.02.00"(DS:0x2BC)
 *  • "1400 LT" (DS:0x2C5) — для Tandy 1400
 *  • "Tandon"  (DS:0x2CD)
 *
 * Если удерживается SHIFT при запуске (cs:0x3C50..), показывается меню
 * выбора режима (строка "press key for desired video mode" из DS:0x573).
 */
#include "video.h"
#include "globals.h"
#include <cstdio>
#include <cstdlib>

namespace karnov {

/* KARNOV.EXE: cs:0x?? — prompt_video_mode.
 *
 * Печатает четыре строки и ждёт нажатия C/E/T/M.
 */
VideoMode prompt_video_mode() {
    clear_screen();
    draw_text(8,  "press key for desired video mode");
    draw_text(10, "C     -     CGA");
    draw_text(11, "E     -     EGA");
    draw_text(12, "T     -     TGA");
    draw_text(13, "M     -     Monochrome Graphics");

    int c;
    while (true) {
        c = std::getchar();
        if (c == 'C' || c == 'c') return VM_CGA;
        if (c == 'E' || c == 'e') return VM_EGA;
        if (c == 'T' || c == 't') return VM_TGA;
        if (c == 'M' || c == 'm') return VM_HERC;
        if (c == EOF) std::exit(1);
    }
}

/* KARNOV.EXE: автодетектор.
 * Реальная реализация смотрит BIOS data area (40:10h, 40:49h, 40:87h),
 * сравнивает строки в ROM ("Tandy", "1400 LT", "Tandon") по фиксированным
 * адресам и анализирует биты адаптера.
 */
VideoMode autodetect_video_mode() {
    /* TODO: INT 11h, чтение 40:10h, поиск сигнатуры в C000:0..C7FF:0. */
    return VM_EGA;
}

/* INT 10h: AH=0, AL=video_mode. */
void init_video(VideoMode mode) {
    video_mode = static_cast<u8>(mode);
    /* TODO: для каждого режима — соответствующий INT 10h вызов и
     * настройка указателей видеопамяти. */
}

/* KARNOV.EXE: cs:0x15F:0x91 — clear_screen. */
void clear_screen() {
    /* TODO: очистка видеопамяти. */
    std::printf("\x1b[2J\x1b[H");
}

/* KARNOV.EXE: cs:0x4E4:0x0B — draw_text(row, ptr).
 *
 * В оригинале три-аргументная сигнатура (row, far ptr, length). Здесь
 * сворачиваем до C-строки. */
void draw_text(int row, const char* text) {
    /* TODO: вывод через знакогенератор/спрайт-текст. */
    std::printf("\x1b[%d;1H%s\n", row + 1, text);
}

/* ─── Глобалы ───────────────────────────────────────────────────────── */
u8  video_mode    = VM_EGA;

} /* namespace karnov */
