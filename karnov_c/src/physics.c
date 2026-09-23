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
#include "audio.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>

/* Константы физики (Q4 fixed-point для vy).
 *
 * ОТКАТ (2026-09-14): предыдущий проход (аудит скоростей) заменил эту модель
 * на «трапецию» — защищённый подъём constant vy=-5.0 ровно на 16px, затем
 * мгновенное переключение тайл-чеком. Основание («file 0x118CE — прыжок
 * Карнова, ai_type 2») оказалось ОШИБОЧНЫМ: этот адрес (cs:0xFFA:0x00FF) —
 * код ВЕРХНЕГО снаряда трёхстрельного выстрела САМОГО Карнова (SHOT_RISING,
 * см. `ai.c`/`KARNOV.md` §5.4.1 — там же самое совпадение констант vy=-5.0/
 * 16px уже правильно задокументировано для снаряда). Это другой сегмент
 * (0xFFA, generic-модуль), не сегмент Карнова (0xE62, ai_type02,
 * file 0x111E6). Повторный обход всего модуля Карнова (INIT/TICK/
 * UPDATE_DIR_FLAGS/DESTROY, ~40 записей записи в vy-поля) не нашёл НИ ОДНОЙ
 * отрицательной vy-константы и ни одной jump-триггер ветки — настоящий
 * алгоритм прыжка/гравитации Карнова пока НЕ НАЙДЕН (возможно, общий для
 * всех объектов интегратор вне ai_type02, вызываемый из главного цикла, а
 * не из объектного обработчика — требует отдельного расследования).
 *
 * Пользователь подтвердил на практике: «трапеция» даёт прыжок намного ниже,
 * чем должен быть, играть невозможно. Восстановлена СТАРАЯ модель (плавное
 * ускорение JUMP_VY_Q4→GRAVITY_Q4→VY_MAX), которая тоже не байт-подтверждена
 * байт-в-байт, но была играбельна и не содержит доказанной ошибки — дающая
 * подъём ≈40px (4 тайла), разумная высота для платформера. `JUMP_VY_Q4=-80`
 * (эквивалент -5.0 px/frame старта прыжка) сам по себе остаётся правдоподобным
 * (совпадает по модулю с байт-точной SHOT_RISING-константой в соседнем коде
 * того же файла EXE — возможно, не случайно, оба тянутся из одной таблицы
 * констант; но как ИМЕННО эта величина используется в прыжке Карнова —
 * не подтверждено). cs:0xD4B:0x3A5 — НЕ физика (проверено), это хелпер
 * очистки флага [0xD8AE] по input-биту (6 строк, file 0xF055..0xF06E). */
/* s_ground_y / s_ceiling_y / WORLD_MIN_X / WORLD_MAX_X — мировые ограничители
 * физики Karnov. Дефолты — LEVEL0 splash (узкий 320×168). При переходе на
 * широкий уровень game.c вызывает physics_set_world_bounds() с новыми
 * значениями (например, ground_y=240 и max_x=3590 для LEVEL1). */
static int s_ground_y    = 115;
static int s_ceiling_y   = 75;
static int s_world_min_x = 8;
static int s_world_max_x = 312;
#define JUMP_VY_Q4  (-80)   /* -5.0 px/frame × 16 (Q4); старт прыжка */
#define GRAVITY_Q4    4     /* плавный разгон — порт-модель, не байт-точна,
                              * но играбельна (см. комментарий выше) */
#define VY_MIN      (-96)   /* порт-модель, симметрично GRAVITY_Q4/VY_MAX */
#define VY_MAX        80    /* +5.0 × 16 = 80 — предельная скорость падения */
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

/* Flight (wings) — байт-верифицировано (2026-09): игровой диспетчер
 * Karnov'а (cs:0x17EC... на самом деле cs-модуль игрока, file 0x111E6)
 * проверяет fly_timer прямо в THINK:
 *   cmp word [0xd8b6], 0 ; jng skip     (file 0x111FE→0x112 47..0x11 24c:
 *                                        «833E B6D8 00» / «7E 6D»)
 * пока fly_timer > 0, код НЕ идёт в обычный walk/jump путь, а зовёт общий
 * хелпер задания скорости cs:0xD4B:0x8D (file 0xED3D), который пишет
 * vx/vy/ax/ay объекта из ОДНОЙ И ТОЙ ЖЕ 16-байтной velocity-таблицы
 * DS:0x1348 (file 0x204D8) — той же, что и у ходьбы (WALK_SPEED), но по
 * ДРУГОМУ индексу (движок AND'ит [0xD8AE] raw-ввод с маской [0xCCAC] и
 * получает индекс 0..10, а не 0..3 как при ходьбе). Дамп записей 4/7 (вверх)
 * и 8 (вниз) даёт vy = ±2.0 Q16.16 БЕЗ дальнейшего ×2 lshift (тот
 * применяется только если ОДНОВРЕМЕННО активны boots, отдельная ветка file
 * 0x11262+); записи 1/2 (вправо/влево) дают vx = ±2.0 — тоже без ×2.
 * Т.е. полёт вдвое медленнее ходьбы (WALK_SPEED=4), не «чуть медленнее по
 * вертикали». Диагональные записи 5/6/9/10 дают (±1.5,±1.5) — движок не
 * складывает два полных вектора по осям, а использует отдельную
 * «нормализованную» диагональ; порт этого не переносит (независимые
 * X/Y-дельты как при клавишах со всех 4 направлений), только магнитуда
 * осевого движения исправлена. */
#define FLY_SPEED_X   2
#define FLY_SPEED_Y   2

/* Cooldown между выстрелами в game-ticks. Байт-верифицировано по fire()
 * (cs:0xFFA:0x1888, file 0x13028): в конце функция пишет [DS:0xD8B2] = 3
 * (или 1, если взведён флаг [DS:0xCCA6] — «скорострельность»), т.е.
 * перезарядка = 3 тика. Плюс жёсткий лимит 15 живых снарядов ([0x1424]),
 * который проверяется ДО стрельбы. */
