/* think.h — структура активного объекта и AI think_cycle.
 *
 * Декомпилировано из KARNOV.EXE:
 *   cs:0xD4B:0xE03  (file 0x0FAB3)  THINKCYCLE
 *   cs:0xD4B:0xBD5  (file 0x0F885)  ai_dispatch (call far [obj+0xB7])
 *   cs:0xD4B:0xB6E  (file 0x0F81E)  check_collision
 *   cs:0x6E:0x4B4   (file 0x02394)  process_object_chain
 *   cs:0x6E:0x21C   (file 0x021DC)  update_world_bbox
 *   cs:0x6E:0x14E   (file 0x02106)  per_player_dispatch (called from update_players)
 */
#ifndef KARNOV_THINK_H
#define KARNOV_THINK_H

#include "karnov.h"

namespace karnov {

/* ─── Activeobject struct ──────────────────────────────────────────────
 *
 * Размер: 0xF5 = 245 байт. Хранятся как массив в DS:0x2824, индексируются
 * по uint16. Адресация: ptr = DS:(index * 0xF5 + 0x2824).
 *
 * Расшифровка полей выведена из:
 *   • cs:0x2106 — игрок проходит через эту структуру
 *   • cs:0x2394 — обход цепочки активных объектов (поля +0xB2/0xB4)
 *   • cs:0xF885 — диспетчер AI (поля +0xB6, 0xB7, 0xB9, 0xBD, 0xBF,
 *                +0xC5, 0xC7, 0xC9, 0xCB, 0xCD, 0xCF, 0xD5, 0xD9, 0xDB)
 *   • cs:0xFAB3 — THINKCYCLE (поля +0xCB, 0xDD, 0xE5, 0xE7)
 *
 * Все WORD'ы — little-endian, без выравнивания. AiHandler — far ptr.
 */
using AiHandler = void(*)();           /* в оригинале — far void(*)() */

struct ActiveObject {
    /* === Заголовок 0x00..0x1F === */
    u16  x_screen;      /* +0x00 — экранная X (вычислена в update_world_bbox) */
    u16  y_screen;      /* +0x02 — экранная Y */
    u8   _pad04[0x04];  /* +0x04..0x07 */
    u16  x_world;       /* +0x08 — мировая X */
    u16  _pad0A;        /* +0x0A */
    u16  y_world;       /* +0x0C — мировая Y */
    u8   _pad0E[0x10];  /* +0x0E..0x1D */
    u16  num_bboxes;    /* +0x1E — количество спрайт-боксов */

    /* === Массив боксов (+0x20..+0x9F, до 8 шт. по 18 байт каждый) === */
    struct SpriteBox {
        u16 sprite_id;  /* +0 — индекс спрайта; lookup [+0xD08], [+0xFAC] */
        u16 x_offset;   /* +2 — смещение от obj.x */
        u16 y_offset;   /* +4 — смещение от obj.y */
        u16 world_x0;   /* +6 — вычисляется в update_world_bbox */
        u16 world_y0;   /* +8 */
        u16 world_x1;   /* +0xA */
        u16 world_y1;   /* +0xC */
        u8  _pad[4];    /* +0xE..0x11 */
    };
    SpriteBox bboxes[8];                      /* +0x20..+0xAF (8 × 18 = 144) */

