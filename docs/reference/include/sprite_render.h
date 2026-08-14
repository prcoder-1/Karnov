/* sprite_render.h — система загрузки и рендеринга спрайтов.
 *
 * Декомпилировано из KARNOV.EXE:
 *   cs:0xBFB:0x004 (file 0x0D7B4)  sprite_init        — выделить video buffers
 *   cs:0x15F:0x494 (file 0x03284)  detect_video_mode  — open sprites.ega/cga
 *   cs:0x6E:0x0256 (file 0x02856)  render_object_sprites — итерация по bboxes
 *   cs:0x6E:0x0085 (file 0x02685)  is_bbox_visible    — camera bounds check
 *   cs:0x920:0x002 (file 0x0AA02)  sprite_blit        — диспетчер по mode
 *   cs:0x920:0x4A6 (file 0x0BCA8)  inner_blit         — 81-way per-width dispatch
 *
 * АРХИТЕКТУРА (резюме):
 *
 *  ┌──────────────┐
 *  │ KARNOV.EXE   │ ┌─────────────────┐
 *  │ startup      │→│ detect_video_mode  ────┐
 *  └──────────────┘ │  (открывает sprites.ega│
 *                   │   с retry "insert disk"│
 *                   │   — выбирает EGA/CGA/  │
 *                   │   Tandy/Herc)          │
 *                   └────────────────────────┘
 *                            │
 *                            ↓
 *                   ┌─────────────────┐
 *                   │ sprite_init      │
 *                   │ выделяет видеобуфер
 *                   │ DS:0xD04..D06 = ptr
 *                   └─────────────────┘
 *                            │
 *                            ↓ in-memory layout:
 *                  DS:0xD04 → sprite_dir_ptr  (FAR)
 *                              ↓
 *                  [338 × 4-byte FAR pointers]
 *                              ↓
 *                  sprite_dir[N] → pixel data (per sprite)
 *
 *  Per frame (DRAWCYCLE phase 9):
 *  ┌─────────────────────────┐
 *  │ DRAWCYCLE                │
 *  │ for each active obj:     │
 *  │   render_object_sprites()│
 *  └─────────────────────────┘
 *           │
 *           ↓
 *  ┌─────────────────────────┐
 *  │ render_object_sprites    │
 *  │ for each bbox in obj:    │
 *  │   if is_bbox_visible():  │
 *  │     sprite_blit(...)     │
 *  └─────────────────────────┘
 *           │
 *           ↓
 *  ┌─────────────────────────┐
 *  │ sprite_blit               │
 *  │ dispatch by level_mode:  │
 *  │   0,2 = EGA              │
 *  │   1   = CGA              │
 *  │   3   = TGA/Herc         │
 *  │ - resolve sprite ptr     │
 *  │   from DS:0xD04 table     │
 *  │ - load width             │
 *  │ - call inner_blit        │
 *  └─────────────────────────┘
 *           │
 *           ↓
 *  ┌─────────────────────────┐
 *  │ inner_blit                │
 *  │ jmp [cs:width*2+0x12B7]  │
 *  │ 81 spec-routines (per w) │
 *  │ — unrolled MOVSB sequences│
 *  └─────────────────────────┘
 *           │
 *           ↓ writes to video memory
 *           ↓ (CGA: ES=B800, EGA: through 0x3CE port etc.)
 */
#ifndef KARNOV_SPRITE_RENDER_H
#define KARNOV_SPRITE_RENDER_H

#include "karnov.h"
#include "think.h"

