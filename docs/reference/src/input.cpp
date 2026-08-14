/* input.cpp — клавиатура, джойстик и обработка ввода.
 *
 * Пословный перевод из KARNOV.EXE:
 *   cs:0x39CA — read_keyboard
 *   cs:0x394C — read_joystick
 *   cs:0x3B1B — расширенная обработка джойстика / опрос порта 0x201
 *   cs:0x3C50 — process_input (диспетчер, разлагает state на флаги)
 *
 * Биты результата (DS:0xCC9E):
 *   0x01 RIGHT, 0x02 LEFT, 0x04 DOWN, 0x08 UP, 0x10 FIRE, 0x20 OPTION.
 */
#include "input.h"
#include "globals.h"
#include <cstdint>

namespace karnov {

/* KARNOV.EXE: cs:0x39CA — read_keyboard.
 *
 * Псевдокод (в оригинале — большая jump-table по ASCII-коду в last_key_code,
 * см. cs:0x39FA..0x3A5D):
 *
 *   case ' ':        raw_keyboard_state |= 0x10;             ; FIRE
 *   case '5':        raw_keyboard_state  = 0;                ; центр джойстика
 *   case '1':  // LEFT+DOWN
 *      raw_keyboard_state = (raw_keyboard_state & ~0x09) | 0x06;
 *   case '2':  // DOWN
 *      raw_keyboard_state = (raw_keyboard_state & ~0x08) | 0x04;
 *   case '3':  // RIGHT+DOWN
 *      raw_keyboard_state = (raw_keyboard_state & ~0x0A) | 0x05;
 *   case '4':  // LEFT
 *      raw_keyboard_state = (raw_keyboard_state & ~0x01) | 0x02;
 *   case '6':  // RIGHT
 *      raw_keyboard_state = (raw_keyboard_state & ~0x02) | 0x01;
 *   case '7':  // LEFT+UP
 *      raw_keyboard_state = (raw_keyboard_state & ~0x05) | 0x0A;
 *      // также сбрасывает 'continue' флаг: input_history &= 0xFE
 *   case '8':  // UP   (только если max_level_reached != 1)
 *      raw_keyboard_state = (raw_keyboard_state & ~0x04) | 0x08;
 *   case '9':  // RIGHT+UP
 *      raw_keyboard_state = (raw_keyboard_state & ~0x06) | 0x09;
 *   case 'X':        raw_keyboard_state |= 0x10;             ; FIRE
 *   case 'Z':        raw_keyboard_state |= 0x20;             ; OPTION
 *   default:         raw_keyboard_state &= 0x0F;             ; clear buttons
 *
 *   return (u8)raw_keyboard_state;
 *
 * Также есть начальная проверка cs:0x39D2 для уровня 1 (max_level_reached
 * == 1) с битом 0x08 в DS:0xCCA8 — особая логика для тренировочного режима.
 */
u8 read_keyboard() {
    /* В обёрточной реализации мы получаем ASCII-код из stdin/событий
     * (last_key_code обновляется отдельным механизмом). */
    u16 key = last_key_code;

    /* cs:0x39D2: если уровень 1, очистить бит 0x08 в накопителе. */
    if (max_level_reached == 1 && (raw_keyboard_state & 0x08)) {
        raw_keyboard_state &= 0xFFF7;
    }

    /* cs:0x39E2..0x3A30: ранний выход при некоторых сочетаниях
     * '1'..'9' + флаг key_counter > 0x46 (длинное удержание). */
    if (key >= 0x31 && key <= 0x39 && key_counter > 0x46) {
        /* cs:0x3B16: return текущий low-byte. */
        return static_cast<u8>(raw_keyboard_state);
    }

    switch (key) {
    case ' ': raw_keyboard_state |= 0x10; break;            /* FIRE */
    case '5': raw_keyboard_state  = 0; break;
    case '1': raw_keyboard_state = (raw_keyboard_state & 0xFFF6) | 0x06; break;
    case '2': raw_keyboard_state = (raw_keyboard_state & 0xFFF7) | 0x04; break;
    case '3': raw_keyboard_state = (raw_keyboard_state & 0xFFF5) | 0x05; break;
    case '4': raw_keyboard_state = (raw_keyboard_state & 0xFFFE) | 0x02; break;
    case '6': raw_keyboard_state = (raw_keyboard_state & 0xFFFD) | 0x01; break;
    case '7':
        raw_keyboard_state = (raw_keyboard_state & 0xFFFA) | 0x0A;
        input_history &= 0xFE;
        break;
    case '8':
        if (max_level_reached != 1) {
            raw_keyboard_state = (raw_keyboard_state & 0xFFFB) | 0x08;
        }
        break;
    case '9': raw_keyboard_state = (raw_keyboard_state & 0xFFF9) | 0x09; break;
    case 'X': case 'x': raw_keyboard_state |= 0x10; break;
    case 'Z': case 'z': raw_keyboard_state |= 0x20; break;
    default: raw_keyboard_state &= 0x0F; break;
    }
    return static_cast<u8>(raw_keyboard_state);
}

/* KARNOV.EXE: cs:0x394C — read_joystick.
 *
 * Точный байт-код опрашивает порт 0x201 в плотном цикле для измерения
 * времени разряда конденсаторов (стандартный приём DOS-эры). Результат
 * приводится к той же битовой маске. */
u8 read_joystick() {
    /* TODO: цикл «out 0x201, al; in al, 0x201; loop» из cs:0x3B22 onwards.
     * В реконструкции возвращаем «отпущено». */
    return 0;
}

/* KARNOV.EXE: cs:0x3C50..0x3D04 — process_input.
 *
 * Псевдокод:
 *   if use_joystick && key_counter > 0x46 && (last_dir & 0x30):
 *       fake_key(0x0D)                          ; принять Enter
 *
 *   raw_state = (use_joystick != 0) ? joystick_read() : keyboard_read()
 *   input_state = raw_state          ; cs:0x3C7C → DS:0xCC9E
 *
 *   if input_state != prev_input_state:
 *       option_pressed  = input_state & 0x20
 *       fire_pressed    = input_state & 0x10
 *       direction_bits  = input_state & 0x0F
 *       vert_dir_cache  = direction_bits & 0x03   ; up/down
 *       direction_changed = ((input_state & 0x0F) != (prev_input_state & 0x0F))
 *
 *   ...
 *   while frame_timer_busy:  // ждать VSYNC
 *
 *   prev_input_state = input_state  ; ((cs:0x3CE2..0x3CFE)) — обновление
 */
void process_input() {
    u8 raw;
    if (use_joystick) {
        raw = read_joystick();
    } else {
        raw = read_keyboard();
    }
    input_state = raw;

    if (input_state != prev_input_state) {
        option_pressed   = static_cast<u16>(input_state & 0x20);
        fire_pressed     = static_cast<u16>(input_state & 0x10);
        direction_bits   = static_cast<u16>(input_state & 0x0F);
        u16 v = static_cast<u16>(direction_bits & 0x03);
        if (v) vert_dir_cache = v;
        direction_changed =
            ((input_state & 0x0F) != (prev_input_state & 0x0F)) ? 1u : 0u;
    }
    /* cs:0x3CE2: дополнительные действия (системная задержка). */
    prev_input_state = input_state;
}

/* KARNOV.EXE: cs:0x3BFE — handle_menu_keys.
 *
 * Обработка клавиш-переключателей вне игры:
 *   '1' → num_players ^= 1     (toggle 1 player / 2 players)
 *   '2' → use_joystick = !use_joystick
 *   '3' → music_on ^= 1
 *   '4' → sound_on ^= 1
 *   'Q' → exit(0)
 *   ESC → in_play = 0     (pause)
 */
void handle_menu_keys() {
    /* Здесь last_key_code обновляется системой клавиатурного прерывания
     * (cs:0x394C). Логика — большой switch в оригинале (cs:0x3E80..). */
    /* TODO: ESC, '1'..'4', 'Q'. Точная реализация требует обработчиков
     * INT 9h, которые в оригинале живут в Turbo C runtime. */
}

/* ─── Глобалы ввода ─────────────────────────────────────────────────── */
u8  input_state         = 0;
u8  prev_input_state    = 0;
u8  input_history       = 0;
u16 option_pressed      = 0;
u16 fire_pressed        = 0;
u16 direction_bits      = 0;
u16 raw_keyboard_state  = 0;
u16 last_key_code       = 0;
u16 key_counter         = 0;
u16 direction_changed   = 0;
u16 hint_counter        = 0;

} /* namespace karnov */
