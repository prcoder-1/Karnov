/* ai_scripts.h — таблица AI handler'ов по типу врага и базовые декларации.
 *
 * Архитектура (резюме декомпиляции):
 *
 * 1. Каждый враг имеет тип в поле `EnemySpawn.type` (offset 0, в BIN-файле).
 * 2. При активации (cs:0x4F30..0x5010, функция `spawn_active_object`) тип
 *    используется как индекс в таблице AI-хэндлеров `DS:0x10FE`:
 *
 *        bx = spawn.type * 4
 *        far_ptr = (DS:[bx+0x10FE], DS:[bx+0x1100])    ; off, seg
 *
 *    Этот far-ptr вместе с `spawn[0x10]` (initial sub_timer) и obj far_ptr
 *    передаётся в `init_active_object` (cs:0xD4B:0xAF5).
 *
 * 3. Каждый handler — функция вида:
 *        void handler(void) {
 *            switch (current_state_byte) {       // [DS:0xD8A2]
 *            case 0: case 1:  init();
 *            case 2:          tick();             // обычная анимация
 *            case 3: case 4:  hurt();
 *            case 5: case 6:  dying();
 *            }
 *        }
 *    Точный набор веток зависит от типа.
 *
 * 4. Переход между состояниями делается через `queue_state` (cs:0xD4B:0xB31),
 *    который сохраняет новый handler в +0xBF/+0xC1 и ставит state_byte=1.
 *    На следующем dispatch `ai_dispatch` перезагружает +0xB7/+0xB9 из этих
 *    полей (см. ветку state==1 в src/think.cpp).
 *
 * 5. Поля объекта, используемые AI-скриптами:
 *        +0xC5  sub_timer        — увеличивается в state==2 пока < анимация.frames
 *        +0xC7  state_ext         — == 6 у некоторых триггеров перехода
 *        +0xC9  sub_timer_max     — длительность цикла
 *        +0xCB  collision_cnt     — пер-кадровый счётчик
 *        +0xCD/+0xCF              — far ptr на текущую анимацию (первый WORD = frames)
 *        +0xDB  active_flag       — 1 = жив и тикает
 *        +0xE5  obj_type          — копия EnemySpawn.subtype (НЕ type!)
 *        +0xE7  hit_points        — снимается в pair-loop при попадании
 *        +0xE9  misc_state        — handler-специфичное состояние
 *        +0xED  behavior_phase    — handler-специфичное (часто 0=поиск, 1=атака)
 *        +0xEF  alt_sub_phase     — handler-специфичное
 *
 * 6. Поля движения (используются handler'ами при init):
 *        +0x08  x_world
 *        +0x0A  ??
 *        +0x0C  y_world
 *        +0x0E  vy (для типов с гравитацией)
 *        +0x10  vx (для типов с гор. движением)
 *        +0x1E  num_bboxes
 *        +0x20  bbox[0].sprite_id
 *        +0x22  bbox[0].x_offset
 *        +0x24  bbox[0].y_offset
 *        +0x2E  visible flag?
 */
#ifndef KARNOV_AI_SCRIPTS_H
#define KARNOV_AI_SCRIPTS_H

#include "karnov.h"
#include "think.h"

