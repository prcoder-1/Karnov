/* scroll.cpp — система прокрутки экрана.
 *
 * Пословный перевод двух фаз кадрового цикла:
 *   • SCROLLVIEW   (cs:0x15F:0x296, file 0x3086)  — фаза 8 (безусловная)
 *   • SCROLLUPDATE (cs:0x15F:0x195, file 0x2F85)  — фаза 10 (в игре)
 *
 *  Логика двухфазовой прокрутки описана в scroll.h:
 *    SCROLLVIEW   — «лог.»: считает дельту камеры, перерисовывает
 *                            только тайлы на раскрывающемся крае
 *                            (вертикальная полоса или горизонтальная).
 *    SCROLLUPDATE — «физ.»: перемещает видеопамять / переключает страницу.
 *
 *  Эта декомпозиция — стандартный приём DOS-эры:
 *    • буфер карты больше экрана на 1-2 тайла со всех сторон;
 *    • при движении камеры ровно один край открывает новые тайлы;
 *    • остальной экран просто «сдвигается» через CRTC start-address.
 */
#include "scroll.h"
#include "globals.h"
#include "karnov.h"
#include <cstring>

namespace karnov {

/* ─── Глобальное состояние ──────────────────────────────────────────── */
u16  view_x          = 0;  /* DS:0xCC8E */
u16  view_y          = 0;  /* DS:0xCC90 */
u16  prev_view_x     = 0;  /* DS:0xBA70 */
u16  prev_view_y     = 0;  /* DS:0xBA72 */
u16  prev_tile_box[4] = {0,0,0,0};  /* DS:0xB81E..0xB824 */
u16  cur_tile_box [4] = {0,0,0,0};  /* DS:0xA2B4..0xA2BA */
u16  anchor       [2] = {0,0};      /* DS:0xA2B0..0xA2B2 */
u8   pt_workspace[16] = {0};        /* DS:0xABB8 */
u16  video_offset    = 0;  /* DS:0xAD50 */
u16  video_segment   = 0;  /* DS:0xBA64 */
/* Lookup-таблицы (заполняются при инициализации режима). */
u16  px_to_tile_x[1024] = {0};   /* DS:0xBB5E */
u16  px_to_tile_y[1024] = {0};   /* DS:0xAF22 */

/* ─── pt_copy ─────────────────────────────────────────────────────────
 * KARNOV.EXE: cs:0x2B5:0x9D (file 0x3CED).
 * Поведение сводится к копированию 8 байт (= 4 WORD'а, "точка"-bbox)
 * из src в dst через временный буфер workspace.  В реконструкции —
 * простое присваивание. */
void pt_copy(u16* dst, u16* workspace, u16* src) {
    /* В оригинале: memcpy(workspace, src, 8); memcpy(dst, workspace, 8).
     * dst/src в оригинале адресуют 4 смежных WORD'а в DS (например
     * 0xCC8E..0xCC95).  В реконструкции делаем то же — копируем
     * пословно, чтобы не получить UB по array-bounds. */
    for (int i = 0; i < 4; ++i) {
        workspace[i] = src[i];
        dst[i]       = workspace[i];
    }
}

/* ─── draw_tile ──────────────────────────────────────────────────────
 * KARNOV.EXE: cs:0x287:0x06 (file 0x4076).
 *
 * Псевдокод:
 *   if (level_mode != 0 && level_mode != 2) return    ; mode-фильтр
 *   row_far_ptr = tile_ptrs[tile_y]                    ; DS:[tile_y*4 - 0x52AE]
 *   tile_id     = row_far_ptr[tile_x*2]                ; WORD из карты
 *   ; tile_id * 40 = смещение в map_buffer
 *   map_addr   = map_buffer + tile_id * MAP_COLUMNS
 *   ; → копирование 40-байтной строки тайла в видеопамять (CGA/EGA-зависимо)
 *
 * Эта функция — единственный источник содержимого тайлов в видеопамяти.
 * SCROLLVIEW дёргает её по координатам тайлов, попавших на «раскрытый» край.
 */
void draw_tile(u16 tile_x, u16 tile_y) {
    if (level_mode != 0 && level_mode != 2) return;

    /* В оригинале — far ptr из tile-pointer-таблицы. */
    /* extern u8** tile_pointers; // DS:[bx*4 - 0x52AE]  */

    /* Достаём идентификатор тайла из карты. */
    /* u16 tile_id = tile_pointers[tile_y][tile_x]; */
    u16 tile_id = 0;                                    /* TODO */

    /* Каждый тайл хранится как 40 байт (= MAP_COLUMNS) в map_buffer. */
    /* const u8* tile_data = map_buffer + tile_id * MAP_COLUMNS; */
    (void)tile_id;
    /* TODO: cs:0x40BF — копирование в видеопамять (зависит от video_mode). */
}

/* ─── scroll_blit ─────────────────────────────────────────────────────
 * KARNOV.EXE: cs:0x5A3:0x09 (file 0x7239).
 *
 * Нижнеуровневая функция: копирует strip байт из source buffer
 * (DS = video_segment) в CGA framebuffer (ES = 0xB800).  Использует
 * фиксированные константы CGA — 40 байт на строку (0x28), 200 строк,
 * чередующиеся плоскости 0x0000/0x2000.
 *
 * Псевдокод (только mode 0; для mode 1/3 — иные обработчики на 0x7A37/0x7645):
 *   if (level_mode != 0): goto handler_mode_X
 *   if (scroll_counter < 0x186): goto handler_alt
 *   push_all_regs()
 *   es = 0xB800                              ; CGA
 *   ds = video_segment                       ; off-screen buffer
 *   src_offset = arg                         ; начальный байт
 *   dst_offset = 0x1E0                       ; верх видимой области
 *   ; цикл копирования 40 байт × N строк, с чередованием 0x0000/0x2000
 *   ; (это стандартный приём прокрутки CGA — переключение чётных/нечётных строк)
 */
void scroll_blit(u16 src_offset) {
    (void)src_offset;
    /* TODO: cs:0x7253 onwards — точное копирование в B800:0. */
}

/* ─── SCROLLUPDATE ──────────────────────────────────────────────────
 * KARNOV.EXE: cs:0x15F:0x195 (file 0x2F85).
 *
 *   ax = level_mode
 *   if (ax > 3) return                     ; защита от мусора
 *   jmp [cs:0x1A9 + ax*2]                   ; jump-table на 4 ветки
 *
 * Веток четыре (в файле — таблица на cs:0x1A9 = file 0x2F99):
 *
 *     [0] 0x01B1 → mode 0,2: push (level_mode + 0x8000); call scroll_blit
 *     [1] 0x01CE → mode 1:   push (video_offset + 0x4000); call scroll_blit
 *     [2] 0x01B1 → mode 2:   = ветка 0
 *     [3] 0x01C1 → mode 3:   push video_offset; call scroll_blit
 *
 * Старшие биты 0x8000/0x4000 — флаги «какую видеостраницу
 * использовать» (CGA имеет 2 страницы по 8000 байт).
 */
void scroll_update() {
    /* Соответствует cs:0x2F88..0x2FCD. */
    u16 mode = level_mode;
    if (mode > 3) return;

    switch (mode) {
    case 0:                                            /* cs:0x2FA1 path */
    case 2: {
        u16 arg = static_cast<u16>(mode | 0x8000);
        scroll_blit(arg);
        break;
    }
    case 1: {                                          /* cs:0x2FBE path */
        u16 arg = static_cast<u16>(video_offset + 0x4000);
        scroll_blit(arg);
        break;
    }
    case 3: {                                          /* cs:0x2FB1 path */
        scroll_blit(video_offset);
        break;
    }
    }
}

/* ─── SCROLLVIEW ─────────────────────────────────────────────────────
 * KARNOV.EXE: cs:0x15F:0x296 (file 0x3086).
 *
 * Перевод (бескомпромиссный — здесь самая сложная часть прокрутки):
 *
 *   delta_x = view_x - prev_view_x         ; [bp-0x24]
 *   delta_y = view_y - prev_view_y         ; [bp-0x22]
 *
 *   ; Сохранить текущую позицию как предыдущую (через буфер ABB8 в оригинале).
 *   pt_copy(&prev_tile_box, ABB8, &prev_view_x)   ; cs:0x30B1
 *   pt_copy(&cur_tile_box,  ABB8, &view_x)         ; cs:0x30C8
 *
 *   ; Конвертировать пиксельные координаты в тайловые через таблицы.
 *   prev_tx0 = px_to_tile_x[prev_view_x]              ; [bp-0x18]
 *   prev_ty0 = px_to_tile_y[prev_view_y]              ; [bp-0x10]
 *   prev_tx1 = px_to_tile_x[prev_tile_box[2] - 1] + 1 ; [bp-0x14]
 *   prev_ty1 = px_to_tile_y[prev_tile_box[3] - 1] + 1 ; [bp-0x0C]
 *   cur_tx0  = px_to_tile_x[cur_tile_box[0]]          ; [bp-0x16]
 *   cur_ty0  = px_to_tile_y[cur_tile_box[1]]          ; [bp-0x0E]
 *   cur_tx1  = px_to_tile_x[cur_tile_box[2] - 1] + 1  ; [bp-0x12]
 *   cur_ty1  = px_to_tile_y[cur_tile_box[3] - 1] + 1  ; [bp-0x0A]
 *
 *   ; -- ГОРИЗОНТАЛЬНЫЙ СКРОЛЛ -----------------------------------------
 *   if (delta_x == 0) goto vertical;
 *   if (delta_x < 0):              ; камера двинулась ВЛЕВО
 *       from_x  = prev_tx1         ; перерисовать правый край
 *       to_x    = cur_tx1
 *       seam    = prev_tile_box[2] ; правая граница предыдущего экрана
 *       width_y = cur_tile_box[3] - seam
 *   else:                          ; камера двинулась ВПРАВО
 *       from_x  = cur_tx0          ; перерисовать левый край
 *       to_x    = prev_tx0
 *       seam    = cur_tile_box[0]
 *       width_y = prev_tile_box[0] - seam
 *
 *   y_anchor = prev_tile_box[1]
 *   y_extent = prev_tile_box[3] - y_anchor
 *
 *   for (si = from_x; si < to_x; ++si):
 *       for (di = prev_ty0; di < prev_ty1; ++di):
 *           draw_tile(si, di)              ; cs:0x31A2
 *
 *   ; и финальный «инкремент-копирование» области (cs:0x31B6..0x31E5),
 *   ; который копирует обновлённые краевые тайлы из буфера в карту.
 *
 *   vertical:
 *   if (delta_y == 0) return
 *   ; симметричная логика по Y (не показана здесь — в оригинале это
 *   ; почти зеркальный блок 0x31E5..0x344?, который мы не выделяли).
 */
void scroll_view() {
    /* (1) Дельта камеры. */
    s16 delta_x = static_cast<s16>(view_x - prev_view_x);
    s16 delta_y = static_cast<s16>(view_y - prev_view_y);

    /* (2) Сохранить предыдущий bbox и текущий. */
    /* В оригинале prev_view_x / view_x — это начала смежных групп
     * по 4 WORD'а в DS (0xBA70..0xBA76 и 0xCC8E..0xCC94 соответственно).
     * Здесь — собираем стек-локалы вручную. */
    u16 src_prev[4] = { prev_view_x, prev_view_y, 0, 0 };
    u16 src_cur [4] = { view_x,      view_y,      0, 0 };
    pt_copy(prev_tile_box, reinterpret_cast<u16*>(pt_workspace), src_prev);
    pt_copy(cur_tile_box,  reinterpret_cast<u16*>(pt_workspace), src_cur);

    /* (3) Конвертация px → tile через lookup-таблицы.
     * В оригинале — обращения [bx*2 - 0x44A2] для X, [bx*2 - 0x50DE] для Y.
     * Прибавление +1 к правому/нижнему краю обеспечивает «включающий»
     * полу-открытый интервал. */
    u16 prev_tx0 = px_to_tile_x[prev_tile_box[0]];
    u16 prev_ty0 = px_to_tile_y[prev_tile_box[1]];
    u16 prev_tx1 = px_to_tile_x[(prev_tile_box[2] - 1) & 0xFFFF] + 1;
    u16 prev_ty1 = px_to_tile_y[(prev_tile_box[3] - 1) & 0xFFFF] + 1;
    u16 cur_tx0  = px_to_tile_x[cur_tile_box[0]];
    u16 cur_ty0  = px_to_tile_y[cur_tile_box[1]];
    u16 cur_tx1  = px_to_tile_x[(cur_tile_box[2] - 1) & 0xFFFF] + 1;
    u16 cur_ty1  = px_to_tile_y[(cur_tile_box[3] - 1) & 0xFFFF] + 1;

    /* (4) Горизонтальный край. */
    if (delta_x != 0) {
        u16 from_x, to_x;
        u16 seam_x, width_x;
        if (delta_x < 0) {
            /* Камера сдвинулась влево → раскрылся ПРАВЫЙ край. */
            from_x  = prev_tx1;
            to_x    = cur_tx1;
            seam_x  = prev_tile_box[2];
            width_x = static_cast<u16>(cur_tile_box[2] - seam_x);
        } else {
            /* Камера сдвинулась вправо → раскрылся ЛЕВЫЙ край. */
            from_x  = cur_tx0;
            to_x    = prev_tx0;
            seam_x  = cur_tile_box[0];
            width_x = static_cast<u16>(prev_tile_box[0] - seam_x);
        }

        /* (5) Перерисовать столбцы (от from_x до to_x-1)
         *     для каждой строки (от prev_ty0 до prev_ty1-1). */
        for (u16 sx = from_x; sx < to_x; ++sx) {
            for (u16 sy = prev_ty0; sy < prev_ty1; ++sy) {
                draw_tile(sx, sy);                  /* cs:0x31A2 */
            }
        }

        /* (6) Финальный inc-копирование (cs:0x31B6..0x31E5):
         *     отвечает за смещение области внутри карты, чтобы
         *     раскрывшийся столбец «приклеился» к остальной карте. */
        (void)width_x;
        (void)seam_x;
        /* TODO: точная буферная логика cs:0x31B6+ — копирует
         * (width_x × height) тайлов через scroll_blit. */
    }

    /* (7) Вертикальный край (симметричный блок). */
    if (delta_y != 0) {
        u16 from_y, to_y;
        if (delta_y < 0) {
            /* Камера вверх → раскрылся нижний край. */
            from_y = prev_ty1;
            to_y   = cur_ty1;
        } else {
            /* Камера вниз → раскрылся верхний край. */
            from_y = cur_ty0;
            to_y   = prev_ty0;
        }
        for (u16 sy = from_y; sy < to_y; ++sy) {
            for (u16 sx = prev_tx0; sx < prev_tx1; ++sx) {
                draw_tile(sx, sy);
            }
        }
        /* TODO: финальный inc-копирование для Y-направления. */
    }
}

} /* namespace karnov */
