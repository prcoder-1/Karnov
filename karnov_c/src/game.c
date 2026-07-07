/* game.c — splash_run: оркестровка LEVEL%d.BIN + attract demo + аудио. */

#include "game.h"
#include "level.h"
#include "ai.h"
#include "audio.h"
#include "demo.h"
#include "file_io.h"
#include "input.h"
#include "inventory.h"
#include "palette.h"
#include "physics.h"
#include "render.h"
#include "sprite.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

/* ─── Sprite cache loading helpers ─────────────────────────────────── */

static int try_load_sprites(const char *sprite_file)
{
    char path[4096];
    const char *prefixes[] = { "", "data/", "../data/", NULL };
    for (int i = 0; prefixes[i]; ++i) {
        snprintf(path, sizeof(path), "%s%s", prefixes[i], sprite_file);
        if (sprite_cache_load(path) == 0) return 0;
    }
    /* Search exe directory */
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            const char *rel_list[] = { "/data/", "/../data/", NULL };
            for (int i = 0; rel_list[i]; ++i) {
                snprintf(path, sizeof(path), "%s/%s%s", exe, rel_list[i], sprite_file);
                if (sprite_cache_load(path) == 0) return 0;
                snprintf(path, sizeof(path), "%s/../%s", exe, rel_list[i] + 1);
                snprintf(path, sizeof(path), "%s/%s", exe, rel_list[i] + 1);
                if (sprite_cache_load(path) == 0) return 0;
            }
        }
    }
    return 1;
}

/* ─── Brown dragon at LEVEL1 start ─────────────────────────────────── */

#define LEVEL1_DRAGON_X        0      /* world x, левый край */
#define LEVEL1_DRAGON_BOT_Y  260      /* world y, низ туловища = stone platform */
#define LEVEL1_DRAGON_UPPER  145      /* upper sprite */
#define LEVEL1_DRAGON_LOWER  146      /* lower sprite */

static void draw_level1_dragon(int scroll_x, int scroll_y)
{
    const sprite_t *up = sprite_find(LEVEL1_DRAGON_UPPER);
    const sprite_t *lo = sprite_find(LEVEL1_DRAGON_LOWER);
    if (!up || !lo) return;
    int lo_top = LEVEL1_DRAGON_BOT_Y - lo->h;
    int up_top = lo_top - up->h;
    const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
    u8 ck = sprite_is_cga() ? 0xFF : 1;

    /* Align body centers по body_cx — иначе верхняя часть сдвинута на 4 px
     * вправо относительно нижней (#145 body_cx=33 vs #146 body_cx=37). */
    int x0_lower = LEVEL1_DRAGON_X - scroll_x;
    int x0_upper = LEVEL1_DRAGON_X - ((int)lo->body_cx - (int)up->body_cx) - scroll_x;

    const sprite_t *parts[2] = { up, lo };
    int tops[2] = { up_top, lo_top };
    int x0s[2]  = { x0_upper, x0_lower };
    for (int p = 0; p < 2; ++p) {
        const sprite_t *s = parts[p];
        int x0 = x0s[p];
        int y0 = tops[p] - scroll_y;
        for (int r = 0; r < s->h; ++r) {
            int yy = y0 + r;
            if (yy < 0 || yy >= WIN_H) continue;
            for (int c = 0; c < s->w; ++c) {
                /* Mirror: читаем pixel из (w-1-c), но пишем в x0+c. */
                int sc = s->w - 1 - c;
                u8 v = s->pixels[r * s->w + sc];
                if (v == 0xFF) continue;
                if (v == ck) continue;
                int xx = x0 + c;
                if (xx < 0 || xx >= WIN_W) continue;
                u32 col = sprite_is_cga() ? pal[v & 3] : pal[v & 15];
                render_framebuffer()[yy * WIN_W + xx] = col;
            }
        }
    }
}

/* ─── Scroll ───────────────────────────────────────────────────────── */

static int g_scroll_x = 0;
static int g_scroll_y = 0;

