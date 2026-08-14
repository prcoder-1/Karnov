/* physics.c — Karnov state + tick + render.
 *
 * Восстановлено из cs:0xFFA:0x1A1A (PRE-Karnov phase) и cs:0xFFA:0x1C04
 * (POST-Karnov phase). См. memory project_physics_ai.md.
 */
#include "physics.h"
#include "render.h"
#include "sprite.h"
#include "palette.h"
#include "inventory.h"
#include "ai.h"
#include "level.h"
#include <stdio.h>
#include <stdlib.h>

/* Константы физики (Q4 fixed-point для vy).
 * JUMP_VY_Q4 = -80: байт-верифицировано из KARNOV.EXE state 2 TICK
 *   (file 0x118CE: mov [bx+0x14], 0xFFFB = vy_hi=-5 → -5.0 px/frame Q16.16).
 * Q4 эквивалент: -5.0 × 16 = -80.
 * Оригинал использует state-based vy switching (jump→-5, fall→+5, on_ground→0)
 * вместо нашего gradual gravity accumulation. Для LEVEL0 splash это не критично
 * (Karnov не прыгает), но для in-game gameplay требует пересмотра. */
/* s_ground_y / s_ceiling_y / WORLD_MIN_X / WORLD_MAX_X — мировые ограничители
 * физики Karnov. Дефолты — LEVEL0 splash (узкий 320×168). При переходе на
 * широкий уровень game.c вызывает physics_set_world_bounds() с новыми
 * значениями (например, ground_y=240 и max_x=3590 для LEVEL1). */
static int s_ground_y    = 115;
static int s_ceiling_y   = 75;
static int s_world_min_x = 8;
static int s_world_max_x = 312;
#define JUMP_VY_Q4  (-80)   /* -5.0 px/frame × 16 (Q4); was -64 (= -4.0) */
#define GRAVITY_Q4    4
#define VY_MIN      (-96)
#define VY_MAX        80    /* +5.0 × 16 = 80 (fall velocity, was 64) */
/* WALK_SPEED — байт-верифицировано из KARNOV.EXE ai_type 2 handler:
 *   state 2 TICK (file 0x1190D+0x11B):
 *     bx = direction_bits (DS:0xCCAC) & 0x3
 *     dx:ax = DS:[0x1348 + bx*16]              ; Q16.16 velocity delta lookup
 *     lshift_long(dx:ax, cx=1)                  ; × 2
 *     karnov.x_q16 += dx:ax                     ; accumulate
 *   Table @ DS:0x1348 (file 0x204D8):
 *     dir==0 (idle): vx=0
 *     dir==1 (R):    vx=+2.0 → ×2 = +4 px/frame
 *     dir==2 (L):    vx=-2.0 → ×2 = -4 px/frame
 *     dir==3 (R+L):  vx=0 (cancel) */
#define WALK_SPEED    4
/* CLIMB_SPEED = 4 — байт-верифицировано: 2-я velocity table @ DS:0x1388
 * (file 0x20518) содержит U/D векторы с vy=±2.0 Q16.16 → ×2 lshift = ±4 px/frame.
 * Эквивалентно WALK_SPEED для согласованности. С 28 U-frames Karnov поднимается
 * 28*4=112 px; clamped к LADDER_TOP_Y=45 (≈16 frames до top). */
#define CLIMB_SPEED   4

/* Flight (wings) — упрощённая физика без гравитации, 4-направленное
 * движение. Скорость подобрана «на ощупь» — близка к WALK_SPEED по
 * горизонтали для согласованности, по вертикали чуть медленнее, чтобы
 * полёт чувствовался «парящим». Байт-верификации из ai_type03 пока нет
 * (см. inventory.c TODO про точный таймер). */
#define FLY_SPEED_X   4
#define FLY_SPEED_Y   3

/* Cooldown между выстрелами в game-ticks. KARNOV.EXE (verified 2026-07-10):
 * FIRE НЕ имеет фикс. cooldown-счётчика — стрельба гейтится состоянием
 * fire-анимации Karnov'а (`cmp [obj_state],4; skip` @0x10BF8; fire-функция
 * @0x104E6 → play_sound(7)). Т.е. выстрел вылетает как только Karnov НЕ в
 * позе броска и свободен слот снаряда → быстрый повторный огонь. Прежние 8
 * тиков (~3.5/сек) были слишком медленны; 4 ≈ ~7/сек, ближе к оригиналу. */
#define FIRE_COOLDOWN_TICKS  4

/* Hitbox лестницы: ширина в которой Karnov цепляется (+/- от ladder_x). */
#define LADDER_HALF_W 10
/* LADDER_TOP_Y — DOSBox-verified (2026-05-25):
 * В оригинале при t=15s ladder rails видны в y_range game=45..114 (= 69 px tall).
 * Прошлый 78 был неверен — ладдер должен раскладываться значительно выше
 * (от ground y=114 до top y=45, проходя сквозь область KARNOV-текста).
 *
 * LADDER_HEIGHT — 70 px (тот же диапазон 45..115 при ground=115). Для
 * LEVEL1 при ground_y=240 верх лестницы будет y=170. */
#define LADDER_HEIGHT 70
#define LADDER_TOP_Y  (s_ground_y - LADDER_HEIGHT)
#define LADDER_BOT_Y  s_ground_y

/* Initial Karnov position для LEVEL0 SPLASH (с большой надписью KARNOV) —
 * DOSBOX-verified (2026-05-25): pixel detection на actual title screen даёт
 *   Karnov red cluster: x=64..75 (center=69), y=92..108 (feet=108)
 * Karnov STATIC throughout entire attract: LEVEL0.MOV не двигает его в
 * оригинале (см. project_dosbox_splash_reality.md).
 * Ground в LEVEL0 splash: y=108-110 (НЕ y=115 как было в нашем порте). */