namespace karnov {

/* ─── Глобальные переменные подсистемы спрайтов ─────────────────────── */

/* DS:0xD04..0xD07 — far ptr к таблице 4-байтных far ptr-указателей
 * на пиксельные данные спрайтов. Загружается sprite_init. */
extern u8* sprite_data_base;

/* DS:0xCDF0 — пер-кадровый счётчик (используется sprite_blit для выбора
 * специализированной jump-таблицы). */
/* extern u16 scroll_counter; — уже в globals.h */

/* DS:0xD04, DS:0xD06 — far ptr (offset, segment) к sprite directory. */
extern u16 sprite_dir_off;
extern u16 sprite_dir_seg;

/* DS:0xB834..0xB83B — границы камеры (видимая область) */
extern u16 cam_tile_x0;     /* DS:0xB834 */
extern u16 cam_tile_y0;     /* DS:0xB836 */
extern u16 cam_tile_x1;     /* DS:0xB838 */
extern u16 cam_tile_y1;     /* DS:0xB83A */

/* DS:0xB828 — tile_size_shift (sar count для преобразования
 * world_x в tile_x). Обычно 4 (= div by 16). */
extern u16 tile_size_shift;

/* DS:0xBA64 — сегмент off-screen видеобуфера (целевая память для blit). */
extern u16 video_buffer_seg;

/* DS:0xD76E — некий dx-параметр для blit (вероятно width-в-байтах). */
extern u16 blit_dx_param;

/* DS:0xA2A4 — флаг прозрачности/shadow mode. */
extern u16 transparency_mode;

/* DS:0xAF1E — маска X-координаты для shadow mode. */
extern u16 shadow_x_mask;

/* ─── Функции ───────────────────────────────────────────────────────── */

/* KARNOV.EXE: cs:0xBFB:0x04 (file 0x0D7B4).
 *
 * Выделяет видеобуферы в зависимости от level_mode:
 *   level_mode 0/2 (EGA):   allocate 0x1000 paragraphs → DS:0xD04..0xD06
 *   level_mode 1 (CGA):     point to 0xB800 (CGA video)
 *   level_mode 3 (Herc):    allocate 0x0?  → ...
 *
 * Затем вызывает cs:0:0x54D — функция, прoверяющая загруженные данные.
 */
int sprite_init();

/* KARNOV.EXE: cs:0x15F:0x494 (file 0x03284).
 *
 * Пробует открыть `sprites.ega`. При неудаче — цикл «please insert disk»
 * с печатью 4 строк ошибки и blink-цикл. Также проверяет shift-state
 * (40h:10h) для принудительного выбора режима. Может переключиться на
 * `sprites.cga` или другой fallback.
 *
 * После успешной загрузки:
 *   - закрывает файл (просто проверял существование)
 *   - вызывает специфичную для режима подсистему чтения (cs:0xD09:0x50 etc.)
 *   - устанавливает [DS:0x360] (level_mode) в соответствующее значение
 */
void detect_video_mode_and_load_sprites();

/* KARNOV.EXE: cs:0x6E:0x256 (file 0x02856).
 *
 * Рисует ВСЕ видимые bbox'ы данного объекта.
 *
 * Псевдокод:
 *   void render_object_sprites(u16 obj_idx) {
 *       ActiveObject* obj = &all_objects[obj_idx];
 *       u16 num_bboxes = obj->num_bboxes;     // +0x1E
 *       SpriteBox* bbox = &obj->bboxes[0];     // +0x20
 *
 *       for (int i = 0; i < num_bboxes; ++i, ++bbox) {
 *           if (!is_bbox_visible(bbox)) continue;
 *
 *           // Compute screen position
 *           u16 tile_x = bbox->world_x0 >> tile_size_shift;
 *           u16 world_y = max(0, bbox->world_y0);
 *           u16 base_y = y_to_screen_lut[world_y * 2];
 *           u16 screen_pos = base_y + tile_x;
 *
 *           // Compute shadow mask if applicable
 *           u16 mask = 0;
 *           if (transparency_mode != 0) {
 *               mask = bbox->world_x0 & shadow_x_mask;
 *           }
 *
 *           // Blit
 *           sprite_blit(screen_pos, bbox->sprite_id, mask, 0);
 *       }
 *   }
 *
 * Stride 0x12 = sizeof(SpriteBox) = 18 байт.
 */
void render_object_sprites(u16 obj_idx);

/* KARNOV.EXE: cs:0x6E:0x85 (file 0x02685).
 *
 * Проверяет, виден ли bbox в текущей camera-области.
 *
 *   int is_bbox_visible(SpriteBox* bbox) {
 *       u16 tile_x0 = bbox->world_x0 >> tile_size_shift;
 *       u16 tile_x1 = bbox->world_x1 >> tile_size_shift;
 *
 *       if (bbox->world_y0 >= cam_tile_y1)  return 0;
 *       if (bbox->world_y1 <= cam_tile_y0)  return 0;
 *       if (tile_x0       >= cam_tile_x1)   return 0;
 *       if (tile_x1       <= cam_tile_x0)   return 0;
 *       if (bbox->sprite_id == 0)            return 0;
 *       return 1;
 *   }
 */
int is_bbox_visible(ActiveObject::SpriteBox* bbox);

/* KARNOV.EXE: cs:0x920:0x02 (file 0x0AA02).
 *
 * Главный диспетчер blit'а. Сохраняет все регистры, делает self-modifying
 * установку «текущий frame counter» в [cs:0], затем ветвится по level_mode.
 *
 * Псевдокод (для level_mode 0/2 — EGA):
 *
 *   void sprite_blit(u16 screen_pos, u16 sprite_id, u16 mask, u16 flag) {
 *       *(u16*)0   = scroll_counter;          // [cs:0] = scroll_counter
 *
 *       switch (level_mode):
 *         case 0, 2: goto blit_ega
 *         case 1:    goto blit_cga
 *         case 3:    goto blit_herc
 *         default:   return
 *
 *     blit_ega:
 *       dx = blit_dx_param          // DS:0xD76E
 *       es = video_buffer_seg        // DS:0xBA64
 *       di = screen_pos
 *       if (flag == 0): di += 0x8000   // page 1
 *
 *       // Resolve sprite address through directory
 *       lds si, [DS:0xD04]            // far ptr to sprite_dir
 *       si += sprite_id * 4           // 4-byte entries
 *       lds si, [si]                   // dereference → sprite data
 *       if (mask != 0) si += [si]      // shift by mask offset
 *       si += 2                        // skip header word
 *
 *       // Load header
 *       push bp
 *       ax = *((u16*)si)+; si += 2     // first WORD: high=count, low=width
 *       bx = ax & 0xFF00 >> 8          // bx = high byte
 *       if (bx >= 0x14) goto exit
 *       bx *= 2
 *
 *       // Select jump table by scroll_counter
 *       if (scroll_counter > 0x86):
 *           bp = jmp_table_high[bx]    // cs:0x2F9
 *       else:
 *           bp = jmp_table_low[bx]     // cs:0x204
 *
 *       dx -= bx
 *       bx = ax & 0xFF                 // low byte = width
 *       call inner_blit                 // bp = row-jmp-table, bx = width
 *       pop bp
 *   }
 */
void sprite_blit(u16 screen_pos, u16 sprite_id, u16 mask, u16 flag);

/* KARNOV.EXE: cs:0x920:0x4A6 (file 0x0BCA8).
 *
 * Inner blit с 81-сторонним диспетчером:
 *
 *   inner_blit(width = bx):
 *       if (width >= 0x51) return         // макс 80
 *       jmp [cs:width*2 + 0x12B7]          // 81 спец-рутин (по 2 байта на entry)
 *
 *       jump_table_entry[w] → точка в специализированной MOVSB-последовательности
 *       Каждая спец-рутина: unrolled sequence of MOVSB instructions
 *       (от width=1 до width=80), направленная в ES:DI из DS:SI
 *
 * Это классический приём 1980-х: вместо `rep movsb cx=N` для каждого
 * row используется ОДНА большая unrolled sequence из 80 movsb, и
 * jump в нужную середину для нужной ширины.
 *
 * Преимущества:
 *   • Нет overhead от REP-counter
 *   • Каждый movsb = 1 byte, blit оптимален
 * Недостаток:
 *   • 80 movsb = 80 байт + 81 × 2 = 242 байта jump-table = ~322 байт на routine
 *   • НО даёт ускорение в ~2× против rep movsb
 */
void inner_blit(u16 width);

} /* namespace karnov */

#endif