static void update_scroll(int karnov_x, int karnov_y)
{
    int level_w_px = level_num_map_cols * TILE_W;
    int level_h_px = level_num_map_rows * TILE_H;
    if (level_w_px > VIEW_W) {
        int target = karnov_x - VIEW_W / 2;
        if (target < 0) target = 0;
        if (target > level_w_px - VIEW_W) target = level_w_px - VIEW_W;
        g_scroll_x = target;
    } else {
        g_scroll_x = 0;
    }
    if (level_h_px > VIEW_H) {
        int target = karnov_y - VIEW_H / 2;
        if (target < 0) target = 0;
        if (target > level_h_px - VIEW_H) target = level_h_px - VIEW_H;
        g_scroll_y = target;
    } else {
        g_scroll_y = 0;
    }
}

/* ─── Draw level tiles ─────────────────────────────────────────────── */

static void draw_level(bool mode_cga)
{
    int row0 = g_scroll_y / TILE_H;
    int row1 = (g_scroll_y + VIEW_H + TILE_H - 1) / TILE_H;
    int col0 = g_scroll_x / TILE_W;
    int col1 = (g_scroll_x + VIEW_W + TILE_W - 1) / TILE_W;
    if (col1 > level_num_map_cols) col1 = level_num_map_cols;
    if (row1 > level_num_map_rows) row1 = level_num_map_rows;
    for (int r = row0; r < row1; ++r) {
        for (int c = col0; c < col1; ++c) {
            u8 tid = level_map[r][c];
            int sx = c * TILE_W - g_scroll_x;
            int sy = r * TILE_H - g_scroll_y;
            if (mode_cga) {
                render_blit_tile(sx, sy, level_tile_gfx[tid], level_tile_attr[tid], true);
            } else {
                render_blit_tile_ega(sx, sy, level_tile_gfx_ega[tid], level_tile_attr[tid]);
            }
        }
    }
}

/* ─── Ladder ───────────────────────────────────────────────────────── */

static void draw_ladder(u16 frame, int scroll_x, int scroll_y)
{
    if (!physics_ladder_placed()) return;
    int cx = physics_ladder_x();
    int cy_top = physics_ladder_top();
    int cy_bot = physics_ladder_bot();
    /* physics_ladder_bot() возвращает s_ground_y (Karnov-feet anchor), а тот
     * в LEVEL0 splash находится на середине sand-тайла (115 при sand top=110).
     * Округляем вниз к tile-границе, чтобы визуальный низ лесенки совпадал
     * с поверхностью земли, а не уходил на 5 px ниже. Для LEVEL1+ где
     * ground_y кратен TILE_H — изменений нет. */
    cy_bot = (cy_bot / TILE_H) * TILE_H;
    /* Sprite #24 = ladder visual (8 px wide × ~8 tall, тайлится по Y). */
    const sprite_t *s = sprite_find(24);
    if (!s) return;
    const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
    render_blit_ladder(cx - scroll_x, cy_top - scroll_y, cy_bot - scroll_y,
                       s->pixels, s->w, s->h, pal);
}

/* ─── HUD bar ──────────────────────────────────────────────────────── */

static void draw_hud(u16 frame, int karnov_x, u8 input_state)
{
    /* Yellow text on black background bar. */
    render_fill_rect(0, VIEW_H, WIN_W, HUD_H, 0xFF000000u);
    char line[80];
    snprintf(line, sizeof(line),
             "FRAME:%-4u  REC:%u/%u    X:%-3d Y:%-3d    INV %c %c",
             frame, demo_current_index(), demo_record_count(),
             karnov_x, physics_get_y(),
             inv_has_ladder() ? 'L' : '-',
             inv_has_wings()  ? 'W' : '-');
    render_draw_text(4, VIEW_H + 4, line, 0xFFFFFF55u);
    snprintf(line, sizeof(line),
             "INPUT:%02X  %c%c%c%c%c%c",
             input_state,
             (input_state & INP_R)    ? 'R' : '-',
             (input_state & INP_L)    ? 'L' : '-',
             (input_state & INP_U)    ? 'U' : '-',
             (input_state & INP_D)    ? 'D' : '-',
             (input_state & INP_FIRE) ? 'F' : '-',
             (input_state & INP_OPT)  ? 'O' : '-');
    render_draw_text(4, VIEW_H + 14, line, 0xFFCCCCCCu);
}