/* Karnov initial X = 137 — байт-верифицировано (2026-05-25):
 * с WALK_SPEED=4 (из velocity table @ DS:0x1348) и net -22 frames в LEVEL0.MOV:
 *   137 - 27*4 = 29 (после L f=22..48, нет wall hit)
 *   29 + 6*4 = 53 (после R f=58..63)
 *   53 - 1*4 = 49 (после L f=64)
 *   ladder placed at x=49 ✓ совпадает с DOSBox title_04 measurement */
static int  s_x = 137;
static int  s_y = 108;
static int  s_vy_q4 = 0;
static int  s_facing = 1;       /* +1 right, -1 left */
static bool s_on_ground = true;

/* Lestn — состояние лестницы. */
static bool s_ladder_placed       = false;
static int  s_ladder_x            = 0;
static u16  s_ladder_placed_frame = 0;
static bool s_on_ladder           = false;
static bool s_prev_opt            = false;   /* для edge-detection B-нажатия */
static u16  s_cur_frame           = 0;
/* Счётчик кадров лазанья — инкрементируется ТОЛЬКО когда Karnov реально
 * движется по лестнице (нажат U или D). Гарантирует, что в idle-pose Karnov
 * висит неподвижно, как и в оригинале (sub_timer не тикает без edge-input). */
static u16  s_climb_ticks         = 0;
/* Прошлое значение U-нажатия на лестнице — позволяет различать
 * «слез к низу через climb-down» (бери лестницу обратно) и «слетел вниз
 * по гравитации после прыжка/отвалу» (лестница потеряна). */
static bool s_climbing_down_prev  = false;
static bool s_prev_u              = false;   /* edge-detect для JUMP */
/* True во время прыжка (от JUMP-press до приземления). Различает «прыгнул»
 * (показывать K_JUMP_* всё время прыжка, в т.ч. на спуске) от «свалился
 * с высоты» (K_FALL_*). Сбрасывается при приземлении на пол. */
static bool s_in_jump             = false;
/* Cooldown между выстрелами. Декрементируется каждый тик; FIRE-press
 * на cooldown==0 спаунит снаряд и сбрасывает cooldown в FIRE_COOLDOWN_TICKS. */
static u16  s_fire_cooldown       = 0;
/* Кэш «летает ли Karnov» — synced с inv_flying() в начале каждого тика,
 * чтобы post-OPT-handling корректно выбирать ветку физики. */
static bool s_flying              = false;

/* Spawn-точка уровня — кэшируется physics_reset, используется для respawn
 * после death-анимации (контакт с водой). */
static int  s_spawn_x             = 137;
static int  s_spawn_y             = 108;

/* Death state. Triggered by water-contact. Karnov играет 4-кадровую
 * анимацию (K_DIE_1..4) DEATH_LEN тиков (~2.2 sec @ 27.5 FPS),
 * после чего respawn'ится в s_spawn_x/y. game.c (через physics_just_died)
 * проигрывает death-music (track 5) на старте анимации. */
static bool s_dying               = false;
static u16  s_dying_frame         = 0;
static bool s_just_died_pulse     = false;
/* Неуязвимость: включается на время victory-секвенции (boss убит, Karnov
 * авто-идёт к свитку), чтобы вода/опасности не убили его во время прохода. */
static bool s_invuln              = false;
/* Level-end: Karnov сидит со свитком (front-facing поза #0x60-0x62), а не
 * обычный crouch. Включается на время s_level_end. */
static bool s_sit_scroll          = false;
#define DEATH_LEN  60   /* ≈ 2.2 sec @ 27.5 FPS */

/* Sprite-slot mapping для Karnov (пользователь-verified из оригинала
 * KARNOV.EXE, 2026-05-27). Спрайты содержатся как РАЗДЕЛЬНЫЕ left/right
 * пары — не нужно flip'ать при рендере: pick_sprite_id возвращает уже
 * правильную ориентацию по s_facing.
 *
 * Полный inventory:
 *   95..97   — анимация появления (spawn) — НЕ используется пока.
 *   98       — стоит вправо
 *   99..102  — ходьба вправо (4 кадра)
 *   103      — сидит лицом вправо
 *   104      — прыгает вправо
 *   105      — стреляет вправо
 *   106      — стоит влево
 *   107..110 — ходьба влево (4 кадра)
 *   111      — сидит лицом влево  (по симметрии: user typo'd как #114)
 *   112      — прыгает влево
 *   113      — стреляет влево
 *   114..117 — анимация гибели — НЕ используется пока.
 *   118      — падает лицом вправо
 *   119      — падает лицом влево
 *   120..122 — анимация лазанья по лестнице (3 кадра, цикл AnimSeg
 *              DS:0x1524 = [121, 122, 121, 120])
 *   123      — Karnov наверху лестницы (стационарный кадр)
 *   124..125 — летит вправо на крыльях (2 кадра)
 *   126..127 — летит влево на крыльях (2 кадра)
 *   128..131 — плавает вправо/влево (НЕ используется — water levels не реал.)
 *   132..143 — плавание/маска (НЕ используется). */