#define FIRE_COOLDOWN_TICKS  3

/* Hitbox лестницы: ширина в которой Karnov цепляется (+/- от ladder_x). */
#define LADDER_HALF_W 10
/* LADDER_TOP_Y — DOSBox-verified (2026-05-25):
 * В оригинале при t=15s ladder rails видны в y_range game=45..114 (= 69 px tall).
 * Прошлый 78 был неверен — ладдер должен раскладываться значительно выше
 * (от ground y=114 до top y=45, проходя сквозь область KARNOV-текста).
 *
 * LADDER_HEIGHT — 70 px (тот же диапазон 45..115 при ground=115). Для
 * LEVEL1 при ground_y=240 верх лестницы будет y=170.
 *
 * Низ лестницы — ПОВЕРХНОСТЬ, НА КОТОРОЙ СТОИТ KARNOV в момент установки
 * (s_ladder_bot_y, снимается через find_floor_y), а НЕ глобальный
 * s_ground_y: на уступе/столбе лестница должна вырастать от уступа, иначе
 * она «проваливалась» к самому нижнему полу уровня. */
#define LADDER_HEIGHT 70
#define LADDER_TOP_Y  (s_ladder_bot_y - LADDER_HEIGHT)
#define LADDER_BOT_Y  s_ladder_bot_y

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
static int  s_ladder_bot_y        = 0;   /* поверхность, на которой стоит лестница */
static u16  s_ladder_placed_frame = 0;
static bool s_on_ladder           = false;
static bool s_prev_opt            = false;   /* для edge-detection B-нажатия */
/* Переключение выбранного предмета OPTIONS движением — по ФРОНТУ нажатия
 * вправо/влево (одно нажатие = один шаг по панели). Удержание клавиши
 * предметы больше не листает: прежний вариант «каждые 16 px пути» при
 * ходьбе прокручивал инвентарь без остановки. */
static bool s_prev_r              = false;
static bool s_prev_l              = false;
static u16  s_cur_frame           = 0;
/* Спрайт текущей позы (обновляется в конце physics_tick). */
static u16  s_render_sid          = 106;   /* K_STAND_R */
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
/* Edge-detect для FIRE (найдено декомпиляцией 2026-09-24, DOCONTROLS
 * cs:0x20F:0x2FB, file 0x3BEB + диспетчер клавиш cs:0x20F:0xA/0xDA,
 * file 0x38FA/0x39CA): в оригинале [DS:0xCCAA] — не «клавиша сейчас
 * зажата», а ИМПУЛЬС длиной в один тик на каждое буферизованное
 * BIOS-нажатие (INT 16h-подобная очередь клавиатуры, функция на file
 * 0x3904 — peek+get, ax=0 если буфер пуст). Диспетчер (file 0x39CA)
 * устанавливает бит 0x10 только когда в этом тике реально извлечена
 * клавиша SPACE/X; на любом другом тике (буфер пуст → default-ветка
 * file 0x3A20) бит СБРАСЫВАЕТСЯ. Т.е. при удержании клавиши повторные
 * «нажатия» появляются только с частотой аппаратного автоповтора
 * клавиатуры (typematic), а не каждый тик — visually «нужно нажимать
 * заново». Порт не эмулирует BIOS-автоповтор (он недетерминирован и
 * зависит от железа), поэтому вместо этого выстрел разрешён только по
 * переднему фронту wants_fire — как и JUMP (s_prev_u) и переключение
 * OPTIONS (s_prev_r/s_prev_l). */
static bool s_prev_fire            = false;
/* Кэш «летает ли Karnov» — synced с inv_flying() в начале каждого тика,
 * чтобы post-OPT-handling корректно выбирать ветку физики. */
static bool s_flying              = false;

/* Spawn-точка уровня — кэшируется physics_reset, используется для respawn
 * после death-анимации (контакт с водой). */
static int  s_spawn_x             = 137;
static int  s_spawn_y             = 108;

