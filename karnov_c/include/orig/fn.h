/* orig/fn.h — функции перевода; в комментарии — адрес в оригинале. */
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
s16 bk_init(void);                                   /* @015F:0646 */
s16 detect_machine(void);                            /* @01F3:0007 */
s16 read_key(void);                                  /* @020F:000A */
void DOCONTROLS(void);                               /* @020F:02FB */
void push_key(s16 key);                              /* @020F:0416 */
void demo_set_attract(s16 on);                       /* @020F:06B7 */
void delay(s16 n);                                   /* @0282:000F */
void draw_tile(s16 col, s16 row);                    /* @0287:0006 */
void rect_move_inside(s16 *a, const s16 *b);         /* @02B5:0001 */
void rect_intersect(const s16 *a, const s16 *b, s16 *out); /* @02B5:009D */
void herc_init(void);                                /* @02C7:000E */
void herc_shutdown(void);                            /* @02C7:0052 */
void hiscore_enter(u8 *score);                       /* @02D3:000C */
void show_hiscores(s16 ticks);                       /* @02D3:00FB */
void joy_read(void);                                 /* @0316:00E0 */
void bios_set_cursor(s16 row, s16 col);              /* @0330:0009 */
void bios_set_mode(s16 mode);                        /* @0330:001A */
void bios_get_mode(s16 *mode, s16 *cols, s16 *page, s16 *unused); /* @0330:0026 */
void con_putc(s16 ch);                               /* @0330:0057 */
void con_puts(const char *s);                        /* @0330:0097 */
void sys_init(void);                                 /* @0330:00D1 */
void sys_shutdown(void);                             /* @0330:015A */
void fatal(s16 code, const char *msg);               /* @0330:01AD */
s16 game_main(void);                                 /* @0330:01C2 */
s16 get_key(void);                                   /* @0361:001F */
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
void hud_items(void);                                /* @041D:01CF */
void items_reset(PlayerRec *r);                      /* @041D:0206 */
void hud_sel_clear(void);                            /* @041D:0244 */
void bcd_set(u8 *p, s16 n, s32 v);                   /* @041D:02BF */
void bcd_add(u8 *p, s16 n, s32 v);                   /* @041D:02F4 */
s16 bcd_sub(u8 *p, s16 n, s32 v);                    /* @041D:037C */
void hud_digits(u8 *p, s16 n, s16 col, s16 row);     /* @041D:041A */
void hud_score(u8 *score);                           /* @041D:0498 */
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
void item_consume(s16 n);                            /* @041D:0AB4 */
void item_set_active(s16 k, s16 on);                 /* @041D:0B4F */
void items_tick(void);                               /* @041D:0BC1 */
void draw_text(const char *s, s16 row);              /* @04E4:000B */
void end_life(void);                                 /* @04E4:018D */
void main_loop(void);                                /* @04E4:0326 */
void game_over_screen(void);                         /* @04E4:04FA */
void game_run(void);                                 /* @04E4:0621 */
void demo_reset(Demo *d);                            /* @054B:0050 */
s16 demo_record_event(Demo *d, s16 mask, s16 frame); /* @054B:00AD */
s16 demo_play(Demo *d);                              /* @054B:0106 */
s16 load_demo(void);                                 /* @054B:0166 */
FILE *bin_fopen(const char *name, const char *mode); /* @058B:0006 */
s16 bin_fclose(FILE *fp);                            /* @058B:004C */
void bin_reset(void);                                /* @058B:0066 */
u16 bin_fread(u8 *buf, u16 size, u16 count, FILE *fp); /* @058B:0077 */
s16 read_byte(u8 *dst, FILE *fp);                    /* @058B:0135 */
void scroll_blit(u16 src);                           /* @05A3:0009 */
void sprite_blit(u16 off, s16 sprite, s16 phase, s16 mode); /* @0920:0002 */
void sprite_erase(u16 off, s16 sprite, s16 phase);   /* @0920:13FE */
void vb_present_rect(u16 off, s16 w, s16 h);         /* @0920:2392 */
void hud_blit(u16 off, s16 sprite, s16 phase);       /* @0920:24C8 */
void hud_erase(u16 off, s16 sprite, s16 phase);      /* @0920:2A56 */
void sprite_init(void);                              /* @0BFB:0004 */
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
void tandy_convert(u8 *dst, const u8 *src);          /* @0D18:000D */
u16 tandy_sprite_convert(u8 *dst, const u8 *src);    /* @0D18:009D */
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
void cam_track(s16 tx, s16 ty);                      /* @0E62:0032 */
void camera_update(void);                            /* @0E62:0081 */
void set_player_state(s16 s);                        /* @0E62:01E7 */
void level_enter(void);                              /* @0E62:01F9 */
void k_start_state3(void);                           /* @0E62:05C0 */
void karnov_die(s16 s, s16 mode);                    /* @0E62:0683 */
void die_water(s16 k);                               /* @0E62:06C6 */
void die_hit(void);                                  /* @0E62:06FE */
s16 attr_head(void);                                 /* @0E62:0712 */
s16 attr_feet(void);                                 /* @0E62:0727 */
void ladder_grab(void);                              /* @0E62:07C3 */
void ladder_grab_down(void);                         /* @0E62:07FF */
void k_appear(void);                                 /* @0E62:0C49 */
void k_walk(void);                                   /* @0E62:0CF6 */
void k_slope(void);                                  /* @0E62:0E20 */
void k_crouch(void);                                 /* @0E62:0FAE */
void k_climb(void);                                  /* @0E62:1071 */
void k_jump(void);                                   /* @0E62:1165 */
void k_fly(void);                                    /* @0E62:13C6 */
void k_state3(void);                                 /* @0E62:14D7 */
void k_bounce(void);                                 /* @0E62:16F3 */
void karnov_death(void);                             /* @0E62:1791 */
void shot_straight(void);                            /* @0FFA:00C2 */
void shot_upper(void);                               /* @0FFA:00FF */
void shot_lower(void);                               /* @0FFA:0183 */
void boomerang(void);                                /* @0FFA:0247 */
void fireball(void);                                 /* @0FFA:0575 */
void ladders_mark(void);                             /* @0FFA:0724 */
void ladder_item(void);                              /* @0FFA:07CC */
void bomb_blast(void);                               /* @0FFA:0A7B */
void bomb(void);                                     /* @0FFA:0BCB */
void hidden_item(void);                              /* @0FFA:0CF6 */
void falling_item(void);                             /* @0FFA:0DAA */
void static_item(void);                              /* @0FFA:0EB8 */
void level_scroll(void);                             /* @0FFA:1032 */
void sky_ice(void);                                  /* @0FFA:139F */
void kill_sky_chain(void);                           /* @0FFA:155E */
void PRE_KARNOV(void);                               /* @0FFA:1A1A */
void POST_KARNOV(void);                              /* @0FFA:1C04 */
void aim_speed(s16 x0, s16 y0, s16 x1, s16 y1, s32 *vx, s32 *vy, s32 speed); /* @11D6:0006 */
void aim(s16 x0, s16 y0, s16 x1, s16 y1, s32 *vx, s32 *vy, s16 n); /* @11D6:00DD */
void spawn_projectile(s16 x0, s16 y0, s16 tx, s16 ty, s16 kind, s32 speed, s16 flat); /* @11D6:0139 */
void projectile(void);                               /* @11D6:02A3 */
void ai_t10(void);                                   /* @11D6:0413 */
void ai_t11(void);                                   /* @11D6:04CA */
void ai_t17(void);                                   /* @11D6:0617 */
void ai_t13(void);                                   /* @11D6:06A8 */
void ai_t12(void);                                   /* @11D6:08B3 */
void ai_t15(void);                                   /* @11D6:0ACB */
void ai_t14(void);                                   /* @11D6:0E08 */
void ai_t16(void);                                   /* @11D6:1033 */
void ai_t18(void);                                   /* @11D6:127D */
void ai_t09(void);                                   /* @11D6:1644 */
void ai_t19(void);                                   /* @11D6:16C0 */
void ai_t23(void);                                   /* @1348:0007 */
void gold_ball(void);                                /* @1348:00D7 */
void ai_t24(void);                                   /* @1348:03A9 */
void ai_t20(void);                                   /* @1348:06DD */
void ai_t25(void);                                   /* @1348:0A20 */
void t22_drop(void);                                 /* @1348:0BB2 */
void ai_t22(void);                                   /* @1348:0C82 */
void ai_t26(void);                                   /* @1348:0E46 */
void ai_t21(void);                                   /* @1348:1153 */
void ai_t30(void);                                   /* @1348:12CF */
void ai_t29(void);                                   /* @1348:1531 */
void ai_t27(void);                                   /* @1348:17AB */
void ai_t28(void);                                   /* @1348:197C */
void ai_t31(void);                                   /* @1348:1B1A */
void ai_t32(void);                                   /* @1348:1C34 */
void ai_t33(void);                                   /* @151A:00CD */
void ai_t34(void);                                   /* @151A:02A6 */
void ai_t36(void);                                   /* @151A:061B */
void ai_t35(void);                                   /* @151A:08B0 */
void ai_t37(void);                                   /* @151A:0C31 */
void ai_t38(void);                                   /* @151A:0EC7 */
void ai_t39(void);                                   /* @161F:0002 */
void ai_t41(void);                                   /* @161F:020A */
void ai_t42(void);                                   /* @161F:029F */
void ai_t40(void);                                   /* @161F:0447 */
void ai_t45(void);                                   /* @1692:00B4 */
void ai_t43(void);                                   /* @1692:0205 */
void ai_t47(void);                                   /* @1692:0479 */
void ai_t44(void);                                   /* @1692:052E */
void ai_t46(void);                                   /* @1692:066B */
void t48_drop(void);                                 /* @170C:000E */
void ai_t48(void);                                   /* @170C:018C */
void ai_t49(void);                                   /* @170C:03C5 */
void ai_t50(void);                                   /* @175E:000F */
void ai_t52(void);                                   /* @175E:0203 */
void partner_kill(void);                             /* @175E:0391 */
void t51_open(void);                                 /* @175E:03A9 */
void ai_t51(void);                                   /* @175E:0498 */
void ai_t53(void);                                   /* @17B6:007D */
void ai_t54(void);                                   /* @17B6:01DB */
void ai_t55(void);                                   /* @17EC:0028 */
void ai_t56(void);                                   /* @17EC:03B2 */
void ai_t57(void);                                   /* @17EC:0607 */
s16 rtl_bioskey(s16 cmd);                            /* @1B7A:0000 */
s16 rtl_rand(void);                                  /* @1CE7:0017 */

/* порт */
ActiveObject *new_object(s16 parity);
const u8 *sprite_data(s16 n);
void wait_for_file(const char *name, const char *msg);

#endif