enum {
    /* RIGHT-facing */
    K_STAND_R = 98,
    K_WALKR_1 = 99,  K_WALKR_2 = 100, K_WALKR_3 = 101, K_WALKR_4 = 102,
    K_CROUCH_R = 103,
    K_JUMP_R = 104,
    K_FIRE_R = 105,
    /* LEFT-facing */
    K_STAND_L = 106,
    K_WALKL_1 = 107, K_WALKL_2 = 108, K_WALKL_3 = 109, K_WALKL_4 = 110,
    K_CROUCH_L = 111,
    K_JUMP_L = 112,
    K_FIRE_L = 113,
    /* DEATH — 4-кадровая анимация утопления/гибели. */
    K_DIE_1 = 114, K_DIE_2 = 115, K_DIE_3 = 116, K_DIE_4 = 117,
    /* FALL — отличаются от JUMP'ов: используются при vy_q4 >= 0 (descending) */
    K_FALL_R = 118,
    K_FALL_L = 119,
    /* LADDER cycle + at-top pose */
    K_CLIMB_A = 120, K_CLIMB_B = 121, K_CLIMB_C = 122,
    K_CLIMB_TOP = 123,
    /* FLY (крылья) */
    K_FLYR_1 = 124, K_FLYR_2 = 125,
    K_FLYL_1 = 126, K_FLYL_2 = 127,
};

/* LEVEL0 splash имеет статичные «платформы» — буквы KARNOV в верхней
 * полосе. Тайлы букв в LEVEL0.BIN имеют attr=02, что НЕ удовлетворяет
 * `attr & 1` (рабочий solid-критерий из KARNOV.EXE для LEVEL1+). Чтобы
 * Karnov мог ходить по верху букв после подъёма по лестнице (как в
 * оригинальной заставке, где он MOV-driven), захардкодим overlay:
 *   y_top=45  (top of letter row 4), x_range=[48, 240]  (cols 3..14).
 * Извлечено из LEVEL0 map: letters в rows 4..6 cols 3..14. */
static int s_level0_splash = 0;
/* LEVEL1 start — slim каменная платформа слева, где сидит большой brown
 * dragon. Это «солидная поверхность» поверх обычного ground_y=290. Karnov
 * стоит на ней рядом с драконом. Match'ит оригинал DOSBox: stone slab
 * extends x≈0..130, top y=260. После x=130 — abyss с обычным brick floor
 * (y=290) внизу. */
static int s_level1_dragon_platform = 0;
#define LVL1_PLAT_X0   0     /* world x_min платформы */
#define LVL1_PLAT_X1 191     /* world x_max — соответствует визуальной протяжённости
                              * каменной платформы в row 26 (cols 0..11, x=0..191).
                              * Раньше 130 — Karnov проваливался посреди gray-stone
                              * визуально (col 8.5 вместо col 11). */
#define LVL1_PLAT_TOP_Y 260  /* верх платформы (на чём стоит Karnov/дракон) */

/* find_floor_y — порт алгоритма KARNOV.EXE «найти solid-тайл под объектом».
 *
 * Декомпил оригинала (восстановлено из дизасма KARNOV.EXE):
 *   cs:0xD4B:0x6CB (file 0xF37B)  move_with_collision(obj, dx, dy)
 *     — субдивидит движение на шаги ≤TILE_W (16) по X или ≤TILE_H (10) по Y,
 *       для каждого шага вызывает probe_tile.
 *   cs:0xD4B:0x564 (file 0xF214)  probe_tile(x, y, dx, dy, mask, out…)
 *     — для движения ВНИЗ снапит y к НИЖНЕЙ границе текущей строки:
 *         y_snap = (y / TILE_H) * TILE_H + (TILE_H - 1)
 *       для движения ВВЕРХ — к верхней границе той же строки.
 *       Затем читает tile_id через get_tile_id(x, y_snap) и проверяет
 *       (tile_props[tile_id] AND mask) == mask. Если да — коллизия,
 *       объект снапится на границу.
 *   cs:0xD4B:0x4F2 (file 0xED0A)  get_tile_id(world_x, world_y)
 *     — row = y_lut[y*2]   (DS:[-0x50DE], эквивалент y / TILE_H)
 *       col = x >> [0xB828] (=x >> 4 = x / TILE_W)
 *       row_ptr = far_row_table[row]  (DS:[-0x52AE], 4 байта на строку)
 *       tile_id = row_ptr[col*2 + 1]   (stride 2 байта на ячейку)
 *
 * Маска: Karnov-плеер вызывает с mask=0x0001 (bit 0 = solid floor).
 * Для других объектов (e.g. projectile, enemy) маска может быть другой.
 *
 * Наша реализация: для одного столбца wx сканируем строки сверху-вниз
 * от wy_above до уровня пола (s_ground_y), возвращаем TOP_Y первого
 * solid тайла. Эквивалентно итерации probe_tile вниз с шагом TILE_H.
 *
 * Хаки для LEVEL0 splash (буквы KARNOV) и LEVEL1 dragon platform
 * остаются: в level-данных эти тайлы НЕ помечены attr&1 (визуальная
 * декорация), но в оригинале player на них стоит — вероятно, через
 * иной механизм (per-level overlay в данных, которые мы не расшифровали). */