/* ─── Гибель Карнова — порт cs:0xE62:0x1791 (file 0x115B1) ────────────
 *
 * Вход в смерть — две точки в модуле игрока (сегмент 0xE62):
 *   die_water (cs:0xE62:0x06C6, file 0x104E6) — тайл категории 8 (вода) в
 *       tile-loop игрока; делает set_player_anim(7, 4), y_world += 4 и
 *       **play_sound(7)** (единственный play_sound во всём модуле игрока,
 *       file 0x10515). Зовётся из трёх мест: file 0x10C10 / 0x10DB1 /
 *       0x1114C — по одному на каждый режим движения.
 *   die_hit (cs:0xE62:0x06FE) — state_byte 4, поставленный pair-loop'ом;
 *       set_player_anim(6, 4). Звук уже сыгран самим pair-loop:
 *       **play_sound(2)** в ветке «игрок ПОГИБ» (file 0x0FBC2) — при
 *       НЕсмертельном касании игроку не играется ничего.
 *   Обе через set_player_anim (0x683) ставят обработчик 0xE62:0x1791,
 *   player_state ([0xD8CE]) = 7 (вода) либо 6 (враг) и firepower = 1.
 *
 * Обработчик 0x1791:
 *   INIT (0x17B7): tick_period = 8, state_list = DS:0x1682, num_bboxes = 1,
 *       box0.hitflag = 0 (виден, но не сталкивается), vx = 0, ax = 0,
 *       ay += 0x8000 (0.5 px/тик²; если тайл под ногами категории 9 —
 *       только 0x2000 = 0.125), +0xE9 = 1 («ещё летит»), аним-состояние 0
 *       и play_music(DS:0x09D9, prio 0x64, loop 0) = трек 5 «смерть Karnov».
 *   THINK (0x184C): пока летит — если y_world ушёл ниже низа экрана + 100,
 *       объект прячется (num_bboxes = 0) и переходит в аним-состояние 1;
 *       после приземления, когда 3-кадровая анимация 1 отработала круг
 *       (frame_idx вернулся в 0), — аним-состояние 2. Респавн
 *       (cs:0x4E4:0x18D — он же гасит музыку и декрементит жизни DS:0xCF4E)
 *       вызывается, только когда музыка доиграла (cs:0xCB9:0x37 —
 *       «[0x1212]|[0x1214] == 0») И аним-состояние равно 2.
 *   state 3 (0x18BE) — коллизия с тайлом вида 4 («уперся снизу»):
 *       y_world = кромка тайла − высота ТЕКУЩЕГО спрайта (таблица высот
 *       DS:0x0FAC), vy = 0, ay = 0, «летит» = 0, аним-состояние 1.
 *   state 4 (0x1972) — повторный урон игнорируется (state_byte := 2).
 *
 * Кадры (state_list DS:0x1682 → AnimScript + FrameOffset, dx в 2px-единицах):
 *   0: #114                 (0,0)
 *   1: #115, #116, #117     (−4,3) (−6,12) (−6,18)
 *   2: #117                 (−6,18)
 * Высоты из DS:0x0FAC — байт-верифицировано заново (2026-09):
 *   file 0x1F190+0xFAC+sprite_id — таблица ИНДЕКСИРУЕТСЯ РЕАЛЬНЫМ id
 *   спрайта (не позицией 0..3 в анимации). Само чтение —
 *   cs:0xE62:0x18BE (file 0x116DE): «shl bx,1; mov ax,[bx+0xd8e6];
 *   les bx,[0xd8a6]; mov bx,[es:bx+0x20] (bbox0.sprite); mov dl,[bx+0xfac]
 *   (BYTE, без сдвига!); sub ax,dx; …; mov [es:bx+0xc],ax» — т.е.
 *   y_world = кромка_тайла − table[sprite_id]. Дамп по sprite_id:
 *     #114→24 (file 0x201AE), #115→24 (0x201AF),
 *     #116→15 (0x201B0), #117→11 (0x201B1).
 *   В кэше порта КАНВА этих спрайтов — 25/24/15/14 px (без пустых строк
 *   сверху/снизу — расхождение НЕ паддинг, а сама таблица уже отличается
 *   от полной высоты арта). Но try_land вызывается только пока
 *   state_byte==0 (see death_try_land ниже), а в состоянии 0 текущий
 *   спрайт всегда #114 — так что практически используется только
 *   табличная высота 24 (не canvas-высота 25 из кэша). Записано байт-в-
 *   байт через death_sprite_ground_h(), а не через sprite_find()->h. */
#define DEATH_TICK_PERIOD  8            /* +0xC9 из INIT */
#define DEATH_AY_Q         0x8000       /* 0.5 px/тик² */
#define DEATH_AY_Q_CAT9    0x2000       /* 0.125 — если под ногами категория 9 */
/* Маска нижних углов хитбокса на время смерти: state-структура DS:0x169A,
 * поле +0x04 = 0x0004 (проходит только «небо», всё остальное — опора). */
#define DEATH_LOWER_MASK   0x0004
/* Страховка порта: если музыка смерти почему-то не сигналит о завершении,
 * всё равно возродиться. В оригинале ограничения нет. */
#define DEATH_MAX_TICKS    400

static bool s_dying               = false;
static int  s_death_state         = 0;      /* аним-состояние 0..2 */
static int  s_death_frame         = 0;      /* индекс кадра внутри состояния */
static int  s_death_tick          = 0;      /* троттлинг tick_period */
static bool s_death_falling       = false;  /* +0xE9 */
static bool s_death_hidden        = false;  /* num_bboxes = 0 */
static int  s_death_x             = 0;      /* якорь: ЛЕВЫЙ край (px) */
static s32  s_death_y_q           = 0;      /* якорь: ВЕРХ спрайта, Q16.16 */
static s32  s_death_vy_q          = 0;
static s32  s_death_ay_q          = 0;
static u16  s_death_ticks         = 0;
static bool s_death_by_water      = false;  /* player_state 7 vs 6 */
static bool s_just_died_pulse     = false;
/* Камера (мировой Y верхнего края вьюпорта) — нужна для порога «скрыть
 * тело за экраном» в death_tick (байт-верификация — см. комментарий там).
 * Обновляется game.c каждый кадр через physics_set_scroll_y(); при смерти
 * physics_get_y()/get_x() замораживаются на месте гибели, поэтому камера
 * (следующая за update_scroll(physics_get_x/y())) тоже фактически
 * замирает — как и в оригинале, где смерть подменяет обработчик игрока и
 * курсор камеры перестаёт двигаться за телом. */
static int  s_scroll_y            = 0;
/* Неуязвимость: включается на время victory-секвенции (boss убит, Karnov
 * авто-идёт к свитку), чтобы вода/опасности не убили его во время прохода. */
static bool s_invuln              = false;
/* HP Карнова. Оригинал при (ре)спавне пишет player.hit_points = 5
 * (file 0x101CE, рядом со сбросом таймеров ботинок/крыльев/очков и
 * счётчика снарядов). Урон считает pair-loop (file 0xF81E): при касании
 * ОБЕ стороны вычитают HP друг друга с клампом в ноль, и если у игрока
 * стало 0 — он гибнет. У врагов HP от 10, так что касание практически
 * всегда смертельно; «синий Карнов» из мануала кодом не подтверждается.
 * s_hit_iframes — защита порта от повторного срабатывания на соседних
 * кадрах (в оригинале эту роль играет state 4 + сброс hit-флага). */
static int  s_hp                  = 5;
static int  s_hit_iframes         = 0;
/* Level-end: Karnov сидит со свитком (front-facing поза #0x60-0x62), а не
 * обычный crouch. Включается на время s_level_end. */
static bool s_sit_scroll          = false;

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
/* Слоты поз Karnov'а. Спрайты 98..113 — две зеркальные группы по 8;
 * после переизвлечения кэша БЕЗ переворота (арт лежит как в архиве)
 * группы поменялись ролями: 98..105 смотрят ВЛЕВО, 106..113 — ВПРАВО.
 * В архиве это подтверждается парами 98↔106, 99↔107, … (75 таких пар;
 * flip'а в блиттере оригинала нет — разворот = выбор другого id). */
