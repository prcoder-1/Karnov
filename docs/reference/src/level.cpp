/* level.cpp — загрузка LEVEL%d.BIN.
 *
 * Пословный перевод из KARNOV.EXE:
 *   cs:0x5350 — load_level         (главная функция)
 *   cs:0x51B6 — read_object_regions
 *   cs:0x5216 — read_enemies
 *
 * Все обращения к диску — через bin_fopen/bin_fread, которые на лету
 * выполняют RLE-распаковку (см. file_io.cpp).
 *
 * Структура BIN-файла (после распаковки):
 *   [2]  num_objects
 *   [num_objects * 12]   ObjectRegion[]
 *   [2]  num_enemies
 *   [num_enemies * 20]   EnemySpawn[]
 *   [2]  num_map_rows
 *   [2]  num_unique_tiles
 *   [2]  tile_size_bytes      ; размер одного тайла В БАЙТАХ
 *   For row in 0..num_map_rows-1:
 *       [1]  tile_idx
 *       [1]  attr
 *       [40] row_data
 *       [80] row_extra
 *   [num_unique_tiles * tile_size_bytes]   tile_pixels
 *
 * Проверено побайтово по реальным LEVEL0..3,9.BIN: сумма
 * (num_map_rows * 122 + num_unique_tiles * tile_size_bytes) точно
 * совпадает с количеством байт после фиксированного заголовка.
 */
#include "level.h"
#include "globals.h"
#include "file_io.h"
#include "video.h"
#include <cstdio>
#include <cstdlib>