static int find_floor_y(int wx, int wy_above)
{
    /* Дефолт — самая нижняя строка карты. Karnov «проваливается до конца
     * map data», если в его колонке нет ни одного solid тайла (LEVEL2
     * bottomless pits). Раньше fallback был s_ground_y — это удерживало
     * Karnov-а на surface-уровне даже над чёрными провалами в подземелье. */
    int best = (level_num_map_rows > 0)
                 ? ((int)level_num_map_rows - 1) * TILE_H
                 : s_ground_y;
    if (best < s_ground_y) best = s_ground_y;  /* LEVEL0 splash (map < ground_y). */

    /* (1) LEVEL0 splash overlay — KARNOV letter platform (engine workaround). */
    if (s_level0_splash) {
        if (wx >= 48 && wx <= 240 && wy_above <= 45) {
            if (45 < best) best = 45;
        }
    }

    /* (1b) LEVEL1 start overlay — stone platform под драконом (engine workaround). */
    if (s_level1_dragon_platform) {
        if (wx >= LVL1_PLAT_X0 && wx <= LVL1_PLAT_X1 && wy_above <= LVL1_PLAT_TOP_Y) {
            if (LVL1_PLAT_TOP_Y < best) best = LVL1_PLAT_TOP_Y;
        }
    }

    /* (2) Tile-attr scan — точный порт probe_tile (file 0xF214) с iteration
     * по строкам. Делегируем solid-test в level_solid_at (LUT-based с
     * Karnov-mask 0x0006).
     *
     * Сканируем ВСЕ строки карты (row0..num_rows-1), а не только до
     * s_ground_y/TILE_H. Это позволяет LEVEL2-подземелью «принимать»
     * Karnov-а, когда он падает сквозь чёрный pit в surface-строке 31:
     * loop continues в строки 32..55 и находит solid floor подземелья
     * (≈ y=440..490). */
    int row0 = wy_above / TILE_H;
    if (row0 < 0) row0 = 0;
    int row_max = (int)level_num_map_rows - 1;
    for (int r = row0; r <= row_max; ++r) {
        int top_y = r * TILE_H;
        /* probe_tile эквивалент: проверяем тайл В ЭТОЙ строке (нижняя
         * кромка top_y+TILE_H-1 → тот же tile_row через /TILE_H деление). */
        if (level_solid_at(wx, top_y + 1)) {
            if (top_y < best) best = top_y;
            break;
        }
    }
    return best;
}

void physics_set_world_bounds(int min_x, int max_x, int ground_y, int ceiling_y)
{
    s_world_min_x = min_x;
    s_world_max_x = max_x;
    s_ground_y    = ground_y;
    s_ceiling_y   = ceiling_y;
}

void physics_set_level0_splash(bool enable) { s_level0_splash = enable ? 1 : 0; }

/* Обновить точку респауна БЕЗ полного сброса состояния. Используется
 * checkpoint-логикой смерти (KARNOV.EXE file 0x10019): точка возрождения
 * берётся из ObjectRegion по позиции гибели, а не из старта уровня. */
void physics_set_spawn_point(int x, int y)
{
    s_spawn_x = x;  s_spawn_y = y;
}
void physics_set_level1_dragon_platform(bool enable) {
    s_level1_dragon_platform = enable ? 1 : 0;
}

void physics_reset(int x, int y)
{
    s_x = x; s_y = y; s_vy_q4 = 0; s_facing = 1; s_on_ground = (y >= s_ground_y);
    s_spawn_x = x;  s_spawn_y = y;          /* для respawn после death */
    s_ladder_placed = false;
    s_ladder_x = 0;
    s_ladder_placed_frame = 0;
    s_on_ladder = false;
    s_prev_opt = false;
    s_cur_frame = 0;
    s_climb_ticks = 0;
    s_climbing_down_prev = false;
    s_fire_cooldown = 0;
    s_flying = false;
    s_prev_u = false;
    s_in_jump = false;
    s_dying = false;
    s_dying_frame = 0;
    s_just_died_pulse = false;
    s_invuln = false;
    inv_reset();                 /* пустой инвентарь — pickup обязателен */
    /* для отладки можно зафиксировать позицию через env: */
    const char *e = getenv("KARNOV_FIX_POS");
    if (e) { int xx, yy; if (sscanf(e, "%d,%d", &xx, &yy) == 2) { s_x = xx; s_y = yy; } }
}

void physics_set_current_frame(u16 frame) { s_cur_frame = frame; }

/* Karnov стоит в hitbox'е лестницы по горизонтали? */
static bool in_ladder_column(void)
{
    if (!s_ladder_placed) return false;
    int dx = s_x - s_ladder_x;
    if (dx < 0) dx = -dx;
    return dx <= LADDER_HALF_W;
}

