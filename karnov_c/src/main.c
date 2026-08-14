/* main.c — точка входа karnov_c.
 *
 * CLI:
 *   --cga / --ega       — выбор палитры (default EGA)
 *   --level N           — какой LEVEL%d.BIN загрузить (default 0)
 *   --scale N           — множитель окна (default 3)
 *   --ppm path --frame N — headless рендер N-го кадра в PPM, выход
 *   --no-audio          — отключить SDL2 audio
 */
#include "karnov.h"
#include "game.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    splash_options_t opt = {
        .video_mode = VIDEO_EGA,
        .level_num  = 0,
        .scale      = 3,
        .with_audio = true,
        .ppm_path   = NULL,
        .ppm_frame  = 0,
    };

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if      (!strcmp(a, "--cga")) opt.video_mode = VIDEO_CGA;
        else if (!strcmp(a, "--ega")) opt.video_mode = VIDEO_EGA;
        else if (!strcmp(a, "--no-audio")) opt.with_audio = false;
        else if (!strcmp(a, "--demo")) opt.with_demo = true;
        else if (!strcmp(a, "--level") && i + 1 < argc) opt.level_num = (u16)atoi(argv[++i]);
        else if (!strcmp(a, "--scale") && i + 1 < argc) opt.scale     = atoi(argv[++i]);
        else if (!strcmp(a, "--ppm")   && i + 1 < argc) opt.ppm_path  = argv[++i];
        else if (!strcmp(a, "--frame") && i + 1 < argc) opt.ppm_frame = atoi(argv[++i]);
        else if (!strcmp(a, "--music-test")) opt.music_test = true;
        else if (!strcmp(a, "--karnov-showcase")) opt.karnov_showcase = true;
        else {
            fprintf(stderr, "Неизвестный флаг: %s\n", a);
            return 1;
        }
    }

    if (SDL_Init(opt.with_audio ? SDL_INIT_AUDIO : 0) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    int rc = splash_run(&opt);

    SDL_Quit();
    return rc;
}