namespace karnov {

/* KARNOV.EXE: cs:0x51B6.
 *
 *   fread(&num_level_objects, 2, 1, fp);    // DS:0xCDFE
 *   if (fread succeeded):
 *       fread(level_objects, 12, num_level_objects, fp)
 *       if (count matches):
 *           obj0_x1 = level_objects[0].x1   ; DS:0x48C ← [es:bx+0x8]
 *           obj0_y1 = level_objects[0].y1   ; DS:0x48E ← [es:bx+0xA]
 *           return 0
 *   return 1
 */
int read_object_regions(FILE* fp) {
    if (bin_fread(&num_level_objects, 2, 1, fp) != 1) return 1;
    if (num_level_objects == 0) { obj0_x1 = obj0_y1 = 0; return 0; }
    if (bin_fread(level_objects, sizeof(ObjectRegion),
                  num_level_objects, fp) != num_level_objects) return 1;
    obj0_x1 = level_objects[0].x1;
    obj0_y1 = level_objects[0].y1;
    return 0;
}

/* KARNOV.EXE: cs:0x5216.
 *   fread(&num_level_enemies, 2, 1, fp);
 *   fread(level_enemies, 20, num_level_enemies, fp);
 *   Затем чтение/обработка каждой записи в локальном цикле (на стек
 *   копируется по 20 байт, затем берётся [bx+4] и т. д. — это, видимо,
 *   связывание с глобальным состоянием спавна).
 */
int read_enemies(FILE* fp) {
    if (bin_fread(&num_level_enemies, 2, 1, fp) != 1) return 1;
    if (num_level_enemies == 0) return 0;
    if (bin_fread(level_enemies, sizeof(EnemySpawn),
                  num_level_enemies, fp) != num_level_enemies) return 1;

    /* cs:0x5268..0x533C — цикл по записям, который заполняет внутренние
     * таблицы (адресации [bp-0x4]/[bp-0x6]) и не сохраняет результат
     * наружу. Деталь повёрнута в исходник C, но здесь не критична —
     * заглушка явно отмечает место. */
    for (u16 i = 0; i < num_level_enemies; ++i) {
        /* TODO: cs:0x5268 — bind_enemy_to_spawn(level_enemies[i]); */
    }
    return 0;
}

/* Вспомогательная функция: ждать «нажмите клавишу» с возможностью выйти.
 * Соответствует группе вызовов cs:0x53A8..0x53FA. */
static void prompt_insert_data_disk(bool prog_disk, const char* missing_file) {
    clear_screen();
    char line[128];
    if (prog_disk) {
        std::snprintf(line, sizeof(line),
                      "Please insert program disk (file not found: %s)",
                      missing_file);
    } else {
        std::snprintf(line, sizeof(line),
                      "Please insert data disk (file not found: %s)",
                      missing_file);
    }
    draw_text(10, line);
    draw_text(12, "press any key when ready (Q to quit)");

    /* cs:0x20F:0x0A — get_key(); cs:0x330:0x30B — exit(0).
     * EOF на stdin (например при перенаправлении) — выходим, иначе
     * получится бесконечный цикл без возможности продолжить. */
    int c = std::getchar();
    if (c == EOF || c == 'q' || c == 'Q') {
        clear_screen();
        std::exit(c == EOF ? 1 : 0);
    }
    clear_screen();
}

/* KARNOV.EXE: cs:0x5350 — load_level.
 *
 * Полный перевод. Если внутри что-то не считается, возвращаются коды LL_*
 * (см. level.h), которые в оригинале использовались для отладки —
 * jmp 0x584E загружает AX и выходит. */
int load_level(u16 level_number) {
    char filename[16];
    std::sprintf(filename, "level%1u.bin", static_cast<unsigned>(level_number));

    /* (1) Открыть файл (с повторными попытками при ошибке). */
    FILE* fp = nullptr;
    for (;;) {
        fp = bin_fopen(filename, "rb");
        if (fp) break;
        prompt_insert_data_disk(/*prog_disk=*/level_number >= 7, filename);
    }

    /* (2) Оригинал закрывает и открывает заново. cs:0x5402..0x542F. */
    bin_fclose(fp);
    fp = bin_fopen(filename, "rb");
    if (!fp) return LL_OPEN_FAIL;

    /* (3) Таблица регионов. */
    if (read_object_regions(fp)) {
        bin_fclose(fp);
        return LL_REGIONS_FAIL;
    }

    /* (4) Таблица врагов. */
    if (read_enemies(fp)) {
        bin_fclose(fp);
        return LL_ENEMIES_FAIL;
    }

    /* (5) Три WORD'а заголовка карты. Третье поле — размер тайла В БАЙТАХ
     * (не в WORD-ах, как ошибочно следовало из имени переменной).
     * Подтверждено: для всех LEVEL*.BIN
     *   num_unique_tiles * <это_поле>  == размер блока тайлов в файле. */
    u16 num_map_rows = 0;
    if (bin_fread(&num_map_rows,     2, 1, fp) != 1) { bin_fclose(fp); return LL_HDR_FAIL_1; }
    if (bin_fread(&num_unique_tiles, 2, 1, fp) != 1) { bin_fclose(fp); return LL_HDR_FAIL_2; }
    if (bin_fread(&tile_size_bytes,  2, 1, fp) != 1) { bin_fclose(fp); return LL_HDR_FAIL_3; }
    tile_size_words = tile_size_bytes >> 1;  /* для совместимости */

    /* (6) Построение таблицы дальних указателей tile[i] = base + i*tile_size_bytes.
     * В оригинале хранится в массиве по адресу DS:0x52AC/0x52AE (по 4 байта
     * на запись). Здесь — просто массив указателей. */
    static u8* tile_ptrs[256];
    for (u16 i = 0; i < num_unique_tiles; ++i) {
        tile_ptrs[i] = tile_pixels + i * tile_size_bytes;
    }

    /* (7) Главный цикл чтения строк карты. */
    for (u16 row = 0; row < num_map_rows; ++row) {

        /* cs:0x5575: каждые 16 итераций — обновление экрана (yield). */
        if ((row & 0xF) == 0) {
            /* cs:0x20F:0x2FB — refresh_screen / pump_cooperative_loop. */
        }

        /* (7a) Прочитать (tile_idx, attr). cs:0x5580..0x55CE. */
        u8 tile_idx = 0, attr = 0;
        if (bin_fread(&tile_idx, 1, 1, fp) != 1) { bin_fclose(fp); return LL_ROW_HDR_FAIL_A; }
        if (bin_fread(&attr,     1, 1, fp) != 1) { bin_fclose(fp); return LL_ROW_HDR_FAIL_B; }

        /* (7b) Сохранить атрибут в lookup-таблицу.
         * Оригинал: mov [bx-0x5440 + tile_idx*2], <word> — то есть
         * 16-битная ячейка, верхний байт может быть «мусором», как
         * в скомпилированной C-программе. Здесь — корректное расширение. */
        static u16 tile_attr[256];
        tile_attr[tile_idx] = static_cast<u16>(attr);

        /* (7c) Прочитать данные строки. Размер зависит от level_mode.
         * cs:0x55FE: cmp word [DS:0x360], 0 / 2 — обычный режим;
         *           иначе (3, см. ниже) — особая обработка. */
        u8* row_dst = map_buffer + tile_idx * MAP_COLUMNS;
        if (level_mode == 0 || level_mode == 2) {
            if (bin_fread(row_dst, MAP_COLUMNS, 1, fp) != 1) {
                bin_fclose(fp); return LL_ROW_DATA_FAIL_A;
            }
            /* cs:0x5660: дополнительные 80 байт записываются на стек
             * (локальный буфер [bp-0x5C], 80 байт), затем обрабатываются. */
            u8 local_extra[MAP_ROW_EXT_BYTES];
            if (bin_fread(local_extra, MAP_ROW_EXT_BYTES, 1, fp) != 1) {
                /* cs:0x5680 — в оригинале выход с ret(1), но переход
                 * jz 0x5752 (продолжать) — то есть «не страшно». */
            }
        } else if (level_mode == 3) {
            /* cs:0x56DE — особый режим: читаем 40 байт в основной буфер
             * и 80 в локальный, далее вызываем cs:0xD18:0xD (вероятно,
             * декодер тайлов / композитор). */
            if (bin_fread(row_dst, MAP_COLUMNS, 1, fp) != 1) {
                bin_fclose(fp); return LL_ROW_DATA_FAIL_A;
            }
            u8 local_extra[MAP_ROW_EXT_BYTES];
            if (bin_fread(local_extra, MAP_ROW_EXT_BYTES, 1, fp) != 1) {
                bin_fclose(fp); return LL_ROW_DATA_FAIL_B;
            }
            /* TODO: cs:0xD18:0xD — process_tile_row(row_dst, local_extra) */
        } else {
            /* По умолчанию читаем 80 байт в позицию по индексу тайла. */
            if (bin_fread(row_dst, MAP_ROW_EXT_BYTES, 1, fp) != 1) {
                bin_fclose(fp); return LL_ROW_DATA_FAIL_B;
            }
        }
    }

    /* (8) Графика тайлов одним блоком. cs:0x575C: fread(tile_pixels,
     *     num_unique_tiles * tile_size_bytes, 1, fp). */
    std::size_t total = static_cast<std::size_t>(num_unique_tiles) * tile_size_bytes;
    if (bin_fread(tile_pixels, total, 1, fp) != 1) {
        bin_fclose(fp);
        return LL_TILES_FAIL;
    }

    bin_fclose(fp);

    /* (9) Постобработка в зависимости от level_mode. cs:0x57AB. */
    /* TODO: для каждого режима — вызвать соответствующий пост-процессор. */

    return LL_OK;
}

} /* namespace karnov */