enum {
    /* RIGHT-facing */
    K_STAND_R = 106,
    K_WALKR_1 = 107, K_WALKR_2 = 108, K_WALKR_3 = 109, K_WALKR_4 = 110,
    K_CROUCH_R = 111,
    K_JUMP_R = 112,
    K_FIRE_R = 113,
    /* LEFT-facing */
    K_STAND_L = 98,
    K_WALKL_1 = 99,  K_WALKL_2 = 100, K_WALKL_3 = 101, K_WALKL_4 = 102,
    K_CROUCH_L = 103,
    K_JUMP_L = 104,
    K_FIRE_L = 105,
    /* DEATH — 4-кадровая анимация утопления/гибели. */
    K_DIE_1 = 114, K_DIE_2 = 115, K_DIE_3 = 116, K_DIE_4 = 117,
    /* FALL — отличаются от JUMP'ов: используются при vy_q4 >= 0 (descending) */
    K_FALL_R = 119,
    K_FALL_L = 118,
    /* LADDER cycle + at-top pose */
    K_CLIMB_A = 120, K_CLIMB_B = 121, K_CLIMB_C = 122,
    K_CLIMB_TOP = 123,
    /* FLY (крылья) */
    K_FLYR_1 = 126, K_FLYR_2 = 127,
    K_FLYL_1 = 124, K_FLYL_2 = 125,
};

/* Аним-состояния смерти — байт-порт state_list DS:0x1682 (3 StateScript'а).
 * dx хранится в единицах движка (2 px), dy — в пикселях; спрайт рисуется
 * в (2·x_world + 2·dx, y_world + dy), см. KARNOV.md §3.3. */
typedef struct { u16 sid; s16 dx_units, dy; } death_frame_t;
static const death_frame_t DEATH_FALL[1]    = { {K_DIE_1,  0,  0} };
static const death_frame_t DEATH_COLLAPSE[3]= { {K_DIE_2, -4,  3},
                                                {K_DIE_3, -6, 12},
                                                {K_DIE_4, -6, 18} };
static const death_frame_t DEATH_REST[1]    = { {K_DIE_4, -6, 18} };
static const death_frame_t *const DEATH_ANIM[3] = {
    DEATH_FALL, DEATH_COLLAPSE, DEATH_REST
};
static const int DEATH_ANIM_N[3] = { 1, 3, 1 };

static const death_frame_t *death_cur_frame(void)
{
    int st = (s_death_state >= 0 && s_death_state < 3) ? s_death_state : 0;
    int fi = s_death_frame;
    if (fi < 0 || fi >= DEATH_ANIM_N[st]) fi = 0;
    return &DEATH_ANIM[st][fi];
}

/* DS:0x0FAC[sprite_id] байт-в-байт (см. комментарий выше death_frame_t):
 * только эти 4 значения нужны смерти Карнова. death_try_land() зовётся
 * только пока текущий кадр — #114 (state 0), так что реально «в дело»
 * идёт только 24, но таблица оставлена полной для честности порта. */
static u8 death_sprite_ground_h(u16 sid)
{
    switch (sid) {
        case K_DIE_1: return 24;   /* file 0x201AE */
        case K_DIE_2: return 24;   /* file 0x201AF */
        case K_DIE_3: return 15;   /* file 0x201B0 */
        case K_DIE_4: return 11;   /* file 0x201B1 */
        default:      return 24;
    }
}

/* LEVEL0 splash и LEVEL1 dragon-platform — байт-верифицировано (2026-09),
 * хардкод СНЯТ: прежняя гипотеза «эти тайлы НЕ помечены attr&1» была
 * неверна. Прямой разбор LEVEL0.BIN/LEVEL1.BIN (формат level_load,
 * level.c) показывает:
 *   LEVEL0: row4 (y=40..49), tile_id∈{1,4,7,10,13,16,19,22,26,29,32,35}
 *     (cols 3..14, x=48..239 — верх букв KARNOV) → attr=0 (props=0x0001,
 *     «standable floor»), т.е. ИМЕННО solid по обычному KARNOV_WALK_MASK
 *     тесту. Соседние ряды 0..3 и 5..8 (фон и «тело» букв) — attr=2
 *     (passable), поэтому визуально Karnov не проваливается внутрь букв,
 *     а стоит ровно на их верхней кромке.
 *   LEVEL1: row26 (y=260..269), tile_id∈{5,6,7} cols 0..11 (x=0..191,
 *     каменная плита под драконом) → attr=0 — тоже solid.
 * Обычный generic-скан find_floor_y (шаг 2 ниже) УЖЕ находит эти тайлы
 * без всякого overlay: для LEVEL1 top_y=row26*TILE_H=260 — ТОЧНО
 * совпадает с прежним хардкодом LVL1_PLAT_TOP_Y=260 (нулевая невязка).
 * Для LEVEL0 generic-скан даёт top_y=row4*TILE_H=40 (прежний хардкод
 * y_top=45 был ручной DOSBox-оценкой «на глаз» с точностью до полтайла —
 * байт-точное значение из данных уровня приоритетнее). Оба
 * s_level0_splash/s_level1_dragon_platform overlay и LVL1_PLAT_* сняты
 * как избыточные костыли порта. */

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
 * LEVEL0 «буквы KARNOV» и LEVEL1 dragon-platform больше НЕ нуждаются в
 * overlay-костыле (см. комментарий над этой функцией) — generic-скан ниже
 * сам находит их через level_solid_at (attr=0 в данных уровня). */
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

/* Обновить точку респауна БЕЗ полного сброса состояния. Используется
 * checkpoint-логикой смерти (KARNOV.EXE file 0x10019): точка возрождения
 * берётся из ObjectRegion по позиции гибели, а не из старта уровня. */
void physics_set_spawn_point(int x, int y)
{
    s_spawn_x = x;  s_spawn_y = y;
}

/* Камера — см. s_scroll_y выше. Зовётся game.c каждый кадр (update_scroll
 * пишет g_scroll_x/g_scroll_y). */
