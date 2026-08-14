/* think.cpp — THINKCYCLE: фаза AI кадрового цикла.
 *
 * Полное пословное декомпилирование функции cs:0xD4B:0xE03 (file 0xFAB3)
 * и связанных AI-функций. См. think.h для расшифровки полей структуры
 * ActiveObject и адресов глобального состояния.
 *
 * Архитектура AI (резюме):
 *   1. THINKCYCLE состоит из двух фаз:
 *      A. pair_phase()  — итерирует таблицу пар DS:0x2722, для каждой пары
 *                          (obj_a, obj_b) вызывает диспетчер по obj_a.type.
 *                          Это коллизия + урон.
 *      B. player_phase() — для каждого из 2 игроков обходит цепочку их
 *                          активных объектов и вызывает ai_dispatch для
 *                          каждого, у которого «думающий» флаг.
 *   2. Каждый объект — конечный автомат с FAR-функцией-обработчиком в
 *      поле +0xB7/+0xB9 и опциональной «инициализирующей» функцией
 *      в +0xBD/+0xBF.
 *   3. Состояние объекта (поле state_byte +0xB6) принимает значения 0..5:
 *        0 — новый, требуется инициализация (загрузка из +0xBD/+0xBF)
 *        1 — реинициализация (та же логика, плюс перезагрузка handler)
 *        2 — активен, тикает sub_timer
 *        3,4,5 — другие активности
 *   4. AI handler возвращает результат через DS:0xD963:
 *        bit 0 → приостановить, добавить в revival list (DS:0xD780)
 *        bit 1 → переход в новое состояние (по +0xD9 и таблице +0xD5)
 */
#include "think.h"
#include "globals.h"
#include <cstring>

