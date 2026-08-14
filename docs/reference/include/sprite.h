/* sprite.h — спрайты (SPRITES.CGA, SPRITES.EGA). */
#ifndef KARNOV_SPRITE_H
#define KARNOV_SPRITE_H

#include "karnov.h"

namespace karnov {

/* KARNOV.EXE: cs:0xBFB:0x04 — sprite_init.
 * При сбое выводит "SpriteInit failed!" (DS:0x3CC) и "sprite bummer #%d". */
int sprite_init();

/* KARNOV.EXE: cs:0xBFB:0xB2 — splash/title (после init_sprites). */
void splash_screen();

} /* namespace karnov */

#endif
