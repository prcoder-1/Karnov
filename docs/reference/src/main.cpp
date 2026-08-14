/* main.cpp — точка входа (соответствует Turbo-C _main, вызываемому из
 * стартапа cs:0x1800).
 *
 * Стартап (cs:0x1800..0x18A0) — стандартный «c0.asm» Turbo-C 1987:
 *   1. STI
 *   2. mov dx, 0x1D99       ; DS-сегмент
 *   3. INT 21h AH=0x30      ; версия DOS
 *   4. вычитка argv[0] из ENV (поиск завершающих '0' '=')
 *   5. allocate-arena, INT 21h AH=0x4A
 *   6. call инициализационных таблиц (cs:0x2566, cs:0x256A)
 *   7. call main()
 *
 * Здесь — минимальная скелетная реализация. Запуск собственно игры
 * требует реализации графической и звуковой подсистем (вне рамок этой
 * реконструкции).
 */
#include "karnov.h"
#include "globals.h"
#include "game.h"
#include "video.h"
#include "audio.h"
#include "splash.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace karnov;

int main(int argc, char* argv[]) {
    /* --- 1. Видеорежим: SHIFT при запуске → меню. -------------------- */
    bool force_prompt = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0) force_prompt = true;
    }

    VideoMode vm = force_prompt ? prompt_video_mode() : autodetect_video_mode();
    init_video(vm);

    /* --- 2. Выделить структуры демо. --------------------------------- */
    /* Резервируем место под максимальную длину (256 событий — больше
     * хватит для оригинальных файлов). */
    static u8 demo_play_buf  [4 + 256*sizeof(MovRecord)];
    static u8 demo_record_buf[4 + 256*sizeof(MovRecord)];
    std::memset(demo_play_buf,   0, sizeof(demo_play_buf));
    std::memset(demo_record_buf, 0, sizeof(demo_record_buf));
    demo_play   = reinterpret_cast<DemoState*>(demo_play_buf);
    demo_record = reinterpret_cast<DemoState*>(demo_record_buf);

    /* --- 3. Выделить буферы уровня (стат. размер для max-LEVEL9). ---- */
    static ObjectRegion level_objects_buf[16];
    static EnemySpawn   level_enemies_buf[256];
    static u8           map_buffer_buf  [256 * MAP_ROW_EXT_BYTES];
    static u8           tile_pixels_buf [256 * 512];

    level_objects = level_objects_buf;
    level_enemies = level_enemies_buf;
    map_buffer    = map_buffer_buf;
    tile_pixels   = tile_pixels_buf;

    /* --- 4. Аудио. --------------------------------------------------- */
    bool with_audio = false;
    if (audio_init() == 0) {
        with_audio = true;
        std::printf("audio: SDL2 initialised, loader music will start with splash\n");
        std::fflush(stdout);
    } else {
        std::printf("audio: disabled (SDL2 init failed)\n");
    }

    /* --- 5. Запустить заставку (LEVEL0 + LEVEL0.MOV). ----------------- *
     * splash_run() полностью замещает классический run_game(), который
     * требует SPRITES.EGA-блиттера и полной игровой логики (см.
     * karnov_cpp/src/game.cpp). Здесь мы:
     *   - загружаем LEVEL0.BIN правильно (см. splash.cpp);
     *   - проигрываем демо LEVEL0.MOV (cs:0x6DC0 tick_demo);
     *   - запускаем loader-music DS:0x0A6A (track 6) на повтор;
     *   - рендерим в SDL2-окно в EGA-палитре (16 цветов) по умолчанию.
     */
    SplashOptions opt;
    opt.video_mode = SPLASH_EGA;
    opt.level_index = 0;
    opt.with_audio  = with_audio;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cga") == 0) opt.video_mode = SPLASH_CGA;
        else if (std::strcmp(argv[i], "--ega") == 0) opt.video_mode = SPLASH_EGA;
        else if (std::strcmp(argv[i], "--level") == 0 && i+1 < argc)
            opt.level_index = static_cast<u16>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--scale") == 0 && i+1 < argc)
            opt.win_scale = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--ppm") == 0 && i+1 < argc)
            opt.ppm_path = argv[++i];
        else if (std::strcmp(argv[i], "--frame") == 0 && i+1 < argc)
            opt.ppm_frame = std::atoi(argv[++i]);
    }
    int rc = splash_run(opt);

    audio_stop();
    audio_close();
    return rc;
}