namespace karnov {

/* ─── Таблица AI handler'ов (DS:0x10FE, 57 типов) ─────────────────────
 *
 * Полное содержимое декомпилировано в ai_scripts.cpp. Здесь —
 * только enum имён, для удобства. */
enum EnemyType : u8 {
    ENEMY_NONE       = 0,    /* NULL — не активный */
    ENEMY_BASIC      = 1,    /* cs:0xE62:0xC49 — базовый враг с анимацией */
    ENEMY_TYPE_02    = 2,    /* cs:0xE62:0x13C6 — присутствует в LEVEL2 */
    ENEMY_TYPE_03    = 3,    /* cs:0xE62:0x14D7 */
    ENEMY_TYPE_04    = 4,    /* cs:0xFFA:0xCF6 */
    ENEMY_TYPE_05    = 5,    /* cs:0xFFA:0xDAA */
    ENEMY_RANDOM_06  = 6,    /* cs:0xFFA:0xEB8 — LEVEL0 общий враг */
    ENEMY_TYPE_07    = 7,    /* cs:0xFFA:0x1032 */
    ENEMY_BOSS_08    = 8,    /* cs:0xFFA:0x139F — LEVEL1+ "boss-like" */
    /* ... (полная таблица из 57 типов — в ai_scripts.cpp) */
    ENEMY_TRACKER_23 = 23,   /* cs:0x1348:0x007 — преследователь, LEVEL3/9 */
    ENEMY_RYU_36     = 36,   /* cs:0x151A:0x61B — финальный босс? LEVEL9 */
    ENEMY_TYPE_MAX   = 57    /* последний валидный тип */
};

/* Структура записи в таблице. */
struct AiTableEntry {
    u16 off;
    u16 seg;
};

/* DS:0x10FE — таблица из 58 записей (типы 0..57). */
extern const AiTableEntry ai_handler_table[58];

/* ─── Spawn / dispatch ───────────────────────────────────────────── */

/* KARNOV.EXE: cs:0x4F30 (приблизительно) — spawn_active_object().
 * Преобразует EnemySpawn в ActiveObject, используя ai_handler_table.
 *
 * Псевдокод (полная декомпиляция из cs:0x4F83..0x5004):
 *   obj.obj_type     = spawn[2]              ; subtype в +0xE5
 *   obj.hit_points   = spawn[0x11]           ; HP в +0xE7
 *   obj.x_world      = spawn[0xC]            ; +0x08
 *   obj.y_world      = spawn[0xE]            ; +0x0C
 *   obj.num_bboxes   = 1                      ; +0x1E
 *   obj.???          = 1                      ; +0x2E
 *   obj.bbox[0].xoff = 0                      ; +0x22
 *   obj.bbox[0].yoff = 0                      ; +0x24
 *
 *   init_arg = spawn[0x10]                    ; затем → +0xC3/+0xC5
 *   far_ptr  = ai_handler_table[ spawn[0] ]   ; AI handler
 *   init_active_object(obj, far_ptr, init_arg)
 *   spawn[0x12] = 1                            ; пометка "уже активирован"
 */
ActiveObject* spawn_active_object(/* EnemySpawn* spawn */);

/* ─── Декомпилированные handler'ы (примеры) ───────────────────────── */

/* Type 1 — базовый враг, файл 0x10A69, cs:0xE62:0xC49.
 * Три state-ветки: init, init-повтор, tick. */
void ai_type01(void);

/* Type 2 — Карнов: НОРМАЛЬНОЕ движение игрока, файл 0x111E6, cs:0xE62:0x13C6.
 * Читает [DS:0xCCAC] (direction_bits игрока), удваивает vx/vy через
 * _lshift_long(cx=1). 4 state-ветки. */
void ai_type02(void);

/* Type 3 — Карнов: альтернативное движение (плавание/полёт/лестница?),
 * файл 0x112F7, cs:0xE62:0x14D7.  Делит vx/vy пополам через _rshift_long
 * (более медленное движение).  4 state-ветки + вложенный диспетчер
 * на 7 sub-веток. */
void ai_type03(void);

/* Type 4 — анимационный переход (для Карнова), файл 0x12496, cs:0xFFA:0xCF6.
 * Вызывается из ai_type01 при завершении анимации (queue_state).
 * Использует тот же helper RNG (cs:0xFFA:0x677), что и type 5 — вычисляет
 * misc_state. Tickaет фрейм-зависимо ([0xCDEA] & 8). */
void ai_type04(void);

/* Type 5 — падающий бонусный объект, файл 0x1254A, cs:0xFFA:0xDAA.
 * vy = 2 (фикс. падение), при коллизии с тайлом останавливается.
 * При HURT (state 4) — drop item по индексу +0xE9. */
void ai_type05(void);

/* Type 6 — рандомизированный спавн, файл 0x12658, cs:0xFFA:0xEB8.
 * Содержит вызов RNG (cs:0xD4B:0x105) и выбор поведения по seed. */
void ai_type06(void);

/* Type 7 — «призрак»/триггер, файл 0x127D2, cs:0xFFA:0x1032.
 * Невидим (num_bboxes=0), сохраняет себя в DS:0xD94F как «активный
 * trigger», отслеживает позицию игрока. Внутренний sub-диспетчер
 * по +0xE9 (4 ветки поведения). */
void ai_type07(void);

/* Type 8 — небесный спавн рядом с игроком, файл 0x12B3F, cs:0xFFA:0x139F.
 * Использует `cs:0x1CE7:0x17` (PRNG) дважды: спавнится в случайной
 * точке (player.x ± 32, player.y - 100 ± 32). LEVEL1+. */
void ai_type08(void);

/* Type 9 — двух-спрайтный враг ("boss-like"), файл 0x14BA4, cs:0x11D6:0x1644.
 * num_bboxes=2, sprite_id 0x91 + 0x92 со смещением 5 по X.
 * Делегирует TICK helper'у на cs:0x11D6:0x4B36. */
void ai_type09(void);

/* Type 10 — горизонтальный «маятник», файл 0x13973, cs:0x11D6:0x0413.
 * Двигается между двумя позициями (target +0x1A/+0x1C), при достижении
 * NEG'ит target (flip направления). Базовая vx=−0x1000. */
void ai_type10(void);

/* Type 11 — range-based трекер, файл 0x13A2A, cs:0x11D6:0x04CA.
 * Считает |dx|=abs(player.x-obj.x), переключает behavior_phase 0↔1
 * при пересечении расстояния 10 пикселей. */
void ai_type11(void);

/* Type 12 — наземный патрульный, файл 0x13E13, cs:0x11D6:0x08B3.
 * Двигается по полу с vx=±3, HP=20. При HURT — эффект через
 * cs:0x41D:0x4D7 с 0x1000 + cs:0xD4B:0x4FF. */
void ai_type12(void);

/* Type 13 — отслеживающий двух-фазный, файл 0x13C08, cs:0x11D6:0x06A8.
 * state 0 — Y-snap (obj.y -= [DS:0x1002]) + spawn FX (ai_result |= 0x10).
 * state 1 — основной INIT: state_list 0x18CC, sub_timer_max=4. */
void ai_type13(void);

/* Type 14 — спавн со спрайтом, файл 0x14368, cs:0x11D6:0x0E08.
 * state 0 — spawn(obj, sprite=0x95=149, 0) через cs:0xD4B:0x19F.
 * state 1 — state_list 0x1958, misc_state=2, сохраняет obj.x в +0xEB. */
void ai_type14(void);

/* Type 15 — простой трекер, файл 0x1402B, cs:0x11D6:0x0ACB.
 * Использует [DS:0x100E] + player_cache_y и [DS:0x1045] для anchor-точки.
 * sub_timer_max=3, state_list 0x1918. LEVEL3. */
void ai_type15(void);

/* Type 16 — горизонтальный летящий снаряд, файл 0x14593, cs:0x11D6:0x1033.
 * vx = 0xFFFE:0x8000 (= -1.5 px/frame fixed-point), state_list 0x1984. */
void ai_type16(void);

/* Type 17 — стандартный спавнящийся враг, файл 0x13B77, cs:0x11D6:0x0617.
 * state 0 — spawn(obj, sprite=0xA1=161, 0).  state 1 — state_list 0x1880,
 * sub_timer_max=2.  Простой шаблон. */
void ai_type17(void);

/* Type 18 — Y-snap к игроку, файл 0x147DD, cs:0x11D6:0x127D.
 * INIT копирует obj.y_world = primary_player.y_world (визуально прыгает
 * на уровень игрока). sub_timer_max=3, misc_state=2. */
void ai_type18(void);

/* Type 19 — медленный одно-bbox враг, файл 0x14C20, cs:0x11D6:0x16C0.
 * sprite_id=0xAC=172, sub_timer_max=0x3E8=1000 (очень медленный AI).
 * Делегирует TICK к cs:0x11D6:0x4B36 (тот же helper, что и type 9!). */
void ai_type19(void);

/* Type 20 — multi-bbox bosfire/snake, файл 0x1535D, cs:0x1348:0x06DD.
 * obj_type=9, num_bboxes = sub_timer (динамическое!), стрейд 0x12 байт
 * на bbox.  Aligns x_world по 8 пикселям.  LEVEL2. */
void ai_type20(void);

/* Type 21 — 7-сегментная цепь (фиксированная), файл 0x15DD3, cs:0x1348:0x1153.
 * num_bboxes=7, sprite[0]=0xB2=178 с offset (2,-3). Каждый bbox через
 * stride 0x12. Аналог type 20 но c фиксированной длиной. */
void ai_type21(void);

/* Type 22 — tile-aware AI, файл 0x15902, cs:0x1348:0x0C82.
 * При INIT конвертирует obj.x/y в tile-координаты через таблицы
 * `px_to_tile_x[]`/`px_to_tile_y[]` и вызывает `cs:0xD4B:0x21` —
 * tile lookup для контекста (читает карту тайлов!). LEVEL2+. */
void ai_type22(void);

/* Type 24 — multi-spawner (7 детей), файл 0x15029, cs:0x1348:0x03A9.
 * INIT вызывает cs:0x1348:0xF57 в цикле от 0 до 6, создавая 7 дочерних
 * объектов (вероятно "формация" летающих существ). */
void ai_type24(void);

/* Type 25 — randomized timer, файл 0x156A0, cs:0x1348:0x0A20.
 * Использует PRNG (cs:0x1CE7:0x17) для задержки 30..45 кадров между
 * спавнами/действиями. state_list 0x1A58. */
void ai_type25(void);

/* Type 26 — player-direction mover, файл 0x15AC6, cs:0x1348:0x0E46.
 * Pre-init выбирает velocity (0xC000:0) на основе direction к игроку.
 * sub_timer_max=2 (быстрый тик), state_list 0x1AA4. */
void ai_type26(void);

/* Type 27 — атака сверху, файл 0x1642B, cs:0x1348:0x17AB.
 * state 0 ставит obj.y = player_cache_y - 70 (появляется НАД игроком).
 * state_list 0x1B46, misc_state=0, обратный счётчик в +0xEB. */
void ai_type27(void);

/* Type 28 — спавнящийся с FX, файл 0x165FC, cs:0x1348:0x197C.
 * state 0: spawn FX (sprite 0xCF=207). state 1: state_list 0x1B92,
 * sub_timer_max=4, direction-aware. */
void ai_type28(void);

/* Type 29 — multi-phase с inner dispatcher, файл 0x161B1, cs:0x1348:0x1531.
 * sub_timer_max=8 (медленный), state_list 0x1AF0. Inner switch по
 * misc_state (8 веток). spawn FX с sprite 0xD3=211. */
void ai_type29(void);

/* Type 30 — динамический multi-bbox, файл 0x15F4F, cs:0x1348:0x12CF.
 * Начинает с num_bboxes=0, добавляет bboxes динамически в цикле:
 * нечётные индексы — sprite 0x98=152, чётные — sprite 0x05.
 * Якорится к primary_player.x. */
void ai_type30(void);

/* Type 31 — spawn (DD), файл 0x1679A, cs:0x1348:0x1B1A.
 * sub_timer_max=4, spawn FX (sprite 0xDD=221), state_list 0x1BC4. */
void ai_type31(void);

/* Type 32 — spawn (DE), файл 0x168B4, cs:0x1348:0x1C34.
 * sub_timer_max=4, spawn FX (sprite 0xDE=222), state_list 0x1BE0.
 * Близнец type 31 со сдвигом sprite_id +1. */
void ai_type32(void);

/* Type 33 — 7-bbox сегментный с HP-кэшем, файл 0x16A6D, cs:0x151A:0x00CD.
 * num_bboxes=7, кэш HP в obj+0xEB. Аналог type 21 (7-segment) но
 * сохраняет максимум HP для визуализации повреждения. */
void ai_type33(void);

/* Type 34 — 6-bbox с HP-индикатором, файл 0x16C46, cs:0x151A:0x02A6.
 * num_bboxes=6, обратный счётчик +0xEB=5, +0xEF=0. Использует HP в
 * +0xED для отслеживания здоровья каждого сегмента. */
void ai_type34(void);

/* Type 35 — двухрежимный по sub_timer_start, файл 0x17250, cs:0x151A:0x08B0.
 * Если sub_timer_start > 10: behavior_phase=1 (агрессивный), иначе 0
 * (пассивный). state_list зависит от режима. */
void ai_type35(void);

/* Type 36 — Ryu! Финальный босс LEVEL9, файл 0x16FBB, cs:0x151A:0x061B.
 * 2-bbox дракон (sprite 0xF5=245 + 0xF9=249), x_world выровнен по 8-px,
 * misc_state=0/behavior_phase=0 как стартовые фазы. */
void ai_type36(void);

/* Type 37 — slow boss с helper, файл 0x175D1, cs:0x151A:0x0C31.
 * sub_timer_max=7, state_list 0x1C86, использует helper 0x4B36
 * (тот же, что и types 9/19). HP в +0xED. */
void ai_type37(void);

/* Type 38 — long-timer enemy, файл 0x17867, cs:0x151A:0x0EC7.
 * sub_timer_max=7, state_ext=75 (длинный таймер), flag_2E=0 (невидим),
 * state_list 0x1CA4. */
void ai_type38(void);

/* Type 39 — 6-сегментная цепь (sprite 0x103), файл 0x179F2, cs:0x161F:0x0002.
 * num_bboxes=6, все sprite_id=0x103=259. Stride 0x12.
 * Близнец type 21 но из другого сегмента. */
void ai_type39(void);

/* Type 40 — multi-phase с set-bit маркером, файл 0x17E37, cs:0x161F:0x0447.
 * sub_timer_max=2, state_list 0x1D22. Использует трюк "misc_state |= 1"
 * (как type 29) для self-init маркировки. */
void ai_type40(void);

/* Type 41 — high-HP mid-boss, файл 0x17BFA, cs:0x161F:0x020A.
 * subtype=3, hit_points=100 (вдвое больше нормы), sub_timer_max=2,
 * state_list 0x1CBE. Вероятно mini-босс. */
void ai_type41(void);

/* Type 42 — 3-state объект (УНИКАЛЬНО!), файл 0x17C8F, cs:0x161F:0x029F.
 * Единственный handler с cmp ax,2 (только 3 ветки). Простой projectile/FX
 * с условным Y-смещением (-20 px при behavior_phase != 0). */
void ai_type42(void);

/* Type 43 — self-init с HP cache, файл 0x18325, cs:0x1692:0x0205.
 * misc_state |= 1 (self-init), HP в +0xEB, state_list 0x1D7E,
 * sub_timer_max=3. */
void ai_type43(void);

/* Type 44 — random-velocity projectile, файл 0x1864E, cs:0x1692:0x052E.
 * Использует PRNG (cs:0x1CE7:0x17): vy = rand & 0x1FFF + 0x2000
 * (random fixed-point ~8K-16K), vx = 0x3000. sub_timer_max=3. */
void ai_type44(void);

/* Type 45 — slower random-velocity projectile, файл 0x181D4, cs:0x1692:0x00B4.
 * Близнец type 44: PRNG mask тот же, но смещение 0x8000 (выше vy),
 * vx_lo=0x2000, sub_timer_max=7 (медленнее). */
void ai_type45(void);

/* Type 46 — extended-sprite враг, файл 0x1878B, cs:0x1692:0x066B.
 * spawn_fx с sprite_id=0x124=292 (>0xFF, extended range).
 * sub_timer_max=9, state_list 0x1DE4. */
void ai_type46(void);

/* Type 47 — Y-snap с PRNG, файл 0x18599, cs:0x1692:0x0479.
 * sub_timer_max=10 (очень медленный), y_world -= [DS:0x10D4],
 * state_list 0x1DA4, PRNG для рандомизации поведения. */
void ai_type47(void);

/* Type 48 — extended sprite spawn, файл 0x18A4C, cs:0x170C:0x018C.
 * spawn_fx(sprite=0x12B=299, ext), sub_timer_max=4. */
void ai_type48(void);

/* Type 49 — LEVEL9-conditional, файл 0x18C85, cs:0x170C:0x03C5.
 * HP cache в +0xEB. Активирует ветку при max_level==9 И obj.x > 0x44C —
 * Ryu helper или final boss minion. */
void ai_type49(void);

/* Type 50 — 7-сегмент extended (0x132), файл 0x18DEF, cs:0x175E:0x000F.
 * num_bboxes=7, sprite_id=0x132=306. */
void ai_type50(void);

/* Type 51 — мега-фиксированный объект, файл 0x19278, cs:0x175E:0x0498.
 * Сохраняет self в DS:0xD886/D888 (как type 7). num_bboxes=8,
 * sub_timer_max=0xFF (медленный), sprite 0x135=309. Stage-боссовый объект. */
void ai_type51(void);

/* Type 52 — Y-snap по 10, файл 0x18FE3, cs:0x175E:0x0203.
 * Выравнивает y_world по сетке 10 (y -= y%10). sub_timer_max=3. */
void ai_type52(void);

/* Type 53 — простой шаблон, файл 0x193DD, cs:0x17B6:0x007D.
 * sub_timer_max=2, state_list 0x1E96. */
void ai_type53(void);

/* Type 54 — extended sprite spawn (0x140), файл 0x1953B, cs:0x17B6:0x01DB.
 * sub_timer_max=4, state_list 0x1EB0. */
void ai_type54(void);

/* Type 55 — extended sprite (0x142), файл 0x196E8, cs:0x17EC:0x0028.
 * HP cache, misc_state=2, sub_timer_max=4. */
void ai_type55(void);

/* Type 56 — ТРИГГЕР ЗАВЕРШЕНИЯ УРОВНЯ (5 state-веток!), файл 0x19A72,
 * cs:0x17EC:0x03B2. play_sound(9) + 3 level-transition helpers. */
void ai_type56(void);

/* Type 57 — СТАТИЧЕСКАЯ ДЕКОРАЦИЯ (ОНЛИ 2 STATE!), файл 0x19CC7,
 * cs:0x17EC:0x0607. sub_timer_max=0xFF (вечный), sprite_id = init_param + 0x14E. */
void ai_type57(void);

/* Type 23 — преследователь, файл 0x14C87, cs:0x1348:0x007.
 * Двухфазное движение: pre/post collision с тайлами. */
void ai_type23(void);

/* ─── Глобальные переменные, используемые AI ─────────────────────── */

/* DS:0xD95F — far ptr на «основной» объект (игрок), используется
 * как референс для player-relative AI (например, type 23). */
extern ActiveObject* primary_player;

} /* namespace karnov */

#endif