/* ─── Level physics setup ──────────────────────────────────────────── */

typedef struct {
    int spawn_x, spawn_y;
    int ground_y, ceiling_y;
    int min_x, max_x;
} level_phys_t;

static level_phys_t level_phys_for(u16 level_num)
{
    if (level_num == 0) {
        /* LEVEL0 splash: 320×120 экран, без скролла.
         * spawn_x=137 — байт-верифицированный из LEVEL0.MOV demo
         * (Karnov walk -22*4 net = ladder placed at x=49 = DOSBox match).
         * ceiling=10 — чтобы прыжок мог дотянуться до верха букв KARNOV. */
        return (level_phys_t){
            .spawn_x = 137, .spawn_y = 108,
            .ground_y = 115, .ceiling_y = 10,
            .min_x = 8, .max_x = 312
        };
    }
    /* LEVEL1+: широкий мир со скроллом.
     * spawn_x=80 — справа от brown dragon (x=0..64), на каменной dragon-
     * платформе (x=0..191, top y=260). ground_y вычисляется per-level
     * сканом карты (level_compute_ground_y): LEVEL1=290 (sand), LEVEL2=310
     * (трава), LEVEL3=400, и т.д. Это аналог поведения KARNOV.EXE, где
     * нет hardcoded floor — Karnov падает до реальной solid-строки. */
    int level_w_px = level_num_map_cols * TILE_W;
    int ground_y = level_compute_ground_y();
    return (level_phys_t){
        .spawn_x = 80, .spawn_y = 260,
        .ground_y = ground_y, .ceiling_y = 10,
        .min_x = 8, .max_x = (level_w_px > 16 ? level_w_px - 8 : 312)
    };
}

/* ─── prepare_level ────────────────────────────────────────────────── */

static int prepare_level(u16 level_num, bool load_demo)
{
    int rc = level_load(level_num);
    if (rc != 0) {
        fprintf(stderr, "prepare_level: level_load(%u) failed rc=%d\n", level_num, rc);
        return rc;
    }
    printf("LEVEL%u: %u объектов, %u врагов, %u tile_graphics, карта %ux%u\n",
           level_num, level_num_objects, level_num_enemies,
           level_num_tile_graphics, level_num_map_rows, level_num_map_cols);

    level_phys_t ph = level_phys_for(level_num);
    palette_set_level(level_num);
    physics_set_world_bounds(ph.min_x, ph.max_x, ph.ground_y, ph.ceiling_y);
    physics_set_level0_splash(level_num == 0);
    physics_set_level1_dragon_platform(level_num == 1);
    /* DEBUG: KARNOV_START_X env var override (для headless screenshot-теста). */
    const char *dbg_start_x = getenv("KARNOV_START_X");
    int sx = dbg_start_x ? atoi(dbg_start_x) : ph.spawn_x;
    physics_reset(sx, ph.spawn_y);

    ai_init_from_level(level_num);
    /* Если стартовый X сдвинут — установим g_max_right_edge так, чтобы
     * враги в области стартового viewport-а активировались. */
    int init_view_left = sx - VIEW_W/2;
    if (init_view_left < 0) init_view_left = 0;
    ai_spawn_pending(init_view_left, VIEW_W, 0);

    if (load_demo) {
        int dr = demo_load(level_num);
        if (dr == 0) {
            printf("demo: загружено %u событий\n", demo_record_count());
        } else {
            printf("demo: LEVEL%u.MOV не найден (rc=%d) — без attract\n", level_num, dr);
            demo_clear();
        }
    } else {
        demo_clear();
        printf("demo: отключено (интерактив с клавиатуры)\n");
    }
    return 0;
}

/* ─── splash_run main loop ─────────────────────────────────────────── */