void physics_set_scroll_y(int scroll_y) { s_scroll_y = scroll_y; }

void physics_reset(int x, int y)
{
    s_x = x; s_y = y; s_vy_q4 = 0; s_facing = 1; s_on_ground = (y >= s_ground_y);
    s_spawn_x = x;  s_spawn_y = y;          /* для respawn после death */
    s_ladder_placed = false;
    s_ladder_x = 0;
    s_ladder_bot_y = s_ground_y;
    s_ladder_placed_frame = 0;
    s_on_ladder = false;
    s_prev_opt = false;
    s_prev_r = s_prev_l = false;
    s_cur_frame = 0;
    s_climb_ticks = 0;
    s_climbing_down_prev = false;
    s_fire_cooldown = 0;
    s_flying = false;
    s_prev_u = false;
    s_prev_fire = false;
    s_in_jump = false;
    s_dying = false;
    s_death_state = 0;
    s_death_frame = 0;
    s_death_tick = 0;
    s_death_falling = false;
    s_death_hidden = false;
    s_death_vy_q = 0;
    s_death_ay_q = 0;
    s_death_ticks = 0;
    s_death_by_water = false;
    s_just_died_pulse = false;
    s_invuln = false;
    s_hp = 5;                 /* file 0x101CE: hit_points = 5 при респавне */
    s_hit_iframes = 0;
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

/* Кадр Карнова нужен раньше своего определения: коллизия берёт размеры
 * ТЕКУЩЕГО спрайта, как оригинал (sprite_w/sprite_h по bbox[0].sprite_id). */
static u16 pick_sprite_id(u8 input, u16 frame);

/* ─── Гибель: запуск и тик ────────────────────────────────────────────
 * by_water = true — путь die_water (player_state 7): +4 к y_world и
 * play_sound(7). Иначе die_hit (player_state 6): звук уже сыграл
 * pair-loop (play_sound(2) в physics_hit). */
static void death_begin(bool by_water)
{
    /* Якорь берём в конвенции движка — левый край / верх ТЕКУЩЕГО кадра,
     * то есть ровно x_world/y_world объекта игрока. */
    s_death_x   = physics_get_left_x();
    int top     = physics_get_top_y();
    if (by_water) top += 4;              /* die_water: `add [es:bx+0xc],4` */
    s_death_y_q = (s32)top << 16;

    s_dying           = true;
    s_just_died_pulse = true;
    s_on_ladder       = false;
    s_flying          = false;
    s_in_jump         = false;
    s_vy_q4           = 0;

    s_death_state   = 0;                 /* аним-состояние 0 = #114 */
    s_death_frame   = 0;
    s_death_tick    = DEATH_TICK_PERIOD;
    s_death_falling = true;              /* +0xE9 = 1 */
    s_death_hidden  = false;
    s_death_vy_q    = 0;
    /* ay += 0x8000, но над тайлом категории 9 — только 0x2000. */
    s_death_ay_q    = (level_tile_attr_at(s_x, s_y) == 9)
                        ? DEATH_AY_Q_CAT9 : DEATH_AY_Q;
    s_death_ticks   = 0;
    s_death_by_water = by_water;

    if (by_water) audio_play_sfx(7);     /* file 0x10515 */
    /* INIT обработчика (0x1838): play_music(DS:0x09D9, prio 0x64, loop 0) —
     * трек 5 «смерть Karnov». Приоритет в порту не моделируется. Музыку
     * стартует САМА смерть, а не игровой цикл, — иначе она не звучит там,
     * где цикл другой (headless-прогон). */
    audio_play_music(5, /*loop=*/false);
}

/* Приземление — порт state 3 (0x18BE), вид коллизии 4 «уперся снизу».
 * Проверяется каждый кадр (фаза коллизий, а не throttled THINK). */
static bool death_try_land(void)
{
    u16 sid = death_cur_frame()->sid;
    const sprite_t *s = sprite_find(sid);
    int h = (int)death_sprite_ground_h(sid);   /* DS:0x0FAC[sid], НЕ canvas-высота кэша */
    int w = s ? (int)s->art_w : 16;
    int top    = (int)(s_death_y_q >> 16);
    int bottom = top + h;
    int x1 = s_death_x + (w > 0 ? w - 1 : 0);
    if (!level_tile_blocks(s_death_x, bottom, DEATH_LOWER_MASK) &&
        !level_tile_blocks(x1,        bottom, DEATH_LOWER_MASK))
        return false;
    /* y_world = кромка тайла − высота текущего спрайта. */
    s_death_y_q     = (s32)((bottom / TILE_H) * TILE_H - h) << 16;
    s_death_vy_q    = 0;
    s_death_ay_q    = 0;
    s_death_falling = false;
    s_death_state   = 1;                 /* [0xD88C] = 1 */
    s_death_frame   = 0;
    s_death_tick    = DEATH_TICK_PERIOD;
    return true;
}

static void death_tick(void)
{
    if (s_death_ticks < 0xFFFF) ++s_death_ticks;

    /* Фаза 1 (UPDATELOCS) — скорость интегрируется КАЖДЫЙ кадр. */
    if (s_death_falling) {
        s_death_vy_q += s_death_ay_q;
        s_death_y_q  += s_death_vy_q;
        death_try_land();
    }

    /* THINK — раз в tick_period кадров; ai_dispatch сперва прокручивает
     * кадр анимации, потом зовёт обработчик. */
    if (--s_death_tick <= 0) {
        s_death_tick = DEATH_TICK_PERIOD;
        if (++s_death_frame >= DEATH_ANIM_N[s_death_state]) s_death_frame = 0;
        if (s_death_falling) {
            /* Ушёл ниже мира — байт-верифицировано (2026-09):
             * cs:0xE62:0x185E (file 0x1167E) THINK:
             *   cmp word[es:bx+0xe9],0 ; je skip      («ещё летит»?)
             *   mov ax,[es:bx+0xc]                    ; ax = y_world
             *   mov dx,[0xcc94] ; add dx,0x64 ; cmp ax,dx ; jle skip
             *   mov word[es:bx+0x1e],0 ; …             ; hide (num_bboxes=0)
             * [0xCC94] — НЕ level-константа, а ПЕР-КАДРОВЫЙ «низ вьюпорта
             * в мировых координатах» = view_y + 180 (0xB4, file 0x36FC:
             * «mov word[0xb832],0xb4»), где view_y — камера (file 0xFF8C:
             * «[bp-4] = raw − [0xd955] − [0xb82e]; [0xcc90]=view_y;
             * [0xcc94]=view_y+[0xb832]»). Итого порог = view_y + 280
             * (180 высота вьюпорта + 100 запас), а НЕ низ карты уровня —
             * эквивалент в порту: s_scroll_y (камера, см. physics_set_
             * scroll_y) + 280. */
            int floor_px = s_scroll_y + 180 + 100;
            if ((int)(s_death_y_q >> 16) > floor_px) {
                s_death_hidden  = true;   /* num_bboxes = 0 */
                s_death_falling = false;
                s_death_state   = 1;
                s_death_frame   = 0;
            }
        } else if (s_death_state == 1 && s_death_frame == 0) {
            s_death_state = 2;           /* круг анимации 1 отработан */
            s_death_frame = 0;
        }
    }

    /* KARNOV_DBG_DEATH=1 — трассировка анимации гибели. */
    if (getenv("KARNOV_DBG_DEATH")) {
        const death_frame_t *df = death_cur_frame();
        fprintf(stderr, "f=%u death[%s] t=%u anim=%d frame=%d sid=%u top=%d vy=%.3f "
                        "fall=%d hidden=%d music_done=%d x=%d\n",
                s_cur_frame, s_death_by_water ? "water/sfx7" : "enemy/sfx2",
                s_death_ticks, s_death_state, s_death_frame, df->sid,
                (int)(s_death_y_q >> 16), s_death_vy_q / 65536.0,
                (int)s_death_falling, (int)s_death_hidden,
                (int)audio_music_finished(), s_death_x);
    }

    /* Респавн: аним-состояние 2 И трек смерти доиграл. */
    if ((s_death_state == 2 && audio_music_finished()) ||
        s_death_ticks >= DEATH_MAX_TICKS) {
        physics_reset(s_spawn_x, s_spawn_y);
    }
}

void physics_tick(u8 input)
{
    /* Сбрасываем «just_died»-импульс предыдущего кадра. game.c прочитает
     * его в той же тике, в которой был установлен. */
    s_just_died_pulse = false;

    /* Анимация гибели тикает без обработки ввода (в оригинале обработчик
     * игрока подменён на 0xE62:0x1791 и ввод в нём не читается). */
    if (s_dying) {
        death_tick();
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
    /* KARNOV_DBG_GIVE=<id>[,<id>...] — выдать предметы на старте (тестовый
     * хук: иначе эффекты предметов в headless-прогоне не проверить). */
    { static bool given = false;
      const char *dg = getenv("KARNOV_DBG_GIVE");
      if (dg && !given) {
          given = true;
          for (const char *q = dg; *q; ) {
              inv_give(atoi(q));
              while (*q && *q != ',') ++q;
              if (*q == ',') ++q;
          }
      } }

    /* KARNOV_DBG_LADDER=1 — поставить лестницу без предмета (тестовый
     * хук: иначе анимацию лазанья не проверить в headless-прогоне). */
    { const char *dl = getenv("KARNOV_DBG_LADDER");
      if (dl && !s_ladder_placed && s_on_ground) {
          s_ladder_placed = true; s_ladder_x = s_x;
          s_ladder_bot_y = find_floor_y(s_x, s_y); s_ladder_placed_frame = s_cur_frame; } }

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
                    /* Основание — та поверхность, на которой Karnov стоит
                     * сейчас (уступ/столб/земля), а не пол уровня. */
                    s_ladder_bot_y        = find_floor_y(s_x, s_y);
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
        /* User-spec (2026-09-13): управление в воздухе работает ТОЛЬКО если
         * Karnov прыгнул (s_in_jump) — сорвавшись с края платформы без
         * прыжка (walk-off-edge ниже, а также шаг в сторону с лестницы,
         * который тоже роняет s_on_ground без прыжка), он должен падать
         * строго вертикально, без бокового контроля. s_in_jump уже
         * существовал и байт-верифицированно взводится только реальным
         * прыжком (edge-triggered ниже) — раньше использовался только для
         * выбора спрайта (K_JUMP/K_FALL), теперь эта же граница действует и
         * на управление. Настоящий алгоритм оригинала (cs:0xD4B:0x3A5
         * "apply physics") не декомпилирован — это поведенческий фикс по
         * прямой спецификации пользователя, не байт-подтверждённый порт. */
        bool air_control_locked = !s_on_ground && !s_in_jump;
        if (!crouching && !air_control_locked) {
            if (wants_r) { dx_walk += WALK_SPEED; s_facing =  1; }
            if (wants_l) { dx_walk -= WALK_SPEED; s_facing = -1; }
        }
        if (dx_walk != 0) {
            int step = (dx_walk > 0) ? 1 : -1;
            int remain = (dx_walk > 0) ? dx_walk : -dx_walk;
            for (int i = 0; i < remain; ++i) {
                int probe_x = s_x + step;
                /* Коллизия ведущего ребра — как в оригинале
                 * (move_with_collision, file 0xF37B): пробуются РОВНО ДВА
                 * угла хитбокса со СВОИМИ масками, а не три точки одной:
                 *   нижний угол (подошва − 10 px) — маска 0x0006: проходит
                 *     только потолок и небо, значит пол и СТУПЕНИ твёрдые;
                 *   верхний угол (макушка)        — маска 0x0085: проходит
                 *     пол, небо и attr 7, поэтому на ступень/колонну можно
                 *     запрыгнуть сверху (one-way platform).
                 * Прежняя маска 0x001F была изобретением порта и пропускала
                 * Карнова боком сквозь пол, ступени и стены. */
                const sprite_t *ks = sprite_find(pick_sprite_id(input, s_cur_frame));
                int half_w = ks ? (int)ks->art_w / 2 : 8;
                int body_h = ks ? (int)ks->h : 26;
                int lead_x = probe_x + (step > 0 ? +half_w : -half_w);
                bool feet_hit =
                    level_tile_blocks(lead_x, s_y - KARNOV_FEET_PROBE_UP, KARNOV_MASK_FEET);
                /* НИЖНЯЯ СТУПЕНЬКА ПРОХОДИМА (user-req 2026-08-15): если
                 * препятствие у ног высотой ровно в одну строку тайлов —
                 * т.е. строкой выше свободно, — Карнов проходит сквозь неё.
                 * Так он входит в основание лестницы-пирамиды LEVEL1, а не
                 * упирается в невидимую стену за 48 px до неё; ступени в
                 * две строки и выше по-прежнему останавливают. */
                if (feet_hit &&
                    !level_tile_blocks(lead_x, s_y - KARNOV_FEET_PROBE_UP - TILE_H,
                                       KARNOV_MASK_FEET))
                    feet_hit = false;
                bool blocked = feet_hit ||
                    level_tile_blocks(lead_x, s_y - body_h, KARNOV_MASK_HEAD);
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

        /* ОТКАТ (2026-09-14): плавное ускорение вместо «трапеции» — см.
         * большой комментарий у констант выше (JUMP_VY_Q4/GRAVITY_Q4/…) —
         * прошлая byte-точная модель оказалась построена на перепутанном
         * адресе (код чужого объекта, не Карнова) и давала прыжок намного
         * ниже игрового. Эта модель не байт-точна, но играбельна. */
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

    /* 4. Стрельба: выстрел — по ПЕРЕДНЕМУ ФРОНТУ FIRE (в оригинале это
     * импульс от буферизованного нажатия клавиши, см. комментарий у
     * s_prev_fire), удержание клавиши автоогня не даёт. cooldown
     * остаётся вторичным ограничителем на случай частых повторных
     * нажатий (как [0xD8B2] в оригинале). На лестнице стрелять нельзя
     * (как в оригинале — обе руки заняты). В полёте можно. */
    bool fire_edge = wants_fire && !s_prev_fire;
    if (s_fire_cooldown > 0) --s_fire_cooldown;
    if (fire_edge && s_fire_cooldown == 0 && !s_on_ladder) {
        /* Точка вылета — переразобрано заново (2026-09, file 0x12F55..
         * 0x12FA3, см. комментарий в ai_spawn_player_fireball()):
         *   x = player.x_world + 8 px, если Карнов смотрит вправо,
         *       player.x_world + 0 px, если влево — АСИММЕТРИЧНО (одна и
         *       та же velocity-таблица DS:0x1348, что и у WALK_SPEED,
         *       даёт ±2.0, удваивается, плюс флат +4.0; раньше здесь стоял
         *       единый facing-независимый +8, что было неверно для левого
         *       направления);
         *   y = верх спрайта + 7, и ещё +16 если Карнов сидит. */
        int fx = physics_get_left_x() + (s_facing > 0 ? 8 : 0);
        int fy = physics_get_top_y() + 7;
        if (wants_d && s_on_ground) fy += 16;
        /* Cooldown взводится, только если выстрел реально состоялся —
         * при лимите в 15 живых снарядов оригинал (fire(), file 0x1303B)
         * НЕ трогает [0xD8B2], позволяя мгновенный повтор попытки. */
        if (ai_spawn_player_fireball(fx, fy, s_facing, s_cur_frame)) {
            /* С читом перезарядка 1 тик вместо 3 (file 0x1302C). */
            s_fire_cooldown = input_god_mode() ? 1 : FIRE_COOLDOWN_TICKS;
        }
    }
    s_prev_fire = wants_fire;

    if (s_x < s_world_min_x) s_x = s_world_min_x;
    if (s_x > s_world_max_x) s_x = s_world_max_x;
    s_prev_u = wants_u;

    /* Переключение OPTIONS: по фронту нажатия R/L (не по удержанию). */
    if (wants_r && !s_prev_r) inv_cycle(+1);
    if (wants_l && !s_prev_l) inv_cycle(-1);
    s_prev_r = wants_r;
    s_prev_l = wants_l;

    /* Water contact = death. Проверяем тайл прямо под ногами Karnov-а
     * (s_y — base/feet); если это вода (attr=08), стартуем dying-state.
     * В полёте контакт с водой тоже убивает — это совпадает с поведением
     * оригинала. */
    if (s_hit_iframes > 0) --s_hit_iframes;

    if (!s_invuln && !s_dying && level_is_water_at(s_x, s_y))
        death_begin(/*by_water=*/true);   /* die_water → play_sound(7) */

    /* Поза этого кадра — из неё берутся ширина/высота для перевода
     * (центр, подошва) → (левый край, верх), см. physics_get_top_y. */
    s_render_sid = pick_sprite_id(input, s_cur_frame);
}

/* Урон Карнову от касания (pair-loop, file 0xF81E):
 *     player.hp -= enemy_hp;  если < 0 → 0
 *     enemy.hp  -= player.hp; (вычитание симметрично, делает вызывающий)
 *     погиб, если player.hp == 0
 * Пока идут i-frames, включена неуязвимость (victory-секвенция, чит
 * ZEEK) или уже играет анимация гибели — удар игнорируется. */
void physics_hit(int enemy_hp)
{
    /* Чит ZEEK: pair-loop в оригинале гасит удар при взведённом
     * [0xCCA6] (file 0xFB8A). */
    if (input_god_mode()) return;
    if (s_invuln || s_dying || s_hit_iframes > 0) return;
    s_hit_iframes = 20;
    s_hp -= enemy_hp;
    /* Ранен, но жив: в оригинале в этой ветке игроку НЕ играется ничего.
     * Хит-тест (file 0xF81E) вычитает hit_points врага из hit_points
     * игрока и возвращает 1, только если итог == 0 (смертельно). Если
     * ax==0 (игрок жив), pair-loop прыгает на file 0xFB91 — просто снимает
     * флаг касания [+0xDD], без звука. play_sound(2) стоит дальше, в ветке
     * «игрок погиб» (file 0x0FBC2), поэтому звук ниже. Практически ветка
     * недостижима: у игрока 5 HP, у врагов ≥10. */
    if (s_hp > 0) return;
    s_hp = 0;
    audio_play_sfx(2);                    /* file 0x0FBC2 — предсмертный крик */
    death_begin(/*by_water=*/false);      /* die_hit (player_state 6) */
}

int physics_hp(void) { return s_hp; }

bool physics_just_died(void) { return s_just_died_pulse; }
bool physics_is_dying(void)  { return s_dying; }
void physics_set_invuln(bool on) { s_invuln = on; }
void physics_set_sit_scroll(bool on) { s_sit_scroll = on; }

int  physics_get_x(void)       { return s_x; }
int  physics_get_y(void)       { return s_y; }

/* Мировые координаты игрока в конвенции движка: оригинал каждый кадр
 * копирует player->x_world(+0x08) и player->y_world(+0x0C) в кэш
 * [DS:0xD8BC]/[DS:0xD8BE] (file 0x10312), и ИМЕННО с ними сравниваются
 * координаты врагов в TICK'ах и в check_triggers. У объекта x_world — ЛЕВЫЙ
 * край, y_world — ВЕРХ спрайта, тогда как порт держит s_x = центр тела,
 * s_y = подошва. Без пересчёта всякая логика «догнать игрока по Y» промахи-
 * вается вниз на высоту Карнова (~26 px) — из-за этого «комарик» (type 16)
 * опускался ниже ног вместо груди. */
static int karnov_pose_h(void)
{
    const sprite_t *ks = sprite_find(s_render_sid);
    return ks ? (int)ks->h : 26;
}
int physics_get_top_y(void)
{
    /* На время гибели объект живёт в своей (движковой) системе координат:
     * якорь = верх спрайта, его и отдаём. */
    if (s_dying) return (int)(s_death_y_q >> 16);
    return s_y - karnov_pose_h();
}
int physics_get_left_x(void)
{
    if (s_dying) return s_death_x;
    const sprite_t *ks = sprite_find(s_render_sid);
    int w = ks ? (int)ks->art_w : 16;
    return s_x - w / 2;
}
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
    /* Гибель: кадр берётся из state_list DS:0x1682 (см. death_tick). */
    if (s_dying) return death_cur_frame()->sid;
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
        /* Аним-таблица оригинала DS:0x153E → DS:0x1524: 4 кадра
         * #121, #122, #121, #120. Кадр продвигается раз в tick_period
         * игровых тиков (у Карнова = 2) и только когда он реально
         * перебирает руками (s_climb_ticks тикает лишь при U/D). Раньше
         * стояло >> 2 — кадр менялся вдвое реже, и подъём выглядел
         * «скольжением» без анимации. */
        static const u16 cycle[4] = { K_CLIMB_B, K_CLIMB_C, K_CLIMB_B, K_CLIMB_A };
        return cycle[(s_climb_ticks >> 1) & 3];
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

    /* 6. Walk — ПЯТИкадровый цикл, как в аним-таблице оригинала
     * (DS:0x1500: state 0 = 98,99,100,101,102; state 1 = 106..110), т.е.
     * стоячий кадр входит в цикл шага. Темп: кадр раз в tick_period
     * игровых тиков; у обработчиков Карнова tick_period = 2 (значения
     * +0xC9 в cs:0xE62: 2/4/8), поэтому frame >> 1. */
    if (walking) {
        static const u16 cycle_r[5] = { K_STAND_R, K_WALKR_1, K_WALKR_2, K_WALKR_3, K_WALKR_4 };
        static const u16 cycle_l[5] = { K_STAND_L, K_WALKL_1, K_WALKL_2, K_WALKL_3, K_WALKL_4 };
        const u16 *cyc = face_left ? cycle_l : cycle_r;
        return cyc[(frame >> 1) % 5];
    }

    /* 7. Idle (stand). */
    return face_left ? K_STAND_L : K_STAND_R;
}

void physics_render(u8 input, u16 frame, bool mode_cga, int scroll_x, int scroll_y)
{
    bool walking = (input & (INP_R | INP_L)) != 0;

    u16 sid = pick_sprite_id(input, frame);
    /* KARNOV_DBG_POSE=1 — печатать выбранный кадр Карнова (диагностика
     * анимаций: какой sid, состояние лестницы, счётчик лазанья). */
    if (getenv("KARNOV_DBG_POSE"))
        fprintf(stderr, "f=%u sid=%u x=%d y=%d on_ladder=%d climb=%u\n",
                frame, sid, s_x, s_y, (int)s_on_ladder, s_climb_ticks);
    /* Упал ниже мира — num_bboxes = 0, объект не рисуется (0x1867). */
    if (s_dying && s_death_hidden) return;
    const sprite_t *s = sprite_find(sid);
    if (s) {
        /* pick_sprite_id уже вернул спрайт нужного разворота (в архиве
         * зеркальные пары), поэтому flip не нужен. s_x в физике — ЦЕНТР
         * тела, а спрайт якорится левым краем арта → сдвигаем на art_w/2.
         * (Оригинал хранит в объекте сам левый край; у нас Karnov живёт в
         * своей конвенции ноги/центр, см. KARNOV.md §3.3.) */
        int x0 = s_x - s->art_w / 2 - scroll_x;
        int y0 = s_y - s->h - scroll_y;
        if (s_dying) {
            /* Смерть идёт в движковой конвенции: якорь (левый край, верх)
             * плюс FrameOffset кадра — dx в 2px-единицах, dy в пикселях. */
            const death_frame_t *df = death_cur_frame();
            x0 = s_death_x + df->dx_units * 2 - scroll_x;
            y0 = (int)(s_death_y_q >> 16) + df->dy - scroll_y;
        }
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
