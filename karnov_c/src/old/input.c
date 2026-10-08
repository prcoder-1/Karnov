/* input.c — SDL2 keyboard → input_state bits.
 *
 * Arrow keys / Z / X / Space. Если клавиша нажата, OR'им бит в *state.
 * Плюс чит-последовательность ZEEK (см. input_cheat_key). */
#include "input.h"
#include "audio.h"
#include <SDL2/SDL.h>

/* ─── Чит «ZEEK» (KARNOV.EXE, file 0x3F5E..0x3F99) ────────────────────
 *
 * Оригинал держит позицию в последовательности в [DS:0xCCCA] и разбирает
 * нажатие Ctrl+буква так:
 *     Ctrl+Z — принимается ТОЛЬКО когда счётчик 0        → ++счётчик
 *     Ctrl+E — принимается когда счётчик 1 либо 2        → ++счётчик
 *     Ctrl+K — при счётчике 3: [0xCCA6] ^= 1, play_sound(6)
 *     любая другая обработанная клавиша                  → счётчик = 0
 * То есть нужна ровно последовательность Ctrl+Z, Ctrl+E, Ctrl+E, Ctrl+K,
 * а god_mode ПЕРЕКЛЮЧАЕТСЯ (повторный ввод выключает).
 *
 * Эффекты god_mode в оригинале:
 *   • pair-loop гасит удар по игроку (file 0xFB8A);
 *   • перезарядка выстрела 1 тик вместо 3 (file 0x1302C);
 *   • доступен level select (L + цифра, file 0x3DB0).
 * Сбрасывается только при старте новой игры (file 0x6C92). */
static int  s_cheat_pos  = 0;      /* [0xCCCA] */
static bool s_god_mode   = false;  /* [0xCCA6] */

bool input_god_mode(void) { return s_god_mode; }
void input_reset_god_mode(void) { s_god_mode = false; s_cheat_pos = 0; }

void input_cheat_key(int sdl_scancode, bool ctrl)
{
    if (!ctrl) { s_cheat_pos = 0; return; }
    switch (sdl_scancode) {
    case SDL_SCANCODE_Z:
        s_cheat_pos = (s_cheat_pos == 0) ? 1 : 0;
        break;
    case SDL_SCANCODE_E:
        s_cheat_pos = (s_cheat_pos == 1 || s_cheat_pos == 2) ? s_cheat_pos + 1 : 0;
        break;
    case SDL_SCANCODE_K:
        if (s_cheat_pos == 3) {
            s_god_mode = !s_god_mode;
            audio_play_sfx(6);          /* play_sound(6) в оригинале */
        }
        s_cheat_pos = 0;
        break;
    default:
        s_cheat_pos = 0;                /* прочие клавиши сбрасывают */
        break;
    }
}

void input_poll(u8 *state)
{
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    if (!ks) return;
    u8 add = 0;
    if (ks[SDL_SCANCODE_RIGHT]) add |= INP_R;
    if (ks[SDL_SCANCODE_LEFT])  add |= INP_L;
    if (ks[SDL_SCANCODE_DOWN])  add |= INP_D;
    if (ks[SDL_SCANCODE_UP])    add |= INP_U;
    if (ks[SDL_SCANCODE_X] || ks[SDL_SCANCODE_SPACE]) add |= INP_FIRE;
    if (ks[SDL_SCANCODE_Z])     add |= INP_OPT;
    if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]) add |= INP_START;
    if (add) *state = add;   /* keyboard overrides demo */
}
