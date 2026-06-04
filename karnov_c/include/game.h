/* game.h — точка входа splash + игрового цикла. */
#ifndef KARNOV_GAME_H
#define KARNOV_GAME_H

#include "karnov.h"

typedef struct {
    video_mode_t video_mode;
    u16          level_num;
    int          scale;
    bool         with_audio;
    const char  *ppm_path;     /* NULL = SDL2 окно, иначе headless PPM */
    int          ppm_frame;
    bool         music_test;   /* headless: ждать один цикл loader-music и выйти */
    bool         karnov_showcase; /* headless PPM: 8 Karnov-поз на splash */
} splash_options_t;

/* Запускает splash (LEVEL%d.BIN + attract demo). Возвращает 0=OK. */
int splash_run(const splash_options_t *opt);

#endif /* KARNOV_GAME_H */
