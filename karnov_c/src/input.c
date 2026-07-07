/* input.c — SDL2 keyboard → input_state bits.
 *
 * Arrow keys / Z / X / Space. Если клавиша нажата, OR'им бит в *state. */
#include "input.h"
#include <SDL2/SDL.h>

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