void physics_tick(u8 input)
{
    /* Сбрасываем «just_died»-импульс предыдущего кадра. game.c прочитает
     * его в той же тике, в которой был установлен. */
    s_just_died_pulse = false;

    /* Death animation тикает без обработки input. По окончании DEATH_LEN
     * Karnov respawn'ится в s_spawn_x/y. */
    if (s_dying) {
        ++s_dying_frame;
        if (s_dying_frame >= DEATH_LEN) {
            int sx = s_spawn_x, sy = s_spawn_y;
            physics_reset(sx, sy);
        }
        return;
    }

    bool wants_r    = (input & INP_R)    != 0;
    bool wants_l    = (input & INP_L)    != 0;
    bool wants_u    = (input & INP_U)    != 0;
    bool wants_d    = (input & INP_D)    != 0;
    bool wants_fire = (input & INP_FIRE) != 0;
    bool opt_press  = (input & INP_OPT)  != 0;
    bool opt_edge   = opt_press && !s_prev_opt;   /* передний фронт B */
    s_prev_opt = opt_press;

    /* Inventory & таймер полёта — тикают каждый кадр.
     * inv_tick декрементит fly-timer; когда он достигает 0, inv_flying()
     * возвращает false → ниже нормальная физика подберёт vy=0 и применит
     * gravity, мягко уронив Karnov на землю. */
    inv_tick();
    s_flying = inv_flying();

    /* 1. Edge-handling OPT (single «use option» button, INP_OPT = Z).
     *    Оригинал KARNOV.EXE: OPT + LEFT/RIGHT = CYCLE выбора предмета;
     *    OPT без направления = USE выбранного. Эффекты не-физических
     *    предметов применяются внутри inv_use(); LADDER/WINGS возвращают
     *    id, а размещение/полёт делаем здесь. */
    if (opt_edge) {
        if (wants_l || wants_r) {
            /* Переключить выбор предмета, НЕ используя его. */
            inv_cycle(wants_r ? +1 : -1);
        } else {
            int used = inv_use();
            if (used == ITEM_WINGS) {
                /* Активирует полёт (fly_timer уже выставлен в inv_use).
                 * Здесь мирроим прежнее поведение: отлепляем лестницу,
                 * гасим vy, выходим из jump-state. */
                s_flying    = true;
                s_on_ladder = false;
                s_vy_q4     = 0;
                s_in_jump   = false;          /* полёт прерывает прыжок */
            } else if (used == ITEM_LADDER) {
                /* Лестница ставится только стоя на земле и без уже
                 * поставленной. Если условие не выполнено — предмет уже
                 * списан в inv_use(); визуально лестница не появляется
                 * (как «потеря» при неудачной постановке). */
                if (s_on_ground && !s_ladder_placed && !s_on_ladder) {
                    s_ladder_placed       = true;
                    s_ladder_x            = s_x;
                    s_ladder_placed_frame = s_cur_frame;
                }
            }
        }
    }

    /* 2. Решаем, на лестнице ли Karnov. На лестницу можно «прицепиться»:
     *      - стоя в её колонке (по X) и нажимая U  (заход с земли вверх)
     *      - или уже находясь на ней и не вышедшим за края
     *   Сходим с лестницы при движении L/R или при достижении земли.
     *   В режиме полёта лестница игнорируется (Karnov свободно летит). */
    if (s_flying) {
        s_on_ladder = false;
    } else if (s_ladder_placed && in_ladder_column()) {
        if (!s_on_ladder && wants_u && s_y >= LADDER_TOP_Y) {
            s_on_ladder = true;
            s_vy_q4 = 0;
            s_on_ground = false;
        }
    } else {
        s_on_ladder = false;
    }
    if (s_on_ladder && (wants_l || wants_r)) {
        /* Шаг в сторону = сходим с лестницы (физика вернёт гравитацию).
         * Лестница ОСТАЁТСЯ разложенной — но из инвентаря она уже исчезла,
         * так что Karnov её потерял, как и заказывал user-spec. */
        s_on_ladder = false;
    }

    /* 3. Управление. */
    if (s_flying) {
        /* === FLIGHT MODE ===
         * Свободное 4-направленное движение без гравитации.
         * Границы: s_ceiling_y..s_ground_y по Y, 8..312 по X. */
        if (wants_r) { s_x += FLY_SPEED_X; s_facing =  1; }
        if (wants_l) { s_x -= FLY_SPEED_X; s_facing = -1; }
        if (wants_u)   s_y -= FLY_SPEED_Y;
        if (wants_d)   s_y += FLY_SPEED_Y;
        /* Karnov больше не «на земле» в полёте. */
        s_on_ground = false;
        s_vy_q4     = 0;
        if (s_y > s_ground_y)  s_y = s_ground_y;
        if (s_y < s_ceiling_y) s_y = s_ceiling_y;
    } else if (s_on_ladder) {
        /* === ON LADDER ===
         * Вертикальное движение по лестнице, без гравитации. */
        bool moving = false;
        if (wants_u) { s_y -= CLIMB_SPEED; moving = true; }
        if (wants_d) { s_y += CLIMB_SPEED; moving = true; s_climbing_down_prev = true; }
        else         s_climbing_down_prev = wants_u ? false : s_climbing_down_prev;
        /* Animation gate: счётчик растёт только когда Karnov реально
         * перебирает руками. Поведение совпадает с оригиналом, где
         * sub_timer на ladder-handler'е тикает только при U/D-edge. */
        if (moving) ++s_climb_ticks;
        if (s_y <= LADDER_TOP_Y) {
            s_y = LADDER_TOP_Y;
            /* User-spec: «долез до конца — должен встать». При climb-up,
             * достигнув верха лестницы, Karnov автоматически слезает —
             * переходит в on-ground стоячую позу. Если под ногами нет
             * платформы, walk-off-edge/gravity подхватит на следующем тике. */
            if (wants_u) {
                s_on_ladder = false;
                s_on_ground = true;
                s_vy_q4 = 0;
                s_in_jump = false;
                s_climbing_down_prev = false;
            }
        }
        if (s_on_ladder && s_y >= LADDER_BOT_Y) {
            s_y = LADDER_BOT_Y;
            s_on_ladder = false;
            s_on_ground = true;
            s_vy_q4 = 0;
            s_in_jump = false;
            /* === LADDER RETRIEVE ===
             * Если Karnov действительно слез ВНИЗ (был climbing-down в этом
             * или предыдущем кадре), лестница складывается и возвращается
             * в инвентарь. Иначе (например, если слетел сверху без climb-
             * down) — лестница остаётся в мире и теряется навсегда. */
            if (s_climbing_down_prev) {
                s_ladder_placed = false;
                inv_take_back_ladder();
            }
            s_climbing_down_prev = false;
        }
    } else {
        /* === NORMAL PHYSICS ===
         * Ходьба, прыжок, гравитация + tile-collision (платформы). */

        /* Horizontal walk + wall-collision: Karnov body bbox ≈ x±7,
         * y-28..y-1. Если leading edge у solid тайла на любой высоте тела —
         * блок. Это позволяет Karnov-у вставать на каменные столбы
         * (LEVEL1 x=512..540 top=280, x=544..575 top=270): подойти к
         * столбу слева → блок, прыгнуть UP → подняться выше top → пройти
         * горизонтально → приземлиться на столб через find_floor_y. */
        int dx_walk = 0;
        /* User-spec: «не должен перемещаться направо или налево, пока сидит».
         * При crouch (wants_d on ground) блокируем горизонтальное движение И
         * смену facing — чтобы повернуться надо сначала встать. */
        bool crouching = wants_d && s_on_ground;
        if (!crouching) {
            if (wants_r) { dx_walk += WALK_SPEED; s_facing =  1; }
            if (wants_l) { dx_walk -= WALK_SPEED; s_facing = -1; }
        }
        if (dx_walk != 0) {
            int step = (dx_walk > 0) ? 1 : -1;
            int remain = (dx_walk > 0) ? dx_walk : -dx_walk;
            for (int i = 0; i < remain; ++i) {
                int probe_x = s_x + step;
                int lead_x = probe_x + (step > 0 ? +7 : -7);
                /* Sample body at 3 heights — bottom, middle, top.
                 * Top probe at s_y-20 (bbox ≈ 20 px), а не -28: визуальный
                 * sprite Karnov-а ~29 px, но collision-bbox в KARNOV.EXE
                 * короче — иначе голова застревает в overhang'ах.
                 *
                 * Используем level_blocks_walk_h_at — горизонтальную маску
                 * 0x001F (passes attrs 0..4 включая walls=attr3). Карnov
                 * проходит сквозь каменные столбы боком, но всё ещё стоит
                 * на их вершинах через find_floor_y (KARNOV_WALK_MASK 0x0006).
                 * One-way platform behaviour. Блокируют только специальные
                 * obstacles с attr 5..9 (бочки, шипы и т.п.). */
                bool blocked = level_blocks_walk_h_at(lead_x, s_y - 4) ||
                               level_blocks_walk_h_at(lead_x, s_y - 12) ||
                               level_blocks_walk_h_at(lead_x, s_y - 20);
                if (blocked) break;
                s_x = probe_x;
            }
        }
        /* Прыжок — edge-triggered: только когда U только что нажат и Karnov
         * на земле. Без edge'а UP-press во время climb-up→dismount→walk
         * последовательности сразу прыгал, и ceiling-clamp срезал Karnov-а
         * в нижнюю часть экрана (LEVEL0 splash bug). s_in_jump=true помечает
         * «это прыжок» — pick_sprite_id будет показывать K_JUMP даже на
         * descent'е, в отличие от падения-без-прыжка (K_FALL). */
        if (wants_u && !s_prev_u && s_on_ground) {
            /* Дизассемблирование KARNOV.EXE: базовый jump vy = -5.0 px/frame
             * (JUMP_VY_Q4 = -80 в Q4). BOOTS удваивают силу прыжка → -10.0
             * (JUMP_VY_Q4 * 2), пока boots-эффект активен (таймер). */
            s_vy_q4 = inv_boots_active() ? (JUMP_VY_Q4 * 2) : JUMP_VY_Q4;
            s_on_ground = false;
            s_in_jump = true;
        }

        s_vy_q4 += GRAVITY_Q4;
        if (s_vy_q4 < VY_MIN) s_vy_q4 = VY_MIN;
        if (s_vy_q4 > VY_MAX) s_vy_q4 = VY_MAX;
        int new_y = s_y + s_vy_q4 / 16;

        if (s_vy_q4 >= 0) {
            /* Падаем (или стоим). Ищем верх первого solid-тайла под нами
             * между текущей y и new_y. find_floor_y вернёт y этого пола
             * либо s_ground_y, если ничего не нашёл. */
            int floor_y = find_floor_y(s_x, s_y);
            if (new_y >= floor_y) {
                s_y = floor_y;
                s_vy_q4 = 0;
                s_on_ground = true;
                s_in_jump = false;       /* приземлились — выходим из jump-state */
            } else {
                s_y = new_y;
                /* Покинули пол? Если нет ни тайла ниже, ни default-ground в
                 * ближайшем тике — мы в воздухе. Здесь не сбрасываем
                 * on_ground т.к. new_y < floor_y → ещё не достигли пола. */
                s_on_ground = false;
            }
        } else {
            /* Прыгаем вверх — гравитация замедляет. Ceiling clamp. */
            s_y = new_y;
            if (s_y < s_ceiling_y) { s_y = s_ceiling_y; s_vy_q4 = 0; }
            s_on_ground = false;
        }

        /* Walk-off-edge: если стояли на платформе и ушли в сторону, текущий
         * floor может оказаться ниже → стартуем падение. */
        if (s_on_ground) {
            int floor_now = find_floor_y(s_x, s_y);
            if (floor_now > s_y) {
                s_on_ground = false;
                /* vy_q4=0 — мягкий старт падения; гравитация подтянет. */
            }
        }
    }

    /* 4. Стрельба: на удержание FIRE спаунит снаряд с rate-limit. На
     * лестнице стрелять нельзя (как в оригинале — обе руки заняты).
     * В полёте можно. */
    if (s_fire_cooldown > 0) --s_fire_cooldown;
    if (wants_fire && s_fire_cooldown == 0 && !s_on_ladder) {
        /* Высота снаряда — на уровне торса (≈ y - 14). Точка выхода —
         * чуть впереди тела по facing. */
        int fx = s_x + (s_facing < 0 ? -10 : +10);
        int fy = s_y - 14;
        ai_spawn_player_fireball(fx, fy, s_facing, s_cur_frame);
        s_fire_cooldown = FIRE_COOLDOWN_TICKS;
    }

    if (s_x < s_world_min_x) s_x = s_world_min_x;
    if (s_x > s_world_max_x) s_x = s_world_max_x;
    s_prev_u = wants_u;

    /* Water contact = death. Проверяем тайл прямо под ногами Karnov-а
     * (s_y — base/feet); если это вода (attr=08), стартуем dying-state.
     * В полёте контакт с водой тоже убивает — это совпадает с поведением
     * оригинала. */
    if (!s_invuln && !s_dying && level_is_water_at(s_x, s_y)) {
        s_dying = true;
        s_dying_frame = 0;
        s_just_died_pulse = true;
        s_on_ladder = false;
        s_flying = false;
        s_vy_q4 = 0;
        s_in_jump = false;
    }
}

