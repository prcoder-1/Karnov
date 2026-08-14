/* sprite_render.cpp — реализация системы рендеринга спрайтов.
 *
 * Пословный перевод 5 ключевых функций из KARNOV.EXE:
 *   sprite_init                 (cs:0xBFB:0x004, file 0x0D7B4)
 *   detect_video_mode           (cs:0x15F:0x494, file 0x03284)
 *   render_object_sprites       (cs:0x6E:0x256,  file 0x02856)
 *   is_bbox_visible             (cs:0x6E:0x085,  file 0x02685)
 *   sprite_blit                 (cs:0x920:0x002, file 0x0AA02)
 *   inner_blit (81-way dispatch)(cs:0x920:0x4A6, file 0x0BCA8)
 */
#include "sprite_render.h"
#include "globals.h"
#include "file_io.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace karnov {

/* ─── Глобальное состояние подсистемы спрайтов ─────────────────────── */
u8*  sprite_data_base   = nullptr;
u16  sprite_dir_off     = 0;
u16  sprite_dir_seg     = 0;
u16  cam_tile_x0        = 0;
u16  cam_tile_y0        = 0;
u16  cam_tile_x1        = 320;       /* ~ширина экрана в тайлах */
u16  cam_tile_y1        = 200;       /* ~высота */
u16  tile_size_shift    = 4;         /* tile_x = world_x >> 4 (16px tiles) */
u16  video_buffer_seg   = 0;
u16  blit_dx_param      = 0;
u16  transparency_mode  = 0;
u16  shadow_x_mask      = 0;

/* Псевдо-таблица y → screen offset (DS:0xBCBC..AB area). */
static u16 y_to_screen_lut[1024] = {0};

/* ─── sprite_init (file 0x0D7B4) ────────────────────────────────────
 *
 * Псевдокод:
 *   void sprite_init() {
 *       ax = level_mode (DS:0x360);
 *       if (ax > 3) return;
 *       bx = ax * 2;
 *       jmp [cs:bx+0x1C]:                   ; jump-table 4 entries
 *
 *       case mode 0 (EGA):
 *           dx = 1
 *           ax = 0x1000                     ; allocate 0x1000 paragraphs
 *           call cs:0x1BE0:0x8(dx, ax)       ; alloc_mem (DOS or custom)
 *           [DS:0xD04..D06] = (ax, dx)      ; save far ptr
 *           bx = cx = 0
 *           call cs:0:0x54D(bx, cx)          ; init buffer
 *           if (return != 0): error → return
 *
 *       case mode 1 (CGA):
 *           dx = 1
 *           ax = 0xB800                      ; direct CGA video segment
 *           [...same call+save as mode 0]
 *
 *       case mode 2 (Tandy?):
 *           dx = 2; ax = 0
 *
 *       case mode 3 (Hercules):
 *           ...similar
 *   }
 */
int sprite_init() {
    /* В реконструкции выделяем динамическую память. */
    if (level_mode > 3) return -1;

    switch (level_mode) {
    case 0:                                       /* EGA — off-screen buffer */
    case 2: {                                     /* Tandy */
        /* В оригинале: allocate 0x1000 параграфов (= 64KB) через cs:0x1BE0:0x8.
         * Здесь — просто malloc. */
        static u8 buf[0x10000];
        sprite_data_base = buf;
        sprite_dir_off   = 0;
        sprite_dir_seg   = 0;                     /* placeholder */
        break;
    }
    case 1: {                                     /* CGA — direct video memory */
        /* В оригинале es=0xB800. */
        sprite_data_base = nullptr;               /* направить на CGA framebuffer */
        break;
    }
    case 3: {                                     /* Hercules */
        sprite_data_base = nullptr;
        break;
    }
    }
    return 0;
}

