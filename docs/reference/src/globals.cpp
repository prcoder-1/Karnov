/* globals.cpp — определения глобальных переменных, объявленных в globals.h.
 *
 * Адреса в DS оригинала указаны в комментариях; цель — облегчить
 * перекрёстную сверку с дизассемблированием.
 */
#include "globals.h"

namespace karnov {

/* Игровое состояние */
u16  current_frame      = 0;     /* DS:0xCDEA */
u16  in_game            = 0;     /* DS:0xCDEC */
u16  scroll_counter     = 0;     /* DS:0xCDF0 */
u16  bg_palette         = 0;     /* DS:0xCDF4 */
u16  hud_state[3]       = {0,0,0}; /* DS:0xCE00,0xCE02,0xCE04 */
u16  lives_counter      = 0;     /* DS:0xCF4E */
u16  current_level      = 0;     /* DS:0xD8AA */
u16  level_state_a      = 0;     /* DS:0xD8C8 */
u16  level_state_b      = 0;     /* DS:0xD8CA */
u16  level_state_c      = 0;     /* DS:0xD8CC */
u16  max_level_reached  = 0;     /* DS:0xD8CE */
u16  vert_dir_cache     = 0;     /* DS:0xD8B0 */

u16  num_players        = 1;     /* DS:0x35A */
u16  use_joystick       = 0;     /* DS:0x358 */
u16  music_on           = 1;     /* DS:0x354 */
u16  sound_on           = 1;     /* DS:0x356 */
u16  in_play            = 0;     /* DS:0x35C */
u16  level_mode         = 0;     /* DS:0x360 */

/* Указатели */
DemoState*    demo_play       = nullptr;  /* DS:0xD55C far ptr */
DemoState*    demo_record     = nullptr;  /* DS:0xD560 far ptr */
ObjectRegion* level_objects   = nullptr;  /* DS:0x47C far ptr */
u16           num_level_objects = 0;       /* DS:0xCDFE */
EnemySpawn*   level_enemies   = nullptr;  /* DS:0x494 far ptr */
u16           num_level_enemies = 0;       /* DS:0xCDFA */
u8*           map_buffer      = nullptr;  /* DS:0x98 far ptr */
u8*           tile_pixels     = nullptr;  /* DS:0xB82A far ptr */
u16           num_unique_tiles = 0;        /* DS:0xB830 */
u16           tile_size_words  = 0;        /* DS:0xCC96 */
u16           tile_size_bytes  = 0;        /* DS:0xCC98 */
u16           obj0_x1          = 0;        /* DS:0x48C */
u16           obj0_y1          = 0;        /* DS:0x48E */

} /* namespace karnov */
