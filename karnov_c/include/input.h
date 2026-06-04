/* input.h — SDL2 keyboard → input_state bits.
 *
 * Поллит SDL_GetKeyboardState и OR'ит результат в *state.
 * Если клавиши нажаты, перекрывает значение из demo. */
#ifndef KARNOV_INPUT_H
#define KARNOV_INPUT_H

#include "karnov.h"

void input_poll(u8 *state);

#endif