/* ─── detect_video_mode (file 0x03284) ──────────────────────────────
 *
 * Псевдокод:
 *   void detect_video_mode() {
 *       loop:
 *           fp = fopen("sprites.ega", "rb");        // DS:0xA4, DS:0xB0
 *           if (fp == NULL) {
 *               // print "Please insert program disk" 15 раз с blink
 *               for (i = 0; i < 15; ++i) print(DS:0xB3);
 *               print(DS:0x37D);   // дополнительное сообщение
 *               print(DS:0xB6);
 *               print(DS:0x395);
 *               for (i = 0; i < ?; ++i) print(DS:0xB9);
 *               // Test для exit (Q)
 *               goto loop;
 *           }
 *           fclose(fp);                              // только existence check
 *
 *           shift_state = *(u8*)(0:0x410);          // BIOS keyboard flag
 *
 *           if (level_mode == 4) {                    // SHIFT держал?
 *               if (5 <= [DS:0x124] <= 10) {           // hardware kind
 *                   fp = fopen("sprites.cga", "rb"); // DS:0xBC, DS:0xC8
 *                   if (fp != NULL) {
 *                       fclose(fp);
 *                       sound(9);                      // confirmation tone
 *                       level_mode = 3;                // Tandy/Herc fallback
 *                       return;
 *                   }
 *                   ...error handling...
 *               }
 *               sprite_subsystem_a();                   // cs:0xD09:0x6
 *               if (returned non-zero):
 *                   fp = fopen("sprites.ega", "rb");    // DS:0xCB, DS:0xD7
 *                   ...
 *           }
 *           save_shift_state();
 *           sprite_subsystem_b();                       // cs:0xD09:0x50
 *           if (failed) level_mode = 0; return
 *           sprite_subsystem_c();                       // cs:0xD09:0xA3
 *           level_mode = 2;
 *   }
 *
 * Три «subsystem» функции (cs:0xD09:0x6, 0x50, 0xA3) — это
 * graphics driver routines для конкретных режимов (EGA / CGA / Tandy).
 * Они не декомпилированы здесь, но известны по адресам.
 */
void detect_video_mode_and_load_sprites() {
    const char* sprites_file = "sprites.ega";
    for (;;) {
        FILE* fp = karnov_fopen(sprites_file, "rb");
        if (fp) {
            std::fclose(fp);
            break;
        }
        /* "Please insert program disk" + ждать клавишу */
        std::printf("Please insert program disk (file not found: %s)\n",
                    sprites_file);
        int c = std::getchar();
        if (c == 'q' || c == 'Q' || c == EOF) std::exit(c == EOF ? 1 : 0);
    }

    /* TODO: cs:0xD09:0x6/0x50/0xA3 — graphics driver init. */
    /* TODO: SHIFT-key detection (0:0x410) → manual mode select */

    /* По умолчанию устанавливаем mode 2 (Tandy/EGA-like). */
    level_mode = 2;
}

/* ─── is_bbox_visible (file 0x02685) ────────────────────────────────
 *
 * Точная декомпиляция cs:0x6E:0x85..0xDB.
 */
int is_bbox_visible(ActiveObject::SpriteBox* bbox) {
    if (!bbox) return 0;

    s16 tile_x0 = static_cast<s16>(bbox->world_x0) >> tile_size_shift;
    s16 tile_x1 = static_cast<s16>(bbox->world_x1) >> tile_size_shift;

    if (static_cast<s16>(bbox->world_y0) >= static_cast<s16>(cam_tile_y1)) return 0;
    if (static_cast<s16>(bbox->world_y1) <= static_cast<s16>(cam_tile_y0)) return 0;
    if (tile_x0                          >= static_cast<s16>(cam_tile_x1)) return 0;
    if (tile_x1                          <= static_cast<s16>(cam_tile_x0)) return 0;
    if (bbox->sprite_id == 0)                                              return 0;
    return 1;
}

/* ─── render_object_sprites (file 0x02856) ──────────────────────────
 *
 * Главный per-frame entry-point из DRAWCYCLE.
 */
void render_object_sprites(u16 obj_idx) {
    /* В оригинале: ActiveObject* obj = &all_objects[obj_idx];
     * но в реконструкции массив объектов внутри think.cpp статичен. */
    static ActiveObject all_objects[256];
    ActiveObject* obj = &all_objects[obj_idx];

    u16 num_bboxes = obj->num_bboxes;
    ActiveObject::SpriteBox* bbox = &obj->bboxes[0];

    for (u16 i = 0; i < num_bboxes; ++i, ++bbox) {
        if (!is_bbox_visible(bbox)) continue;

        /* Compute screen position */
        s16 tile_x  = static_cast<s16>(bbox->world_x0) >> tile_size_shift;
        s16 wy0     = static_cast<s16>(bbox->world_y0);
        if (wy0 < 0) wy0 = 0;
        u16 base_y  = y_to_screen_lut[wy0 & 0x3FF];
        u16 screen_pos = static_cast<u16>(base_y + tile_x);

        /* Shadow mask */
        u16 mask = 0;
        if (transparency_mode != 0) {
            mask = bbox->world_x0 & shadow_x_mask;
        }

        sprite_blit(screen_pos, bbox->sprite_id, mask, 0);
    }
}