bool physics_just_died(void) { return s_just_died_pulse; }
bool physics_is_dying(void)  { return s_dying; }
void physics_set_invuln(bool on) { s_invuln = on; }
void physics_set_sit_scroll(bool on) { s_sit_scroll = on; }

int  physics_get_x(void)       { return s_x; }
int  physics_get_y(void)       { return s_y; }
int  physics_get_ground_y(void) { return s_ground_y; }
bool physics_facing_left(void) { return s_facing < 0; }

bool physics_ladder_placed(void)      { return s_ladder_placed; }
int  physics_ladder_x(void)           { return s_ladder_x; }
int  physics_ladder_top(void)         { return LADDER_TOP_Y; }
int  physics_ladder_bot(void)         { return LADDER_BOT_Y; }
u16  physics_ladder_placed_frame(void){ return s_ladder_placed_frame; }
bool physics_on_ladder(void)          { return s_on_ladder; }

/* Простой плейсхолдер если sprite не найден. */
static void render_placeholder(int cx, int cy, u8 input, u16 frame, bool walking)
{
    (void)frame;
    u32 SKIN = 0xFFFF55FFu, BODY = 0xFFAA0000u, BOOT = 0xFFAA5500u;
    int x0 = cx - 6, y0 = cy - 22;
    render_fill_rect(x0+4, y0,   5, 2, 0xFF000000u);  /* hair */
    render_fill_rect(x0+3, y0+2, 7, 4, SKIN);          /* face */
    render_fill_rect(x0+1, y0+7, 11,6, BODY);          /* torso */
    if (input & INP_D) {
        render_fill_rect(x0+2, y0+15, 4, 3, SKIN);
        render_fill_rect(x0+7, y0+15, 4, 3, SKIN);
        render_fill_rect(x0+2, y0+18, 4, 2, BOOT);
        render_fill_rect(x0+7, y0+18, 4, 2, BOOT);
    } else {
        int dl = walking ? 1 : 0;
        render_fill_rect(x0+2+dl, y0+15, 4, 5, SKIN);
        render_fill_rect(x0+7-dl, y0+15, 4, 5, SKIN);
        render_fill_rect(x0+2+dl, y0+20, 4, 2, BOOT);
        render_fill_rect(x0+7-dl, y0+20, 4, 2, BOOT);
    }
    if (input & INP_FIRE) {
        int fx = (input & INP_L) ? x0 - 5 : x0 + 13;
        render_fill_rect(fx, y0+8, 5, 4, BODY);
    }
}

