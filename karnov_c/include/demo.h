/* demo.h — LEVEL%d.MOV reader + tick. */
#ifndef KARNOV_DEMO_H
#define KARNOV_DEMO_H

#include "karnov.h"

int  demo_load(u16 level_num);          /* 0 = OK, иначе ошибка */
bool demo_tick(u16 current_frame);      /* true = демо завершено */
u8   demo_input_state(void);
u16  demo_current_index(void);
u16  demo_record_count(void);
void demo_reset(void);                  /* перемотать к началу (records остаются) */
/* Полная очистка: и records, и cursor. После demo_clear demo_tick — no-op,
 * demo_input_state == 0. Используется при splash → LEVEL1 transition. */
void demo_clear(void);

#endif