    /* === AI / state machine === */
    u16  alive_flag;    /* +0xB0 — flag «не убран в dead-list», cs:0x2394 */
    u16  next_idx;      /* +0xB2 — индекс следующего в active-list */
    u16  prev_idx;      /* +0xB4 — индекс предыдущего */
    u8   state_byte;    /* +0xB6 — текущее главное состояние (0..5) */
    /* +0xB7..+0xBA — FAR pointer на текущий AI handler:
     *    [+0xB7] = offset, [+0xB9] = segment.
     *    Вызывается через `call far [obj+0xB7]`. */
    u16  ai_handler_off;/* +0xB7 */
    u16  ai_handler_seg;/* +0xB9 */
    u16  _padBB;        /* +0xBB */
    u16  ai_init_off;   /* +0xBD — handler для перезагрузки на state==1 */
    u16  ai_init_seg;   /* +0xBF */
    u16  _padC1[2];     /* +0xC1..+0xC4 */
    u16  sub_timer;     /* +0xC5 — счётчик внутри state==2 (cs:0xF8AD) */
    u16  state_ext;     /* +0xC7 */
    u16  sub_timer_max; /* +0xC9 — макс. значение sub_timer */
    s16  collision_cnt; /* +0xCB — обратный таймер для AI think */
    u16  state_off;     /* +0xCD — текущий offset в state-таблице */
    u16  state_seg;     /* +0xCF — её сегмент */
    u16  _padD1[2];     /* +0xD1..+0xD4 */
    u16  state_list_off;/* +0xD5..+0xD8 — far ptr на массив StateScript */
    u16  state_list_seg;
    u16  next_state_id; /* +0xD9 — id перехода (см. сравнение с 0x3E7 в cs:0xF991) */
    u8   _padDB;        /* +0xDB */
    u16  active_flag;   /* +0xDB — флаг «активен» (cs:0xF8A2) */
    u16  _padDD;        /* +0xDD — сбрасывается в 0 в pair-loop */
    u16  _padDF[3];     /* +0xDF..+0xE4 */
    u16  obj_type;      /* +0xE5 — тип объекта (0..4 — диспетчер pair-loop) */
    u16  hit_points;    /* +0xE7 — HP, уменьшается в pair-loop при коллизии */
    u8   _padE9[0xC];   /* +0xE9..+0xF4 */
};
/* На GCC packing для 16-битного DOS — у нас порядок и смещения сохранены
 * вручную; sizeof должен совпадать. */
/* static_assert(sizeof(ActiveObject) == 245, "ActiveObject must be 245 bytes"); */

/* StateScript — запись в массиве "состояний" объекта.
 * Адресуется через obj.state_list_seg:obj.state_list_off + next_state_id * 8.
 * См. cs:0xF99F..0xF9C5 (умножение << 3 = ×8). */
struct StateScript {
    u16 anim_ptr_off;   /* +0 — указатель на анимацию/скрипт */
    u16 anim_ptr_seg;   /* +2 */
    u16 unknown_4;      /* +4 */
    u16 unknown_6;      /* +6 */
};

/* ─── Глобальное состояние AI ──────────────────────────────────────── */

/* DS:0x271E — количество слов в pair table (= 2 × числу пар). */
extern u16 num_pair_entries;

/* DS:0x2722 — таблица пар объектов (по 2 индекса на пару).
 * Используется в pair-loop THINKCYCLE для проверки коллизий. */
extern u16 pair_table[];

/* DS:0x2594 — массив индексов активных объектов (используется
 * update_players и AI think; одиночная цепочка). Размер ≈ 128. */
extern u16 active_index[];

/* DS:0x2694 — счётчики активных объектов по слоту (max 2 — игроки). */
extern u16 active_count[2];

/* DS:0x2698 — побочный массив для управления dead-list (cs:0x23DF). */
extern u16 dead_chain[];

/* DS:0xA2AE — счётчик dead-list (cs:0x23D0). */
extern u16 dead_count;

/* DS:0xD8A6 / 0xD8A8 — far ptr на "текущий обрабатываемый объект"
 * (current_obj в AI dispatcher). */
extern ActiveObject* current_obj;

/* DS:0xD8A2 — кэш state_byte для текущего объекта (cs:0xF8A2). */
extern u16 current_state_byte;

/* DS:0xD963 — результат AI handler:
 *   bit 0 → объект приостановлен, сохранить в revival list (cs:0xF95E)
 *   bit 1 → переход в новое состояние (cs:0xF986)
 */
extern u16 ai_result;

/* DS:0xD953 — счётчик сохранённых на revival объектов в текущем кадре. */
extern u16 revival_count;

/* DS:0xD780+ — массив индексов на revival (cs:0xF975). */
extern u16 revival_list[];

/* DS:0xD88C — целевое состояние для отложенного перехода (cs:0xF991). */
extern u16 pending_state_id;

/* DS:0xD8BC, 0xD8BE — кэш player.x / player.y для AI запросов. */
extern u16 player_cache_x, player_cache_y;

/* DS:0xD8A0, 0xD8A4 — копия player_x и флаг. */
extern u16 player_x_a0, player_flag_a4;

/* DS:0xD955 — пер-кадровый счётчик таймера (cs:0x10142). */
extern u16 frame_timer;

/* DS:0xD959 — индекс «выбранного цели» (cs:0xFC64). */
extern u16 target_idx;

/* DS:0xD8CE — повторный CMP в pair-loop type-0 (level == 6 для damage). */
/* (уже объявлен в globals.h как max_level_reached) */

/* DS:0xCCA6 — флаг неуязвимости / «god mode» (cs:0xFB8A). */
extern u16 god_mode;

/* ─── Функции ───────────────────────────────────────────────────────── */

/* KARNOV.EXE: cs:0xD4B:0xE03 (file 0x0FAB3). */
void think_cycle();

/* KARNOV.EXE: cs:0xD4B:0xBD5 (file 0x0F885). */
void ai_dispatch(ActiveObject* obj);

/* KARNOV.EXE: cs:0xD4B:0xB6E (file 0x0F81E). */
int check_collision(ActiveObject* a, ActiveObject* b);

/* KARNOV.EXE: cs:0xD4B:0x1124. Звуковой эффект по индексу. */
void play_sound(u16 sound_id);

/* KARNOV.EXE: cs:0x6E:0x21C (file 0x021DC). */
void update_world_bboxes(ActiveObject* obj);

} /* namespace karnov */

#endif