/* Выбор кадра анимации по input/состоянию + facing.
 * Возвращает ужё facing-correct sprite id — render не флипает. */
static u16 pick_sprite_id(u8 input, u16 frame)
{
    /* Death-animation: 4-кадровый цикл K_DIE_1..4, stride 8 ticks/frame
     * — занимает ровно DEATH_LEN тиков (60/8 ≈ 7 переключений между
     * кадрами, остановится на K_DIE_4 к концу). */
    if (s_dying) {
        static const u16 die_cycle[4] = { K_DIE_1, K_DIE_2, K_DIE_3, K_DIE_4 };
        return die_cycle[(s_dying_frame >> 3) & 3];
    }
    /* Level-end: Karnov сидит со свитком — front-facing CROUCH-поза
     * #0x67(R)/#0x6F(L) (руки скрещены на груди). Это правильная «сидячая»
     * поза (0x60-0x62 были dissolve, 0x7B — спина на лестнице). Сам свиток
     * #0x90 рисуется поверх рук отдельно в physics_render. */
    if (s_sit_scroll) {
        return (s_facing < 0) ? K_CROUCH_L : K_CROUCH_R;
    }
    bool wants_u   = (input & INP_U) != 0;
    bool wants_d   = (input & INP_D) != 0;
    bool walking   = (input & (INP_R | INP_L)) != 0;
    bool in_air    = !s_on_ground && !s_on_ladder && !s_flying;
    /* «Прыгнул сам» (s_in_jump=true) → анимация прыжка ВСЁ время полёта,
     * включая спуск. «Свалился без прыжка» (in_air без s_in_jump) →
     * fall sprite. Это match'ит оригинал KARNOV.EXE поведение, где jump-
     * arc — single sprite, а fall — отдельный графический «беспомощный»
     * pose. */
    bool jumping   = in_air && s_in_jump;
    bool falling   = in_air && !s_in_jump;
    bool crouching = wants_d && !s_on_ladder && !s_flying;
    bool firing    = (input & INP_FIRE) != 0;
    bool face_left = s_facing < 0;

    /* 1. Flight (крылья) — наивысший приоритет. 2-кадровый цикл на сторону,
     * каждый кадр держится 4 game-ticks (≈145 ms — спокойный взмах). */
    if (s_flying) {
        static const u16 cycle_r[2] = { K_FLYR_1, K_FLYR_2 };
        static const u16 cycle_l[2] = { K_FLYL_1, K_FLYL_2 };
        const u16 *cyc = face_left ? cycle_l : cycle_r;
        return cyc[(frame >> 2) & 1];
    }

    /* 2. Лестница. На самом верху и не двигается — спец-кадр #123. */
    if (s_on_ladder) {
        if (s_y <= LADDER_TOP_Y && !wants_u && !wants_d) {
            return K_CLIMB_TOP;
        }
        /* AnimSeg DS:0x1524 = [stride=4, #121, #122, #121, #120].
         * 4-кадровый цикл, продвигается только при U/D-press
         * (s_climb_ticks gate в physics_tick). */
        static const u16 cycle[4] = { K_CLIMB_B, K_CLIMB_C, K_CLIMB_B, K_CLIMB_A };
        return cycle[(s_climb_ticks >> 2) & 3];
    }

    /* 3. В воздухе: jump-sprite если это «свой прыжок» (включая descent),
     * fall-sprite если упал с высоты без прыжка. FIRE-pose НЕ перекрывает
     * jump/fall — user-spec: «не должен менять спрайт на спрайт выстрела
     * если он стреляет ... во время прыжка, падения». */
    if (jumping) return face_left ? K_JUMP_L : K_JUMP_R;
    if (falling) return face_left ? K_FALL_L : K_FALL_R;

    /* 4. Crouch — FIRE-pose также не перекрывает сидячую позу. */
    if (crouching) return face_left ? K_CROUCH_L : K_CROUCH_R;

    /* 5. Fire — поза стрельбы только стоя на земле (не сидя, не в воздухе,
     * не на лестнице — все они уже отфильтрованы выше). */
    if (firing) return face_left ? K_FIRE_L : K_FIRE_R;

    /* 6. Walk — 4-кадровый цикл per facing.
     * Stride (game-ticks per frame) — байт-verified: sub_timer_max=4 ×
     * *state_ptr=2 = 8 ticks/frame ≈ 290 ms at 27.5 FPS. */
    if (walking) {
        static const u16 cycle_r[4] = { K_WALKR_1, K_WALKR_2, K_WALKR_3, K_WALKR_4 };
        static const u16 cycle_l[4] = { K_WALKL_1, K_WALKL_2, K_WALKL_3, K_WALKL_4 };
        const u16 *cyc = face_left ? cycle_l : cycle_r;
        return cyc[(frame >> 3) & 3];
    }

    /* 7. Idle (stand). */
    return face_left ? K_STAND_L : K_STAND_R;
}

