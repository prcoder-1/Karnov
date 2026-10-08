/* Сгенерировано tools/gen_fn.py — не править. */
#ifndef ORIG_FN_H
#define ORIG_FN_H
#include <stdio.h>
#include "orig/ds.h"

void init_object_pool(void);                         /* @006E:0002 */
void objects_init(void);                             /* @006E:00D1 */
s16 alloc_object(s16 parity);                        /* @006E:011B */
void free_object(s16 idx);                           /* @006E:0226 */
void free_all_objects(void);                         /* @006E:02D0 */
void unstamp_object(ActiveObject *o);                /* @006E:04B4 */
void stamp_object(ActiveObject *o);                  /* @006E:052B */
void update_bboxes(ActiveObject *o);                 /* @006E:059A */
void UPDATELOCS(void);                               /* @006E:06D3 */
s16 box_visible(SpriteBox *b);                       /* @006E:07A5 */
s16 any_box_visible(ActiveObject *o);                /* @006E:07FC */
void ERASECYCLE(void);                               /* @006E:0846 */
void DRAWCYCLE(void);                                /* @006E:0C37 */
void POSTHITS(void);                                 /* @006E:0CDB */
void video_reset(void);                              /* @015F:0001 */
void clear_screen(void);                             /* @015F:0091 */
void SCROLLUPDATE(void);                             /* @015F:0195 */
void redraw_rect(const s16 *r);                      /* @015F:01DE */
void SCROLLVIEW(void);                               /* @015F:0296 */
void detect_video_mode(void);                        /* @015F:0494 */
void video_nop(void);                                /* @015F:0641 */
s16 bk_init(void);                                   /* @015F:0646 */
void video_shutdown(void);                           /* @015F:0922 */
s16 detect_machine(void);                            /* @01F3:0007 */
s16 rom_probe(void);                                 /* @01F3:0153 */
s16 rom_compare(void);                               /* @01F3:015D */
s16 read_key(void);                                  /* @020F:000A */
void DOCONTROLS(void);                               /* @020F:02FB */
void push_key(s16 key);                              /* @020F:0416 */
void demo_set_attract(s16 on);                       /* @020F:06B7 */
s16 detect_cpu(void);                                /* @027D:000F */
void delay(s16 n);                                   /* @0282:000F */
void draw_tile(s16 col, s16 row);                    /* @0287:0006 */
void rect_move_inside(s16 *a, const s16 *b);         /* @02B5:0001 */
void rect_intersect(const s16 *a, const s16 *b, s16 *out); /* @02B5:009D */
u8 *joy_read(void);                                  /* @0316:00E0 */
void bios_set_cursor(s16 row, s16 col);              /* @0330:0009 */
void bios_set_mode(s16 mode);                        /* @0330:001A */
void bios_get_mode(s16 *mode, s16 *cols, s16 *page, s16 *unused); /* @0330:0026 */
void con_putc(s16 ch);                               /* @0330:0057 */
void con_puts(const char *s);                        /* @0330:0097 */
void sys_init(void);                                 /* @0330:00D1 */
void sys_shutdown(void);                             /* @0330:015A */
void fatal(s16 code, const char *msg);               /* @0330:01AD */
s16 game_main(void);                                 /* @0330:01C2 */
void clear_arg(s16 a);                               /* @0330:030B */
s16 get_key(void);                                   /* @0361:001F */
s16 level_init(void);                                /* @0365:000B */
void level_shutdown(void);                           /* @0365:00B8 */
ActiveObject *spawn_active_object(EnemySpawn *rec);  /* @0365:00ED */
void CHECKTRIGGERS(void);                            /* @0365:01C1 */
s16 read_object_regions(FILE *fp);                   /* @0365:0366 */
s16 read_enemies(FILE *fp);                          /* @0365:03C6 */
s16 load_level(s16 level);                           /* @0365:0500 */
void enter_level(void);                              /* @0365:0A04 */
void hud_put(s16 sprite, s16 col, s16 row, s16 show); /* @041D:0002 */
void hud_item(s16 i, s16 show);                      /* @041D:0129 */
void hud_sel(s16 i, s16 show);                       /* @041D:014E */
void hud_lives(void);                                /* @041D:01A6 */
void items_reset(u8 *rec);                           /* @041D:0206 */
void bcd_set(u8 *p, s16 n, s32 v);                   /* @041D:02BF */
void bcd_add(u8 *p, s16 n, s32 v);                   /* @041D:02F4 */
s16 bcd_sub(u8 *p, s16 n, s32 v);                    /* @041D:037C */
void add_score(s32 v);                               /* @041D:04D7 */
void hud_redraw(void);                               /* @041D:04F7 */
void hud_options(void);                              /* @041D:05D2 */
s16 use_item0(void);                                 /* @041D:079A */
s16 use_item1(void);                                 /* @041D:07F2 */
s16 use_item2(void);                                 /* @041D:081D */
s16 use_item3(void);                                 /* @041D:0879 */
s16 use_item4(void);                                 /* @041D:08A4 */
s16 use_item5(void);                                 /* @041D:08BD */
s16 use_item6(void);                                 /* @041D:08CB */
s16 use_item7(void);                                 /* @041D:08F1 */
s16 use_item8(void);                                 /* @041D:08F9 */
void item_pickup(s16 n);                             /* @041D:09BE */
void item_set_active(s16 k, s16 on);                 /* @041D:0B4F */
void items_tick(void);                               /* @041D:0BC1 */
void draw_text(const char *s, s16 row);              /* @04E4:000B */
void end_life(void);                                 /* @04E4:018D */
void main_loop(void);                                /* @04E4:0326 */
void game_over_screen(void);                         /* @04E4:04FA */
void game_run(void);                                 /* @04E4:0621 */
void demo_reset(Demo *d);                            /* @054B:0050 */
void demo_copy(const Demo *src, Demo *dst);          /* @054B:0089 */
s16 demo_record_event(Demo *d, s16 mask, s16 frame); /* @054B:00AD */
s16 demo_play(Demo *d);                              /* @054B:0106 */
s16 load_demo(void);                                 /* @054B:0166 */
s16 save_demo(void);                                 /* @054B:031C */
FILE *bin_fopen(const char *name, const char *mode); /* @058B:0006 */
s16 bin_fclose(FILE *fp);                            /* @058B:004C */
void bin_reset(void);                                /* @058B:0066 */
u16 bin_fread(u8 *buf, u16 size, u16 count, FILE *fp); /* @058B:0077 */
s16 read_byte(u8 *dst, FILE *fp);                    /* @058B:0135 */
s16 sprite_init(void);                               /* @0BFB:0004 */
void sprite_shutdown(void);                          /* @0BFB:00B2 */
s16 sprite_loader_main(void);                        /* @0BFB:011C */
void sound_shutdown(void);                           /* @0CB9:000A */
s16 get_music_enabled(void);                         /* @0CB9:0033 */
s16 music_done(void);                                /* @0CB9:0037 */
s16 get_sfx_enabled(void);                           /* @0CB9:0048 */
void sound_init(void);                               /* @0CB9:004C */
void play_music(const u8 *script, u16 prio, s16 loop); /* @0CB9:007D */
void play_sfx(const u8 *script, u16 prio);           /* @0CB9:00DD */
void set_music_enabled(s16 on);                      /* @0CB9:013C */
void set_sfx_enabled(s16 on);                        /* @0CB9:0165 */
void timer_isr(void);                                /* @0CB9:03CC */
farfn get_vector(s16 n);                             /* @0CB9:04B2 */
void set_vector(s16 n, farfn f);                     /* @0CB9:04D8 */
s16 ega_detect(void);                                /* @0D09:0006 */
s16 cga_detect(void);                                /* @0D09:0050 */
s16 herc_detect(void);                               /* @0D09:00A3 */
void map_set_attr(s16 col, s16 row, s16 attr);       /* @0D4B:0003 */
s16 map_attr(s16 col, s16 row);                      /* @0D4B:0021 */
s16 map_tile(s16 col, s16 row);                      /* @0D4B:003E */
s16 attr_at(s16 x, s16 y);                           /* @0D4B:005A */
void set_motion(s16 n);                              /* @0D4B:008D */
s16 dist_to_player(void);                            /* @0D4B:0105 */
void snap_to_ground(ActiveObject *o, s16 sprite, u16 mask, s16 col, s16 row); /* @0D4B:014C */
void place_on_ground(ActiveObject *o, s16 sprite, s16 kind); /* @0D4B:019F */
void place_on_ground_center(ActiveObject *o, s16 sprite); /* @0D4B:01DE */
void center_on_column(ActiveObject *o, s16 col, s16 w); /* @0D4B:0229 */
void stop_at_wall(void);                             /* @0D4B:024C */
s16 count_live_parts(ActiveObject *o);               /* @0D4B:02AC */
void stamp_solid(ActiveObject *o, s16 y0, s16 y1, s16 ncols); /* @0D4B:02DB */
s16 player_sprite_h(void);                           /* @0D4B:0392 */
void dir_latch_release(void);                        /* @0D4B:03A5 */
void puff_handler(void);                             /* @0D4B:03C0 */
void die_puff(void);                                 /* @0D4B:04A5 */
void die_puff2(void);                                /* @0D4B:04FF */
void remove_object(void);                            /* @0D4B:0559 */
void move_with_collision(CollRes *res, SpriteBox *b, s32 dx, s32 dy); /* @0D4B:06CB */
void init_active_object(ActiveObject *o, farfn h, s16 arg); /* @0D4B:0AF5 */
void queue_state(ActiveObject *o, farfn h);          /* @0D4B:0B31 */
void ai_dispatch(void);                              /* @0D4B:0BD5 */
void THINKCYCLE(void);                               /* @0D4B:0E03 */
void play_sound(s16 n);                              /* @0D4B:1124 */
void level_clear_state(void);                        /* @0D4B:1148 */
s16 rtl_bioskey(s16 cmd);                            /* @1B7A:0000 */
void rtl_exit(s16 code);                             /* @1B89:0005 */
void rtl_farfree(u8 *p);                             /* @1BBC:000A */
u8 *rtl_farmalloc(u32 size);                         /* @1BE0:0008 */
FILE *rtl_fopen(const char *name, const char *mode); /* @1BF7:0203 */
s16 rtl_fseek(FILE *fp, s32 off, s16 whence);        /* @1C31:0053 */
void rtl_fmemcpy(void *dst, const void *src, u16 n); /* @1C92:000E */

#endif
