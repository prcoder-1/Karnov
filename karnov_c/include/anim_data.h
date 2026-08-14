/* Сгенерировано tools/extract_anim_tables.py — таблицы анимаций из KARNOV.EXE.
 *
 * Состояние = набор кадров; кадр = sprite_id + смещение bbox (dx, dy).
 * Индекс кадра инкрементируется раз в tick_period игровых тиков и
 * заворачивается по длине состояния (ai_dispatch, file 0x0F885).
 * dx — в единицах движка (в порту умножается на 2), dy — строки. */
#ifndef KARNOV_ANIM_DATA_H
#define KARNOV_ANIM_DATA_H

#include "karnov.h"

typedef struct {
    u8         n;      /* число кадров */
    const u16 *spr;    /* sprite_id[n] */
    const s16 *off;    /* пары (dx, dy) */
} anim_state_t;

typedef struct {
    u8                  count;   /* число состояний (0 = нет данных) */
    const anim_state_t *states;
} anim_list_t;

extern const anim_list_t ANIM_LISTS[58];

#endif