void physics_render(u8 input, u16 frame, bool mode_cga, int scroll_x, int scroll_y)
{
    bool walking = (input & (INP_R | INP_L)) != 0;

    u16 sid = pick_sprite_id(input, frame);
    const sprite_t *s = sprite_find(sid);
    if (s) {
        /* Anchor — BODY center (precomputed s->body_cx). pick_sprite_id уже
         * вернул facing-correct sprite (отдельные L/R sprites #98 vs #106,
         * #99..102 vs #107..110 и т.д.), так что flip не нужен.
         *
         * 2026-05-29: user-feedback показал что cache хранит спрайты в
         * NATURAL orientation для Karnov (#98 raw уже facing-RIGHT). Ранее
         * предположение о mirror'е оказалось неверным — после применения
         * flip Karnov ходил спиной вперёд. Revert. */
        int x0 = s_x - s->body_cx - scroll_x;
        int y0 = s_y - s->h - scroll_y;
        u8 ck = sprite_is_cga() ? 0xFF : 1;
        const u32 *pal = sprite_is_cga() ? CGA_PAL_1H : EGA_PAL16;
        for (int r = 0; r < s->h; ++r) {
            int yy = y0 + r;
            if (yy < 0 || yy >= WIN_H) continue;
            for (int c = 0; c < s->w; ++c) {
                u8 v = s->pixels[r * s->w + c];
                if (v == 0xFF) continue;
                if (v == ck) continue;
                int xx = x0 + c;
                if (xx < 0 || xx >= WIN_W) continue;
                u32 col = sprite_is_cga() ? pal[v & 3] : pal[v & 15];
                render_framebuffer()[yy * WIN_W + xx] = col;
            }
        }
        /* Level-end: свиток #0x90 в руках сидящего Karnov'а (поверх тела,
         * по центру, на уровне груди/рук). Смещение можно подстроить. */
        if (s_sit_scroll) {
            const sprite_t *sc = sprite_find(0x90);
            if (sc) {
                int sxo = s_x - scroll_x - sc->w / 2;
                int syo = s_y - scroll_y - (int)(s->h * 3 / 5);
                for (int r = 0; r < sc->h; ++r) {
                    int yy = syo + r;
                    if (yy < 0 || yy >= WIN_H) continue;
                    for (int c = 0; c < sc->w; ++c) {
                        u8 v = sc->pixels[r * sc->w + c];
                        if (v == 0xFF || v == ck) continue;
                        int xx = sxo + c;
                        if (xx < 0 || xx >= WIN_W) continue;
                        render_framebuffer()[yy * WIN_W + xx] =
                            sprite_is_cga() ? pal[v & 3] : pal[v & 15];
                    }
                }
            }
        }
        return;
    }

    render_placeholder(s_x - scroll_x, s_y - scroll_y, input, frame, walking);
}
