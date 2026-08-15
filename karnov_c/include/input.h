/* input.h — SDL2 keyboard → input_state bits.
 *
 * Поллит SDL_GetKeyboardState и OR'ит результат в *state.
 * Если клавиши нажаты, перекрывает значение из demo. */
#ifndef KARNOV_INPUT_H
#define KARNOV_INPUT_H

#include "karnov.h"

void input_poll(u8 *state);

/* Чит ZEEK (Ctrl+Z, Ctrl+E, Ctrl+E, Ctrl+K) — порт file 0x3F5E.
 * input_cheat_key вызывается из обработчика SDL_KEYDOWN на КАЖДОЕ
 * нажатие: последовательность сбрасывается любой другой клавишей. */
void input_cheat_key(int sdl_scancode, bool ctrl);
bool input_god_mode(void);
void input_reset_god_mode(void);

#endif