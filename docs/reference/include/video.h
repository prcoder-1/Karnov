/* video.h — выбор и инициализация видеорежима. */
#ifndef KARNOV_VIDEO_H
#define KARNOV_VIDEO_H

#include "karnov.h"

namespace karnov {

/* Меню выбора режима (строки DS:0x573, 0x597, 0x5A9, 0x5BB, 0x5CD). */
VideoMode prompt_video_mode();

/* Авто-определение режима. cs:0x1F2BC..0x1F2D6 содержит таблицу подсказок
 * ("Tandy", "01.02.00", "1400 LT", "Tandon"). */
VideoMode autodetect_video_mode();

/* Установка режима через INT 10h. */
void init_video(VideoMode mode);

/* Очистка экрана. cs:0x15F:0x91 — вызывается во многих местах. */
void clear_screen();

/* Рисование текста на экране. cs:0x4E4:0xB — печать строки. */
void draw_text(int row, const char* text);

} /* namespace karnov */

#endif