namespace karnov {

/* Массив активных объектов (DS:0x2824). Размер: ~256 объектов × 245 байт. */
static ActiveObject all_objects[256];

/* Преобразование индекса в указатель — соответствует:
 *    bx = idx * 0xF5 + 0x2824
 * в оригинале. */
static inline ActiveObject* obj_at(u16 idx) {
    return &all_objects[idx];
}

/* ─── Глобальное состояние AI (вынесено из globals.cpp) ───────────────── */
u16 num_pair_entries  = 0;     /* DS:0x271E */
u16 pair_table[256]   = {0};   /* DS:0x2722 */
u16 active_index[256] = {0};   /* DS:0x2594 */
u16 active_count[2]   = {0,0}; /* DS:0x2694 */
u16 dead_chain[256]   = {0};   /* DS:0x2698 */
u16 dead_count        = 0;     /* DS:0xA2AE */
ActiveObject* current_obj    = nullptr; /* DS:0xD8A6 */
u16 current_state_byte = 0;             /* DS:0xD8A2 */
u16 ai_result         = 0;     /* DS:0xD963 */
u16 revival_count     = 0;     /* DS:0xD953 */
u16 revival_list[128] = {0};   /* DS:0xD780 */
u16 pending_state_id  = 0;     /* DS:0xD88C */
u16 player_cache_x    = 0;     /* DS:0xD8BC */
u16 player_cache_y    = 0;     /* DS:0xD8BE */
u16 player_x_a0       = 0;     /* DS:0xD8A0 */
u16 player_flag_a4    = 0;     /* DS:0xD8A4 */
u16 frame_timer       = 0x46;  /* DS:0xD955 */
u16 target_idx        = 0xFFFF;/* DS:0xD959 */
u16 god_mode          = 0;     /* DS:0xCCA6 */

/* ─── Pair-loop type-0 sub-dispatch ──────────────────────────────────── */

/* KARNOV.EXE: cs:0xD4B:0xB6E (file 0xF81E). Проверка пересечения
 * bbox'ов obj_a и obj_b. */
int check_collision(ActiveObject* a, ActiveObject* b) {
    /* TODO: полная декомпиляция функции cs:0xF81E. Логика:
     *   • для каждого bbox в a (a->num_bboxes штук):
     *       для каждого bbox в b:
     *           если a->bbox.world_x0/y0/x1/y1 пересекается с b->bbox:
     *               вернуть 1
     *   • вернуть 0
     * Возвращает 1 при пересечении. */
    if (!a || !b) return 0;
    for (u16 i = 0; i < a->num_bboxes; ++i) {
        const auto& ba = a->bboxes[i];
        for (u16 j = 0; j < b->num_bboxes; ++j) {
            const auto& bb = b->bboxes[j];
            if (ba.world_x0 < bb.world_x1 && ba.world_x1 > bb.world_x0 &&
                ba.world_y0 < bb.world_y1 && ba.world_y1 > bb.world_y0) {
                return 1;
            }
        }
    }
    return 0;
}

/* KARNOV.EXE: cs:0xD4B:0x1124. */
void play_sound(u16 sound_id) {
    /* TODO: cs:0xD4B:0x1124 — пишет в PIT/PC speaker. */
    (void)sound_id;
}

/* KARNOV.EXE: cs:0x6E:0x21C (file 0x21DC). */
void update_world_bboxes(ActiveObject* obj) {
    /* Полный код декомпилирован в дизассемблировании 0x21DC..0x24DA:
     *   for each bbox i in 0..num_bboxes-1:
     *      bbox[i].world_x0 = obj.x + bbox[i].x_offset
     *      bbox[i].world_x1 = bbox[i].world_x0 + sprite_width[sprite_id]
     *      bbox[i].world_y0 = obj.y + bbox[i].y_offset
     *      bbox[i].world_y1 = bbox[i].world_y0 + sprite_height[sprite_id]
     * sprite_width — таблица DS:0xD08, sprite_height — DS:0xFAC. */
    if (!obj) return;
    for (u16 i = 0; i < obj->num_bboxes; ++i) {
        auto& bb = obj->bboxes[i];
        /* В оригинале — байтовые таблицы sprite_dim[]. Здесь — заглушка. */
        /* extern u8 sprite_width[]; extern u8 sprite_height[]; */
        bb.world_x0 = obj->x_world + bb.x_offset;
        bb.world_x1 = bb.world_x0 + 16;                   /* sprite_width[bb.sprite_id]; */
        bb.world_y0 = obj->y_world + bb.y_offset;
        bb.world_y1 = bb.world_y0 + 16;                   /* sprite_height[bb.sprite_id]; */
    }
}

/* ─── Главная функция: THINKCYCLE ─────────────────────────────────────
 * KARNOV.EXE: cs:0xD4B:0xE03 (file 0xFAB3).
 *
 * Пословный перевод:
 *
 *   revival_count = 0
 *   for (si = 0; si < num_pair_entries; si += 2):
 *       a = &all_objects[pair_table[si]]
 *       b = &all_objects[pair_table[si+1]]
 *       a->collision_cnt = 0          ; +0xCB clear (актуальный bug в orig?)
 *       b->collision_cnt = 0
 *       if (a->obj_type > 4) continue ; типы 5+ не интерактивны в этой фазе
 *       switch (a->obj_type) {
 *       case 0:
 *           if (max_level_reached != 6) continue
 *           sub = b->obj_type - 1
 *           if (sub > 8) goto reset_DD
 *           switch (sub):                 ; sub-dispatch таблица DS:0xEB8
 *             case 0 (b.type 1): case 6 (b.type 7):
 *                  if (god_mode) { b->_padDD = 0; continue }
 *                  if (check_collision(a,b)) {
 *                       a->state_byte = 4    ; "hurt"
 *                       play_sound(2)
 *                  }
 *                  goto reset_DD
 *             case 2: b->state_byte = 4
 *             case 4: b->state_byte = 4
 *             case 8: b->state_byte = 5
 *             default: ; no-op
 *       case 1:                            ; no-op
 *       case 2:                            ; ID 2 type — попадание игрока
 *           sub2 = b->obj_type - 1
 *           if (sub2 > 8) goto reset_DD
 *           switch (sub2):
 *             case 0 (b.type 1):           ; обычный враг
 *               b->state_byte = 5
 *               atk = a->hit_points
 *               a->hit_points -= atk
 *               b->hit_points -= atk
 *               if b->hit_points <= 0:
 *                   if max_level_reached != 9:
 *                       b->state_byte = 4
 *                       play_sound(1)
 *                   ... bonus checks ...
 *               else:
 *                   b->_padDD = 0
 *               if max_level_reached == 9 || a->hit_points < 0:
 *                   a->state_byte = 4
 *             case 1: case 3: case 5:      ; разные типы враждебных
 *               ... (см. table DS:0xF56 → разные обработчики)
 *             ...
 *       case 3:                            ; no-op
 *       case 4:                            ; (см. 0xFCD0 — общий "очистить _padDD")
 *
 *       reset_DD:
 *           a->_padDD = 0
 *           b->_padDD = 0
 *
 *   ; Фаза 2: per-player AI think
 *   for (si = 0; si < 2; ++si):
 *       u16 combined_idx = active_count[si] * 2 + si
 *       for (di = si; di < combined_idx; ++di):
 *           u16 obj_idx = active_index[di]
 *           current_obj = &all_objects[obj_idx]
 *           if (current_obj->ai_handler_off | current_obj->ai_handler_seg) != 0:
 *               --current_obj->collision_cnt
 *               if collision_cnt > 0 AND state_byte == 2: continue
 *               current_obj->collision_cnt = current_obj->sub_timer_max
 *               current_state_byte = current_obj->state_byte
 *               ai_dispatch(current_obj)
 *           if si != 0:
 *               if obj_idx != target_idx:
 *                   ... distance/range checks ...
 *
 *   ; Фаза 3: процессинг revival list
 *   for (si = 0; si < revival_count; ++si):
 *       cs:0x6E:0x226(revival_list[si])    ; revive_object
 */
void think_cycle() {
    revival_count = 0;

    /* ─── ФАЗА A: pair-loop коллизий ─────────────────────────────────── */
    for (u16 si = 0; si + 1 < num_pair_entries; si += 2) {
        u16 a_idx = pair_table[si];
        u16 b_idx = pair_table[si + 1];
        ActiveObject* a = obj_at(a_idx);
        ActiveObject* b = obj_at(b_idx);

        a->collision_cnt = 0;       /* +0xCB */
        b->collision_cnt = 0;

        u16 a_type = a->obj_type;   /* +0xE5 */
        if (a_type > 4) {
            /* Внутри цикла: переход прямо на reset_DD в конце. */
            a->_padDD = 0;
            b->_padDD = 0;
            continue;
        }

        /* Главный jump table: cs:0xE8A (5 веток). */
        switch (a_type) {

        /* === Type 0 — окружающие объекты (только уровень 6) === */
        case 0: {
            if (max_level_reached != 6) break;          /* CMP word [0xD8CE],6 в cs:0xFB44 */

            u16 b_type = b->obj_type;
            if (b_type == 0 || b_type - 1 > 8) break;
            switch (b_type - 1) {                       /* sub-table cs:0xEB8 */
            case 0:                                     /* b.type == 1 */
            case 6:                                     /* b.type == 7 */
                /* "Hurt player" — общая ветка для обоих сабтипов. */
                if (god_mode) {
                    b->_padDD = 0;
                    break;
                }
                if (check_collision(a, b)) {
                    if (!god_mode) {
                        a->state_byte = 4;              /* +0xB6 = 4 — hurt */
                        play_sound(2);
                    }
                }
                break;
            case 2:                                     /* b.type == 3 */
            case 4:                                     /* b.type == 5 */
                b->state_byte = 4;
                break;
            case 8:                                     /* b.type == 9 */
                b->state_byte = 5;
                break;
            default:                                    /* 1,3,5,7 — no-op */
                break;
            }
            break;
        }

        /* === Type 1, 3 — пропускаются (jump table → continue) === */
        case 1:
        case 3:
            break;

        /* === Type 2 — урон от удара игрока (jump table → cs:0xFCEC) === */
        case 2: {
            /* Кэш HP перед вычитанием. */
            u16 atk = a->hit_points;                    /* +0xE7 */

            u16 b_type = b->obj_type;
            if (b_type == 0 || b_type - 1 > 8) {
                /* Сброс _padDD. */
                a->_padDD = 0;
                b->_padDD = 0;
                break;
            }
            /* Второй sub-table: cs:0xF56 (9 веток).  Большинство → cs:0xFCD0
             * (общий reset).  Интересные ветки:
             *   sub == 0 (b.type==1) → cs:0xFC21 — damage логика
             *   sub == 6 (b.type==7) → cs:0xFC18 — sub-type 7 logic
             *   sub == 8 (b.type==9) → cs:0xFC18 same
             */
            switch (b_type - 1) {
            case 0: {                                   /* b.type == 1 */
                b->state_byte = 5;                      /* CS:0xFC1B set +0xB6=5 */
                u16 a_e7 = a->hit_points;
                /* Вычитание HP для обоих (см. cs:0xFC2C). */
                a->hit_points -= a_e7;                  /* a.HP -= a.HP (т.е. =0?) */
                /* На самом деле в cs:0xFC2C: SUB [es:bx+0xE7], ax, где
                 * ax = [bp-2] (предыдущее значение a.HP). Если этот код
                 * декрементит обе HP на одно и то же — это «обмен» уроном. */
                b->hit_points -= a_e7;
                if ((s16)b->hit_points > 0) {
                    /* Враг ещё жив. */
                    b->_padDD = 0;
                } else {
                    /* Враг убит. */
                    if (max_level_reached != 9) {       /* CS:0xFC49 */
                        b->state_byte = 4;
                        play_sound(1);
                    }
                    /* Дополнительный bonus-check на уровнях ≠ 6,7. */
                    if (b_idx == target_idx &&
                        max_level_reached != 6 &&
                        max_level_reached != 7) {
                        /* cs:0xFC81: устанавливает флаг на ещё одну
                         * структуру по [DS:0xD94F], затем сбрасывает
                         * счётчик +0x2E на [DS:0xD95F]. */
                        /* TODO: revive flag setting */
                    }
                }
                /* Если игрок «погибает» (уровень 9 или HP < 0). */
                if (max_level_reached == 9 || (s16)a->hit_points < 0) {
                    a->state_byte = 4;
                }
                break;
            }
            case 6:                                     /* b.type == 7 */
            case 8:                                     /* b.type == 9 */
                /* CS:0xFC18: b.state_byte = 5. */
                b->state_byte = 5;
                break;
            default:
                break;
            }
            break;
        }

        /* === Type 4 (jump table → cs:0xFCD0) — общая ветка очистки === */
        case 4:
            /* В cs:0xFCD0 — просто сброс _padDD у обоих объектов. */
            break;

        default:
            break;
        }

        /* reset_DD: общий «хвост» цикла. */
        a->_padDD = 0;
        b->_padDD = 0;
    }

    /* ─── ФАЗА B: per-player AI think (cs:0xFCF0..0xFDA8) ─────────────── */
    for (u16 si = 0; si < 2; ++si) {
        u16 combined_count = active_count[si] * 2 + si; /* cs:0xFCFD */

        for (u16 di = si; di < combined_count; ++di) {
            u16 obj_idx = active_index[di];             /* cs:0xFD0D */
            current_obj = obj_at(obj_idx);

            /* Объект «думает» только если установлен AI handler (поля
             * +0xB7 / +0xB9). cs:0xFD28: ax = [+0xB7]; ax |= [+0xB9]. */
            if ((current_obj->ai_handler_off | current_obj->ai_handler_seg) != 0) {

                --current_obj->collision_cnt;            /* +0xCB-- (cs:0xFD38) */

                /* Если collision_cnt > 0 И state == 2, пропускаем. */
                if (current_obj->collision_cnt > 0 &&
                    current_obj->state_byte == 2) {
                    /* Skip think this frame. */
                } else {
                    /* Сбросить таймер коллизий до максимума +0xC9. */
                    current_obj->collision_cnt = (s16)current_obj->sub_timer_max;

                    /* Передать state в глобал для AI dispatcher. */
                    current_state_byte = current_obj->state_byte;

                    ai_dispatch(current_obj);
                }
            }

            /* si != 0 (только для второго игрока):
             *   проверка дистанции от player_cache_x до объекта (cs:0xFD7C). */
            if (si != 0) {
                if (obj_idx != target_idx) {
                    s16 dx = (s16)player_cache_x - (s16)current_obj->x_world;
                    if (dx < 125) {
                        /* TODO: cs:0xFD8C — какая-то проксимити-логика. */
                    }
                }
            }
        }
    }

    /* ─── ФАЗА C: revival list (cs:0xFDAD..0xFDCC) ───────────────────── */
    for (u16 si = 0; si < revival_count; ++si) {
        u16 obj_idx = revival_list[si];
        /* cs:0x6E:0x226 — revive_object(idx). */
        (void)obj_idx;
        /* TODO: revive_object(obj_idx); */
    }
}

/* ─── AI dispatcher ───────────────────────────────────────────────────
 * KARNOV.EXE: cs:0xD4B:0xBD5 (file 0xF885).
 *
 * Принимает current_obj (через глобал DS:0xD8A6).  Состояние объекта
 * перед вызовом передаётся в current_state_byte (DS:0xD8A2 = obj.state_byte).
 *
 * Действие:
 *   ai_result = 0
 *   pending_state_id = 0x3E7  (sentinel)
 *
 *   if (current_state_byte == 2):
 *       if (current_obj->active_flag != 0):
 *           ++current_obj->sub_timer
 *           if (sub_timer < *(WORD*)current_obj->state_off):
 *               // ещё не пора менять кадр
 *               goto call_handler
 *           current_obj->sub_timer = 0
 *       goto call_handler                     ; (вызов AI handler)
 *
 *   if (current_state_byte > 5): return
 *   switch (current_state_byte):              ; cs:0xC32 jump table
 *     case 0: goto init_state                 ; cs:0xF906
 *     case 1: goto reload_handler             ; cs:0xF8EE (перезагрузка из BD/BF)
 *     case 2: goto call_handler                ; cs:0xF945 (обычный путь)
 *     case 3: if state==1 goto reload          ; cs:0xF939
 *     case 4: goto call_handler                ; cs:0xF945
 *     case 5: goto reload                     ; cs:0xF939
 *
 *   reload (0xF8EE..0xF904):
 *       obj->ai_handler_off = obj->ai_init_off  ; +0xB7 ← +0xBD
 *       obj->ai_handler_seg = obj->ai_init_seg  ; +0xB9 ← +0xBF
 *       fall-through to init_state
 *
 *   init_state (0xF906..0xF937):
 *       obj->active_flag    = 1               ; +0xDB
 *       obj->collision_cnt  = 0               ; +0xCB
 *       obj->sub_timer_max  = 1               ; +0xC9
 *       obj->state_ext      = 0               ; +0xC7
 *       obj->next_state_id  = 0x3E7           ; +0xD9
 *       obj->state_byte     = 2               ; +0xB6
 *       fall-through to call_handler
 *
 *   call_handler (0xF945):
 *       call far [obj+0xB7]    // <— here be dragons (AI script)
 *
 *   После вызова: смотрим ai_result:
 *       if (ai_result & 1):   revival_list[revival_count++] = obj->next_idx
 *       if (ai_result & 2):
 *           if (obj->next_state_id != pending_state_id &&
 *               pending_state_id != 0x3E7):
 *               // Загрузить новое состояние из state_list:
 *               StateScript* s = &obj->state_list[pending_state_id];
 *               obj->state_off = s->anim_ptr_off
 *               obj->state_seg = s->anim_ptr_seg
 *               ... reset sub_timer & friends ...
 *               state_byte = 1   // следующий кадр перезагрузит handler
 */
void ai_dispatch(ActiveObject* obj) {
    if (!obj) return;
    ai_result = 0;
    pending_state_id = 0x3E7;

    if (current_state_byte == 2) {
        if (obj->active_flag != 0) {
            ++obj->sub_timer;
            /* В оригинале: cmp ax, [es:bx]  где es:bx = obj->state_off:state_seg.
             * Если sub_timer < *(WORD*)state_ptr — пропустить смену кадра. */
            u16* state_ptr = nullptr;   /* obj->state_seg:obj->state_off — не моделируем сегмент в C++ */
            if (state_ptr && obj->sub_timer < *state_ptr) {
                goto call_ai;
            }
            obj->sub_timer = 0;
        }
        goto call_ai;
    }

    if (current_state_byte > 5) return;

    /* switch (state_byte): jump-table из cs:0xC32 */
    switch (current_state_byte) {
    case 0:
        /* init_state branch — сразу установка полей. */
        break;
    case 1:
    case 3:
        /* Перезагрузка handler из +0xBD/+0xBF. */
        obj->ai_handler_off = obj->ai_init_off;
        obj->ai_handler_seg = obj->ai_init_seg;
        /* fall through to init_state */
        break;
    case 2:
    case 4:
    case 5:
        goto call_ai;
    }

    /* init_state (0xF906): */
    obj->active_flag      = 1;
    obj->collision_cnt    = 0;
    obj->sub_timer_max    = 1;
    obj->state_ext        = 0;
    obj->next_state_id    = 0x3E7;
    obj->state_byte       = 2;

call_ai:
    /* call far [obj+0xB7]. В реконструкции — обычный указатель. */
    if (obj->ai_handler_off | obj->ai_handler_seg) {
        AiHandler h = reinterpret_cast<AiHandler>(
            static_cast<std::uintptr_t>(obj->ai_handler_off) |
            (static_cast<std::uintptr_t>(obj->ai_handler_seg) << 16));
        /* В оригинале — far-call. На современной 32/64-битной платформе
         * 32 бита недостаточно для функционирующего указателя, поэтому
         * это требует таблицы маппинга «обнаруженных сегмент:оффсет
         * → реальная C++ функция». В каркасе вызов опускается. */
        (void)h;
        /* h(); — закомментировано как небезопасное на современных ОС */
    }

    /* Постобработка результата (см. cs:0xF94E..0xFAAF) */
    if (ai_result == 0) return;
    if (ai_result & 1) {
        /* obj был "suspendend" — добавить obj->next_idx в revival list. */
        if (revival_count < 128) {
            revival_list[revival_count++] = obj->next_idx;
        }
    }
    if (ai_result & 2) {
        /* Транзишн в новое состояние (cs:0xF986..0xFA9C). */
        if (obj->next_state_id != pending_state_id &&
            pending_state_id != 0x3E7) {
            u16 idx = pending_state_id;
            /* В оригинале: idx << 3 = ×8, обращение к state_list. */
            /* StateScript* s = &obj->state_list[idx]; */
            (void)idx;
            /* obj->state_off = s->anim_ptr_off;
             * obj->state_seg = s->anim_ptr_seg;
             * obj->state_byte = 1;
             * obj->sub_timer = 0;
             * obj->collision_cnt = 0; */
        }
    }
}

} /* namespace karnov */
