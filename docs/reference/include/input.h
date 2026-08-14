/* input.h — клавиатура и джойстик. */
#ifndef KARNOV_INPUT_H
#define KARNOV_INPUT_H

#include "karnov.h"

namespace karnov {

/* KARNOV.EXE: cs:0x39CA — read_keyboard: преобразует last_key_code в маску.
 * Использует ASCII-коды '1'..'9' (numpad) + 'X','Z',' '. */
u8 read_keyboard();

/* KARNOV.EXE: cs:0x394C — read_joystick. */
u8 read_joystick();

/* KARNOV.EXE: cs:0x3C7B — обработка ввода и обновление флагов
 * fire_pressed / option_pressed / direction_bits / vert_dir_cache. */
void process_input();

/* KARNOV.EXE: cs:0x3BFE — основной обработчик меню/паузы. */
void handle_menu_keys();

} /* namespace karnov */

#endif