int splash_run(const splash_options_t *opt)
{
    bool mode_cga = (opt->video_mode == VIDEO_CGA);
    bool headless = (opt->ppm_path != NULL);

    u16 current_level = opt->level_num;
    int rc = prepare_level(current_level,
                            (current_level == 0) /* загружаем LEVEL0.MOV для attract */);
    if (rc != 0) return rc;

    /* Sprite cache */
    const char *sprite_file = mode_cga ? "karnov_sprites_cga.bin" : "karnov_sprites_ega.bin";
    if (try_load_sprites(sprite_file) != 0) {
        fprintf(stderr, "sprite_cache_load failed for %s\n", sprite_file);
        return 1;
    }
    printf("sprites: загружен data/%s (%d спрайтов)\n", sprite_file, sprite_cache_count());

    /* Audio: loader-music (track 6) — играет во время загрузки/инициализации.
     * Переключаем на in-game (track 0) после minimum-один-цикл loader-а ниже. */
    bool audio_running = false;
    if (opt->with_audio) {
        if (audio_init() == 0) {
            audio_play_music(6, true);
            audio_running = true;
            printf("audio: loader-music (track 6) запущена\n");
        }
    }

    /* Renderer */
    render_init(opt->scale, headless);

    /* Headless PPM mode: tick до целевого frame, рендер один кадр, сохранение. */
    if (headless) {
        u16 frame = 0;
        while (frame < opt->ppm_frame) {
            demo_tick(frame);
            u8 inp = demo_input_state();
            ai_tick(frame, inp);
            physics_set_current_frame(frame);
            physics_tick(inp);
            ai_try_pickup(physics_get_x(), physics_get_y());
            update_scroll(physics_get_x(), physics_get_y());
            ai_despawn_offscreen(g_scroll_x);
            ai_spawn_pending(g_scroll_x, VIEW_W, frame);
            ++frame;
        }
        /* Initial scroll для frame=0 (когда цикл не выполнился). */
        update_scroll(physics_get_x(), physics_get_y());
        u8 inp = demo_input_state();
        render_clear(0xFF000000u);
        draw_level(mode_cga);
        if (current_level == 1) draw_level1_dragon(g_scroll_x, g_scroll_y);
        draw_ladder((u16)opt->ppm_frame, g_scroll_x, g_scroll_y);
        ai_render(mode_cga, g_scroll_x, g_scroll_y);
        physics_render(inp, (u16)opt->ppm_frame, mode_cga, g_scroll_x, g_scroll_y);
        draw_hud((u16)opt->ppm_frame, physics_get_x(), inp);
        render_present();
        render_save_ppm(opt->ppm_path);
        printf("PPM записан в %s (frame=%d)\n", opt->ppm_path, opt->ppm_frame);
        render_shutdown();
        if (opt->with_audio) audio_close();
        return 0;
    }

    /* Ждём минимум один полный цикл loader-music, отображая copyright-
     * экран (как в оригинале KARNOV.EXE), затем переключаемся на in-game
     * background music (track 0). */
    bool quit = false;
    if (audio_running) {
        const char *line1 = "Karnov (C) 1987 Data East USA Inc.";
        const char *line2 = "Adaptation by Quicksilver Software Inc";
        int x1 = (WIN_W - (int)strlen(line1) * 6) / 2;
        int x2 = (WIN_W - (int)strlen(line2) * 6) / 2;
        render_clear(0xFF000000u);
        render_draw_text(x1, 130, line1, 0xFFFFFFFFu);
        render_draw_text(x2, 140, line2, 0xFFFFFFFFu);
        render_present();

        u32 deadline = SDL_GetTicks() + 30000;  /* safety cap 30s */
        bool skip = false;
        while (!quit && !skip && audio_cycle_count() < 1 && SDL_GetTicks() < deadline) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT ||
                    (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
                    quit = true;
                    break;
                }
                /* Любая другая клавиша — прервать музыку загрузки и начать игру. */
                if (ev.type == SDL_KEYDOWN) {
                    skip = true;
                    break;
                }
            }
            SDL_Delay(20);
        }
        if (!quit) {
            audio_play_music(0, true);
            printf("audio: in-game music (track 0) запущена%s\n",
                   skip ? " (загрузка прервана клавишей)" : "");
        }
    }

    /* Interactive main loop. tick_ms=36 ≈ 27.5 FPS (байт-верифицированная
     * частота из KARNOV.EXE think_cycle wait-loop). */
    u16 frame = 0;
    u16 s_demo_done_frame = 0;
    bool prev_start = false;
    u32 last_tick = SDL_GetTicks();
    const u32 tick_ms = 36;
    SDL_Event evt;

    printf("game loop: tick_ms=%u (%.1f FPS)\n", tick_ms, 1000.0 / tick_ms);

    while (!quit) {
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT) quit = true;
            if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) quit = true;
        }
        u32 now = SDL_GetTicks();
        if (now - last_tick >= tick_ms) {
            last_tick += tick_ms;
            demo_tick(frame);
            u8 demo_inp = demo_input_state();
            u8 kbd_only = 0;
            input_poll(&kbd_only);
            u8 inp = (kbd_only != 0) ? kbd_only : demo_inp;

            bool in_attract = demo_record_count() > 0;
            bool demo_done = in_attract &&
                             demo_current_index() >= demo_record_count();
            if (demo_done) {
                if (s_demo_done_frame == 0) s_demo_done_frame = frame;
            }

            /* ENTER → interactive LEVEL1 (skip attract chain). */
            if ((kbd_only & INP_START) && !prev_start &&
                (current_level == 0 || in_attract)) {
                printf("ENTER — interactive LEVEL1\n");
                if (prepare_level(1, /*load_demo=*/false) == 0) {
                    current_level    = 1;
                    g_scroll_x = g_scroll_y = 0;
                    frame      = 0;
                    inp        = 0;
                    s_demo_done_frame = 0;
                    prev_start = true;
                }
            } else if (demo_done && (frame - s_demo_done_frame) >= 60) {
                u16 next = (current_level == 0) ? 1 : 0;
                printf("attract: LEVEL%u demo done → LEVEL%u demo\n",
                       current_level, next);
                if (prepare_level(next, /*load_demo=*/true) == 0) {
                    current_level = next;
                    g_scroll_x = g_scroll_y = 0;
                    frame = 0;
                    s_demo_done_frame = 0;
                }
            }
            prev_start = (kbd_only & INP_START) != 0;

            ai_tick(frame, inp);
            physics_set_current_frame(frame);
            bool was_dying = physics_is_dying();
            physics_tick(inp);
            /* Karnov только что вошёл в death-state (контакт с водой):
             * запускаем death-music (track 5, не зацикленную). По окончании
             * анимации physics возвращает is_dying=false, и мы возобновляем
             * in-game music (track 0). */
            if (audio_running) {
                if (physics_just_died()) {
                    audio_play_music(5, /*loop=*/false);
                } else if (was_dying && !physics_is_dying()) {
                    audio_play_music(0, /*loop=*/true);
                }
            }
            ai_try_pickup(physics_get_x(), physics_get_y());

            /* Boss death → scroll picked → переход на LEVEL2 (или loop). */
            if (ai_scroll_picked() && current_level >= 1) {
                u16 next = (current_level < 9) ? (u16)(current_level + 1) : 0;
                printf("LEVEL%u: scroll подобран → LEVEL%u\n", current_level, next);
                if (prepare_level(next, /*load_demo=*/false) == 0) {
                    current_level = next;
                    g_scroll_x = g_scroll_y = 0;
                    frame = 0;
                    s_demo_done_frame = 0;
                    continue;
                }
            }

            update_scroll(physics_get_x(), physics_get_y());
            ai_despawn_offscreen(g_scroll_x);
            ai_spawn_pending(g_scroll_x, VIEW_W, frame);
            render_clear(0xFF000000u);
            draw_level(mode_cga);
            if (current_level == 1) draw_level1_dragon(g_scroll_x, g_scroll_y);
            draw_ladder(frame, g_scroll_x, g_scroll_y);
            ai_render(mode_cga, g_scroll_x, g_scroll_y);
            physics_render(inp, frame, mode_cga, g_scroll_x, g_scroll_y);
            draw_hud(frame, physics_get_x(), inp);
            render_present();

            ++frame;
        } else {
            SDL_Delay(1);
        }
    }

    render_shutdown();
    if (opt->with_audio) audio_close();
    return 0;
}
