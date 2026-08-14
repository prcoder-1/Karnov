/* splash.h — точка входа attract-mode рендера (LEVEL0 + LEVEL0.MOV).
 *
 * Декомпилированная реализация загрузки и отображения уровня 0 в режиме
 * демонстрации, с воспроизведением loader-music из cs:0x40E (MUSIC_0A6A).
 * Используется как замена run_game() для запуска игры со заставки.
 */
#ifndef KARNOV_SPLASH_H
#define KARNOV_SPLASH_H

#include "karnov.h"

namespace karnov {

enum SplashVideoMode {
    SPLASH_EGA = 0,    /* 16 цветов, две палитры по attr — по умолчанию */
    SPLASH_CGA = 1,    /* 4 цвета, IBM CGA palette 1 high-intensity      */
};

struct SplashOptions {
    SplashVideoMode video_mode  = SPLASH_EGA;
    u16             level_index = 0;       /* 0..9 */
    int             win_scale   = 3;
    bool            with_audio  = true;
    /* Если ppm_path не null — пропустить демо до кадра ppm_frame, отрендерить
     * один кадр в PPM по этому пути и сразу выйти (для проверки рендера). */
    const char*     ppm_path    = nullptr;
    int             ppm_frame   = 0;
};

/* Запускает SDL2-окно, играет loader-music, показывает уровень 0 и
 * воспроизводит демо LEVEL0.MOV. Возвращает 0 при штатном выходе. */
int splash_run(const SplashOptions& opt);

} /* namespace karnov */

#endif
