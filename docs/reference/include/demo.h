/* demo.h — запись и воспроизведение демо (LEVEL%d.MOV).
 * Соответствует cs:0x6E16, cs:0x6FCC, cs:0x6DC0 в KARNOV.EXE.
 */
#ifndef KARNOV_DEMO_H
#define KARNOV_DEMO_H

#include "karnov.h"

namespace karnov {

/* KARNOV.EXE: cs:0x6E16 — load_demo: открыть, прочитать заголовок, прочитать
 * num_records записей, заполнить структуру в demo_play.  */
int load_demo(int level_number);

/* KARNOV.EXE: cs:0x6FCC — save_demo: записать demo_record в `level%d.mov` (wb). */
int save_demo(int level_number);

/* KARNOV.EXE: cs:0x6DC0 — tick_demo: вызывается каждый игровой кадр. Применяет
 * очередное событие к input_state, если frame_number ≤ current_frame.
 * Возвращает 1, если демо закончено и нужно выйти из аттракт-режима. */
int tick_demo(DemoState* state);

/* KARNOV.EXE: cs:0x6CB9 — is_demo_at_end: curr_idx == num_records. */
int is_demo_at_end(DemoState* state);

/* KARNOV.EXE: cs:0x6D00 — demo_reset: очистить структуру. */
void demo_reset(DemoState* state);

/* KARNOV.EXE: cs:0x6D27 — demo_advance: ++curr_idx (с маской 0xFF). */
void demo_advance(DemoState* state);

/* KARNOV.EXE: cs:0x6D5D — demo_record_event: добавить запись в demo_record. */
int demo_record_event(DemoState* state, u8 input, u16 frame);

} /* namespace karnov */

#endif