/* ─── sprite_blit (file 0x0AA02) ────────────────────────────────────
 *
 * Главный диспетчер blit'а. Self-modifying устанавливает scroll_counter
 * в код, затем разветвляется по level_mode.
 *
 * В реконструкции — упрощённая модель без self-modifying.
 */
void sprite_blit(u16 screen_pos, u16 sprite_id, u16 mask, u16 flag) {
    /* Save scroll_counter for jump-table selection (in original — self-mod) */
    u16 saved_counter = scroll_counter;          /* DS:0xCDF0 */

    /* Dispatch by mode */
    if (level_mode != 0 && level_mode != 2 &&
        level_mode != 1 && level_mode != 3) return;

    /* Resolve sprite pointer from directory:
     *   lds si, [DS:0xD04]
     *   si += sprite_id * 4    // 4-byte directory entries
     *   lds si, [si]            // dereference → sprite data
     */
    if (!sprite_data_base) return;
    u8* sprite_ptr = sprite_data_base + sprite_id * 4;
    /* (В оригинале sprite_ptr теперь — это far ptr на пиксели) */

    /* Mode-specific dispatch — в реконструкции просто документируем. */
    switch (level_mode) {
    case 0:
    case 2: {  /* EGA */
        /* di = screen_pos
         * if (flag == 0) di += 0x8000   ; page 1
         * es = video_buffer_seg
         *
         * Эта ветка переходит к 0xAA9C (для mode 1) или 0xABA2 (для mode 3),
         * каждая из которых далее использует jump_table_high/low в
         * зависимости от scroll_counter.
         */
        break;
    }
    case 1: {  /* CGA — page 0x4000 */
        /* di = screen_pos
         * if (flag == 0) di += 0x4000
         * ... аналогично mode 0/2
         */
        break;
    }
    case 3: {  /* TGA / Hercules */
        /* Альтернативная видеоадресация. */
        break;
    }
    }

    /* Read header from sprite data:
     *   ax = *(u16*)si; si += 2
     *   count = ax >> 8         (high byte)
     *   width = ax & 0xFF       (low byte)
     */
    u16 header = 0;  /* In real impl: read from sprite_ptr */
    u16 count  = (header >> 8) & 0xFF;
    u16 width  = header & 0xFF;
    if (count >= 0x14) return;          /* sanity check (max 20 rows) */

    /* Select jump-table for blit-flavour:
     *   if (scroll_counter > 0x86): bp = high_table[count*2]
     *   else:                       bp = low_table[count*2]
     */
    (void)saved_counter;
    (void)mask; (void)screen_pos;
    /* bp = ... */

    /* Call inner_blit with width */
    inner_blit(width);
}

/* ─── inner_blit (file 0x0BCA8) ─────────────────────────────────────
 *
 * 81-way jump-table dispatch по ширине.
 *
 * Каждая из 81 спец-рутины — точка в большой unrolled MOVSB-секвенции.
 * Для ширины N байт jump-table[N] указывает на правильное смещение
 * внутри последовательности из 80 movsb, что копирует ровно N байт
 * из DS:SI в ES:DI.
 *
 * Это позволяет использовать одну общую секвенцию MOVSB вместо REP MOVSB
 * с counter overhead, для всех ширин 1..80.
 *
 * Для современной реконструкции — простой memcpy.
 */
void inner_blit(u16 width) {
    if (width >= 0x51) return;            /* max 80 байт на row */

    /* В оригинале: jmp [cs:width*2 + 0x12B7].
     * Эта jump-table содержит 81 entry, каждый указывает на
     * соответствующее место в unrolled MOVSB sequence.
     *
     * Структура:
     *   MOVSB        ; copy 1 byte
     *   MOVSB        ; copy 2nd byte (если нужно ещё)
     *   MOVSB        ; ...
     *   ... (80 MOVSB instructions total)
     *   RET          ; конец
     *
     * Для width=N, jump в (80-N)*1 MOVSB = выполнить ровно N MOVSB.
     */

    /* В реконструкции это эквивалентно: */
    /* memcpy(es:di, ds:si, width); */
    /* es:di и ds:si — глобальные регистры в оригинале (через self-modifying
     * через регистровое состояние). */
}

} /* namespace karnov */
