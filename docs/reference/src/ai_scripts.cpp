/* ⚠️ ДОСТОВЕРНОСТЬ ЭТОГО ФАЙЛА ОГРАНИЧЕНА (проверка 2026-08-14).
 *
 * Сверка с KARNOV.EXE показала:
 *   • `case 2: return;` здесь означает «ветку НЕ читали», а не «кода нет».
 *     Проверены типы 12, 20, 33, 39, 50 — во всех state 2 содержит
 *     реальную логику (контакт с полом, анимация, триггеры, урон).
 *   • Часть INIT'ов — ДОГАДКИ, противоречащие бинарнику. Известные:
 *     type 20 (x_offset = −si*8 → на деле dx=0, dy=si*20),
 *     type 30 (0x98/0x05 приняты за sprite_id → это x_offset, спрайт 0xDA),
 *     type 21/33/34/39/50/51 (раскладки боксов), type 36 (позиция хвоста).
 *     Актуальные раскладки — KARNOV.md §5.2.1.
 *   • Скалярные INIT-константы (sprite_id, hp, sub_timer_max) в выборке
 *     из 9 типов оказались верны; систематически отсутствует вторая
 *     половина INIT (скорости, state_list, ai_result, вызовы хелперов).
 *   • Обработчики, доступные только через queue_state, здесь не покрыты
 *     (их нет в таблице DS:0x10FE) — напр. cs:175E:0x3A9 у типа 51.
 *
 * Перед тем как опираться на любой фрагмент — сверяйтесь с дизассемблером.
 */

/* ai_scripts.cpp — таблица handler'ов и декомпилированные AI скрипты.
 *
 * Полная таблица (57 уникальных handler'ов) восстановлена из
 * DS:0x10FE в KARNOV.EXE.  Конкретные handler'ы декомпилированы
 * для типов, наиболее интересных архитектурно или встречающихся в
 * реальных уровнях.
 *
 *  ДЕКОМПИЛИРОВАНО НА ДАННЫЙ МОМЕНТ
 *  ────────────────────────────────
 *
 *  ROLE         | Type | C++          | EXE addr         | Описание
 *  -------------+------+--------------+------------------+----------
 *  PLAYER idle  |   1  | ai_type01    | cs:0xE62:0x0C49  | Карнов — состояние покоя
 *  PLAYER walk  |   2  | ai_type02    | cs:0xE62:0x13C6  | Карнов нормальный (×2 acc)
 *  PLAYER swim  |   3  | ai_type03    | cs:0xE62:0x14D7  | Карнов плавание/полёт (÷2 acc)
 *  Anim-trans   |   4  | ai_type04    | cs:0xFFA:0x0CF6  | Переход анимации (next state из type 1)
 *  ITEM drop    |   5  | ai_type05    | cs:0xFFA:0x0DAA  | Падающий бонус → дроп
 *  Enemy        |   6  | ai_type06    | cs:0xFFA:0x0EB8  | LEVEL0 общий враг (RNG)
 *  Trigger      |   7  | ai_type07    | cs:0xFFA:0x1032  | Невидимый трекер игрока
 *  Sky-spawn    |   8  | ai_type08    | cs:0xFFA:0x139F  | Случайный спавн у игрока (PRNG)
 *  Multi-sprite |   9  | ai_type09    | cs:0x11D6:0x1644 | 2-bbox enemy (boss-like)
 *  Pendulum     |  10  | ai_type10    | cs:0x11D6:0x0413 | Маятник X с flip-direction
 *  Range-AI     |  11  | ai_type11    | cs:0x11D6:0x04CA | Range-based трекер (10 px)
 *  Patrol       |  12  | ai_type12    | cs:0x11D6:0x08B3 | Наземный патруль (HP=20)
 *  Y-tracker    |  13  | ai_type13    | cs:0x11D6:0x06A8 | Y-snap + 2-stage spawn
 *  Sprite spawn |  14  | ai_type14    | cs:0x11D6:0x0E08 | Сначала FX (0x95), потом анимация
 *  Anchor-AI    |  15  | ai_type15    | cs:0x11D6:0x0ACB | LEVEL3 трекер по [0x100E]/[0x1045]
 *  Projectile   |  16  | ai_type16    | cs:0x11D6:0x1033 | Горизонтальный снаряд (vx ≈ -1.5)
 *  Std spawn    |  17  | ai_type17    | cs:0x11D6:0x0617 | spawn(sprite=0xA1)+state_list 0x1880
 *  Y-snap       |  18  | ai_type18    | cs:0x11D6:0x127D | Прыгает на y игрока
 *  Slow-AI      |  19  | ai_type19    | cs:0x11D6:0x16C0 | Медл. AI, sprite 0xAC, helper 0x4B36
 *  Multi-snake  |  20  | ai_type20    | cs:0x1348:0x06DD | Multi-bbox, динамический num_bboxes
 *  7-segment    |  21  | ai_type21    | cs:0x1348:0x1153 | 7 bbox-сегментов с фикс. длиной
 *  Tile-aware   |  22  | ai_type22    | cs:0x1348:0x0C82 | Читает карту тайлов для AI
 *  Tracker      |  23  | ai_type23    | cs:0x1348:0x007  | Преследователь, LEVEL3/9
 *  Multi-spawn  |  24  | ai_type24    | cs:0x1348:0x03A9 | Спавнит 7 детей в формации
 *  PRNG-timer   |  25  | ai_type25    | cs:0x1348:0x0A20 | Задержка 30..45 кадров (PRNG)
 *  Dir-mover    |  26  | ai_type26    | cs:0x1348:0x0E46 | velocity 0xC000 к игроку
 *  Top-drop     |  27  | ai_type27    | cs:0x1348:0x17AB | Атака с y=player-70
 *  FX-spawn     |  28  | ai_type28    | cs:0x1348:0x197C | Spawn FX (0xCF) + direction
 *  Multi-phase  |  29  | ai_type29    | cs:0x1348:0x1531 | Inner switch по misc_state (8 фаз)
 *  Dyn-bbox     |  30  | ai_type30    | cs:0x1348:0x12CF | num_bboxes растёт во времени
 *  Spawn-DD     |  31  | ai_type31    | cs:0x1348:0x1B1A | spawn FX sprite 0xDD
 *  Spawn-DE     |  32  | ai_type32    | cs:0x1348:0x1C34 | spawn FX sprite 0xDE (близнец 31)
 *  7-bbox HP    |  33  | ai_type33    | cs:0x151A:0x00CD | 7-сегмент с кэшем HP
 *  6-bbox HP    |  34  | ai_type34    | cs:0x151A:0x02A6 | 6-сегмент с HP-индикатором
 *  Dual-mode    |  35  | ai_type35    | cs:0x151A:0x08B0 | 2 режима по sub_timer_start
 *  ★ RYU BOSS   |  36  | ai_type36    | cs:0x151A:0x061B | ФИНАЛЬНЫЙ БОСС LEVEL9
 *  Slow boss    |  37  | ai_type37    | cs:0x151A:0x0C31 | helper 0x4B36, HP cache
 *  Long-timer   |  38  | ai_type38    | cs:0x151A:0x0EC7 | state_ext=75 (длинный)
 *  6-chain      |  39  | ai_type39    | cs:0x161F:0x0002 | 6-bbox sprite 0x103
 *  Self-init    |  40  | ai_type40    | cs:0x161F:0x0447 | misc_state |= 1 трюк
 *  Mid-boss     |  41  | ai_type41    | cs:0x161F:0x020A | HP=100, subtype=3
 *  3-state ★    |  42  | ai_type42    | cs:0x161F:0x029F | УНИКАЛЬНО: только 3 ветки!
 *  Self-init HP |  43  | ai_type43    | cs:0x1692:0x0205 | self-init + HP cache
 *  PRNG-proj    |  44  | ai_type44    | cs:0x1692:0x052E | random vy=0x2000-0x4000
 *  PRNG-slow    |  45  | ai_type45    | cs:0x1692:0x00B4 | random vy=0x8000+, slower
 *  Extended spr |  46  | ai_type46    | cs:0x1692:0x066B | sprite 0x124=292 (>0xFF)
 *  Y-snap PRNG  |  47  | ai_type47    | cs:0x1692:0x0479 | sub_timer_max=10 + rand
 *  Ext-spr      |  48  | ai_type48    | cs:0x170C:0x018C | sprite 0x12B=299 extended
 *  LEVEL9-cond  |  49  | ai_type49    | cs:0x170C:0x03C5 | LEVEL9 + x > 0x44C trigger
 *  7-chain ext  |  50  | ai_type50    | cs:0x175E:0x000F | 7 bbox sprite 0x132=306
 *  Mega-static  |  51  | ai_type51    | cs:0x175E:0x0498 | 8 bbox, timer=0xFF, global slot
 *  Y10-snap     |  52  | ai_type52    | cs:0x175E:0x0203 | Y aligned к сетке 10
 *  Simple-tpl   |  53  | ai_type53    | cs:0x17B6:0x007D | базовый шаблон
 *  Ext-spr-2    |  54  | ai_type54    | cs:0x17B6:0x01DB | sprite 0x140=320
 *  Ext-spr-3    |  55  | ai_type55    | cs:0x17EC:0x0028 | sprite 0x142=322
 *  ★ LEVEL-END  |  56  | ai_type56    | cs:0x17EC:0x03B2 | ТРИГГЕР завершения уровня
 *  ★ DECORATION |  57  | ai_type57    | cs:0x17EC:0x0607 | СТАТИЧЕСКАЯ декорация (2 state!)
 *
 *  ВСЕ 57 ТИПОВ ДЕКОМПИЛИРОВАНЫ! (типы 1-57 + NULL type 0)
 *
 *  Общая структура (switch по
 *  state_byte через jump-table), но требуют отдельной декомпиляции.
 *
 *  СТРУКТУРА ОБЩЕГО HANDLER'А
 *  ──────────────────────────
 *
 *     void ai_typeN(void) {
 *         ax = current_state_byte    // [DS:0xD8A2]
 *         if (ax > MAX_STATE) return // фильтр на верхние состояния
 *         bx = ax * 2
 *         jmp [cs:bx + JUMP_TABLE]    // обычно 4..7 веток
 *     }
 *
 *  ТИПОВЫЕ ВЕТКИ
 *  ─────────────
 *
 *    state 0/1  — INIT: установить bbox[0].sprite_id, sub_timer_max,
 *                 vx/vy (16.16 fixed-point в +0x0E..+0x14)
 *    state 2    — TICK: проиграть анимацию, при последнем кадре —
 *                 queue_state(obj, next_handler)
 *    state 3,5  — промежуточные/idle
 *    state 4    — HURT (устанавливается pair-loop type-0)
 *    state 6    — DYING / suspend через ai_result |= 1
 *
 *  ПОЛЯ ОБЪЕКТА, ИСПОЛЬЗУЕМЫЕ В AI
 *  ──────────────────────────────
 *    +0x08    x_world      (16-bit world X)
 *    +0x0A    -            (padding или extra)
 *    +0x0C    y_world      (16-bit world Y)
 *    +0x0E    vx_lo / vy_hi (32-bit fixed-point velocity)
 *    +0x10    vx_hi
 *    +0x12    vy_lo
 *    +0x14    vy_hi
 *    +0x1A/+0x1C  связанные поля (сбрасываются при clamp в type 3)
 *    +0x1E    num_bboxes
 *    +0x20..+0xAF   массив SpriteBox (до 8 шт. × 18 байт)
 *    +0x28    bbox[0].????  (используется в type 5 collision check)
 *    +0x2E    visible flag (часто = 1)
 *    +0xB7/+0xB9  current AI handler (call far)
 *    +0xBB/+0xBD  default handler (на жёсткий reset)
 *    +0xBF/+0xC1  queued handler (для перехода через ai_dispatch state==1)
 *    +0xC5    sub_timer
 *    +0xC9    sub_timer_max
 *    +0xD5/+0xD7  state_list far ptr (анимации)
 *    +0xDB    active_flag
 *    +0xE5    obj_type (subtype, copied from spawn[2])
 *    +0xE7    hit_points
 *    +0xE9    misc_state    (handler-specific)
 *    +0xED    behavior_phase (handler-specific)
 *    +0xEF    alt_sub_phase  (handler-specific)
 *
 *  СИГНАЛЫ ВОЗВРАТА (DS:0xD963)
 *  ──────────────────────────────
 *    bit 0  → объект suspended; индекс → revival_list
 *    bit 1  → переход в новое состояние из obj.state_list[pending_state_id]
 *    bit 2  → animation tick (внутренний)
 *
 *  ПОЛНЫЙ СПИСОК АДРЕСОВ (все 57 типов)
 *
 *  ПОЛНЫЙ СПИСОК HANDLER'ОВ
 *  ────────────────────────
 *  type   1 → cs:0xE62:0xC49  (file 0x10A69) — Карнов idle               [DECOMPILED ai_type01]
 *  type   2 → cs:0xE62:0x13C6 (file 0x111E6) — Карнов нормальная физика  [DECOMPILED ai_type02]
 *  type   3 → cs:0xE62:0x14D7 (file 0x112F7) — Карнов плавание/полёт     [DECOMPILED ai_type03]
 *  type   4 → cs:0xFFA:0xCF6  (file 0x12496) — anim transition (next из type 1)  [DECOMPILED ai_type04]
 *  type   5 → cs:0xFFA:0xDAA  (file 0x1254A) — падающий бонус            [DECOMPILED ai_type05]
 *  type   6 → cs:0xFFA:0xEB8  (file 0x12658) — common enemy, LEVEL0+     [DECOMPILED ai_type06]
 *  type   7 → cs:0xFFA:0x1032 (file 0x127D2) — невидимый трекер          [DECOMPILED ai_type07]
 *  type   8 → cs:0xFFA:0x139F (file 0x12B3F) — sky-spawn у игрока, LEVEL1+    [DECOMPILED ai_type08]
 *  type   9 → cs:0x11D6:0x1644 (file 0x14BA4) — 2-bbox enemy            [DECOMPILED ai_type09]
 *  type  10 → cs:0x11D6:0x413  (file 0x13973) — горизонтальный маятник  [DECOMPILED ai_type10]
 *  type  11 → cs:0x11D6:0x4CA  (file 0x13A2A) — range-based трекер      [DECOMPILED ai_type11]
 *  type  12 → cs:0x11D6:0x8B3  (file 0x13E13) — наземный патруль        [DECOMPILED ai_type12]
 *  type  13 → cs:0x11D6:0x6A8  (file 0x13C08) — Y-snap трекер           [DECOMPILED ai_type13]
 *  type  14 → cs:0x11D6:0xE08  (file 0x14368) — спавн со спрайтом       [DECOMPILED ai_type14]
 *  type  15 → cs:0x11D6:0xACB  (file 0x1402B) — anchor-AI, LEVEL3       [DECOMPILED ai_type15]
 *  type  16 → cs:0x11D6:0x1033 (file 0x14593) — горизонт. снаряд        [DECOMPILED ai_type16]
 *  type  17 → cs:0x11D6:0x617  (file 0x13B77) — std spawn (0xA1)        [DECOMPILED ai_type17]
 *  type  18 → cs:0x11D6:0x127D (file 0x147DD) — Y-snap к игроку         [DECOMPILED ai_type18]
 *  type  19 → cs:0x11D6:0x16C0 (file 0x14C20) — slow AI (helper 0x4B36) [DECOMPILED ai_type19]
 *  type  20 → cs:0x1348:0x6DD (file 0x1535D) — multi-bbox snake, LEVEL2 [DECOMPILED ai_type20]
 *  type  21 → cs:0x1348:0x1153 (file 0x15DD3) — 7-segment chain               [DECOMPILED ai_type21]
 *  type  22 → cs:0x1348:0xC82 (file 0x15902)  — tile-aware AI                  [DECOMPILED ai_type22]
 *  type  23 → cs:0x1348:0x007 (file 0x14C87) — преследователь, LEVEL3/9
 *  type  24 → cs:0x1348:0x3A9 (file 0x15029)  — multi-spawner (7 детей)       [DECOMPILED ai_type24]
 *  type  25 → cs:0x1348:0xA20 (file 0x156A0)  — PRNG-timer (30..45)           [DECOMPILED ai_type25]
 *  type  26 → cs:0x1348:0xE46 (file 0x15AC6)  — direction-mover (vx=0xC000)  [DECOMPILED ai_type26]
 *  type  27 → cs:0x1348:0x17AB (file 0x1642B) — top-drop (y=player-70)        [DECOMPILED ai_type27]
 *  type  28 → cs:0x1348:0x197C (file 0x165FC) — FX-spawn (sprite 0xCF)        [DECOMPILED ai_type28]
 *  type  29 → cs:0x1348:0x1531 (file 0x161B1) — multi-phase (8 sub-фаз)       [DECOMPILED ai_type29]
 *  type  30 → cs:0x1348:0x12CF (file 0x15F4F) — dynamic multi-bbox            [DECOMPILED ai_type30]
 *  type  31 → cs:0x1348:0x1B1A (file 0x1679A) — spawn FX sprite 0xDD          [DECOMPILED ai_type31]
 *  type  32 → cs:0x1348:0x1C34 (file 0x168B4) — spawn FX sprite 0xDE          [DECOMPILED ai_type32]
 *  type  33 → cs:0x151A:0xCD  (file 0x16A6D) — 7-bbox HP-cache               [DECOMPILED ai_type33]
 *  type  34 → cs:0x151A:0x2A6 (file 0x16C46) — 6-bbox HP-indicator           [DECOMPILED ai_type34]
 *  type  35 → cs:0x151A:0x8B0 (file 0x17250) — dual-mode by sub_timer        [DECOMPILED ai_type35]
 *  type  36 → cs:0x151A:0x61B (file 0x16FBB) — ★★★ Ryu BOSS (LEVEL9) ★★★    [DECOMPILED ai_type36]
 *  type  37 → cs:0x151A:0xC31 (file 0x175D1) — slow boss + helper            [DECOMPILED ai_type37]
 *  type  38 → cs:0x151A:0xEC7 (file 0x17867) — long-timer enemy              [DECOMPILED ai_type38]
 *  type  39 → cs:0x161F:0x002 (file 0x179F2) — 6-chain sprite 0x103          [DECOMPILED ai_type39]
 *  type  40 → cs:0x161F:0x447 (file 0x17E37) — self-init multi-phase         [DECOMPILED ai_type40]
 *  type  41 → cs:0x161F:0x20A (file 0x17BFA) — mid-boss HP=100              [DECOMPILED ai_type41]
 *  type  42 → cs:0x161F:0x29F (file 0x17C8F) — УНИКАЛЬНО: 3 state-ветки     [DECOMPILED ai_type42]
 *  type  43 → cs:0x1692:0x205 (file 0x18325) — self-init + HP cache         [DECOMPILED ai_type43]
 *  type  44 → cs:0x1692:0x52E (file 0x1864E) — PRNG-projectile (быстрый)    [DECOMPILED ai_type44]
 *  type  45 → cs:0x1692:0xB4  (file 0x181D4) — PRNG-projectile (медленный)  [DECOMPILED ai_type45]
 *  type  46 → cs:0x1692:0x66B (file 0x1878B) — extended sprite 0x124        [DECOMPILED ai_type46]
 *  type  47 → cs:0x1692:0x479 (file 0x18599) — Y-snap + PRNG                [DECOMPILED ai_type47]
 *  type  48 → cs:0x170C:0x18C (file 0x18A4C) — ext sprite 0x12B               [DECOMPILED ai_type48]
 *  type  49 → cs:0x170C:0x3C5 (file 0x18C85) — LEVEL9 conditional             [DECOMPILED ai_type49]
 *  type  50 → cs:0x175E:0x00F (file 0x18DEF) — 7-chain sprite 0x132          [DECOMPILED ai_type50]
 *  type  51 → cs:0x175E:0x498 (file 0x19278) — mega-static (8 bbox)          [DECOMPILED ai_type51]
 *  type  52 → cs:0x175E:0x203 (file 0x18FE3) — Y-snap 10                      [DECOMPILED ai_type52]
 *  type  53 → cs:0x17B6:0x07D (file 0x193DD) — simple template               [DECOMPILED ai_type53]
 *  type  54 → cs:0x17B6:0x1DB (file 0x1953B) — ext sprite 0x140               [DECOMPILED ai_type54]
 *  type  55 → cs:0x17EC:0x028 (file 0x196E8) — ext sprite 0x142               [DECOMPILED ai_type55]
 *  type  56 → cs:0x17EC:0x3B2 (file 0x19A72) — ★ LEVEL-END trigger            [DECOMPILED ai_type56]
 *  type  57 → cs:0x17EC:0x607 (file 0x19CC7) — ★ STATIC DECORATION            [DECOMPILED ai_type57]
 */
#include "ai_scripts.h"
#include "globals.h"
#include "think.h"
#include <cstdlib>

namespace karnov {

/* В реконструкции "указатели" — функции C++.  Реальные значения
 * (off, seg) сохранены в комментариях. */
const AiTableEntry ai_handler_table[58] = {
    /*  0 */ {0x0000, 0x0000},        /* NULL */
    /*  1 */ {0x0C49, 0x0E62},
    /*  2 */ {0x13C6, 0x0E62},
    /*  3 */ {0x14D7, 0x0E62},
    /*  4 */ {0x0CF6, 0x0FFA},
    /*  5 */ {0x0DAA, 0x0FFA},
    /*  6 */ {0x0EB8, 0x0FFA},
    /*  7 */ {0x1032, 0x0FFA},
    /*  8 */ {0x139F, 0x0FFA},
    /*  9 */ {0x1644, 0x11D6},
    /* 10 */ {0x0413, 0x11D6}, /* 11 */ {0x04CA, 0x11D6},
    /* 12 */ {0x08B3, 0x11D6}, /* 13 */ {0x06A8, 0x11D6},
    /* 14 */ {0x0E08, 0x11D6}, /* 15 */ {0x0ACB, 0x11D6},
    /* 16 */ {0x1033, 0x11D6}, /* 17 */ {0x0617, 0x11D6},
    /* 18 */ {0x127D, 0x11D6}, /* 19 */ {0x16C0, 0x11D6},
    /* 20 */ {0x06DD, 0x1348}, /* 21 */ {0x1153, 0x1348},
    /* 22 */ {0x0C82, 0x1348}, /* 23 */ {0x0007, 0x1348},
    /* 24 */ {0x03A9, 0x1348}, /* 25 */ {0x0A20, 0x1348},
    /* 26 */ {0x0E46, 0x1348}, /* 27 */ {0x17AB, 0x1348},
    /* 28 */ {0x197C, 0x1348}, /* 29 */ {0x1531, 0x1348},
    /* 30 */ {0x12CF, 0x1348}, /* 31 */ {0x1B1A, 0x1348},
    /* 32 */ {0x1C34, 0x1348}, /* 33 */ {0x00CD, 0x151A},
    /* 34 */ {0x02A6, 0x151A}, /* 35 */ {0x08B0, 0x151A},
    /* 36 */ {0x061B, 0x151A}, /* 37 */ {0x0C31, 0x151A},
    /* 38 */ {0x0EC7, 0x151A}, /* 39 */ {0x0002, 0x161F},
    /* 40 */ {0x0447, 0x161F}, /* 41 */ {0x020A, 0x161F},
    /* 42 */ {0x029F, 0x161F}, /* 43 */ {0x0205, 0x1692},
    /* 44 */ {0x052E, 0x1692}, /* 45 */ {0x00B4, 0x1692},
    /* 46 */ {0x066B, 0x1692}, /* 47 */ {0x0479, 0x1692},
    /* 48 */ {0x018C, 0x170C}, /* 49 */ {0x03C5, 0x170C},
    /* 50 */ {0x000F, 0x175E}, /* 51 */ {0x0498, 0x175E},
    /* 52 */ {0x0203, 0x175E}, /* 53 */ {0x007D, 0x17B6},
    /* 54 */ {0x01DB, 0x17B6}, /* 55 */ {0x0028, 0x17EC},
    /* 56 */ {0x03B2, 0x17EC}, /* 57 */ {0x0607, 0x17EC},
};

/* DS:0xD95F — far ptr на player object. */
ActiveObject* primary_player = nullptr;

/* ───────────────────────────────────────────────────────────────────
 * Helper: queue_state (cs:0xD4B:0xB31)
 *
 * Соответствует фрагменту cs:0xF7E1..0xF81D в оригинале:
 *   void queue_state(obj, new_handler):
 *       obj.ai_init_seg = new_handler.seg
 *       obj.ai_init_off = new_handler.off       ; +0xBF..+0xC1
 *       obj.state_byte  = 1                       ; reload на след. тике
 *       if (obj.ai_handler == 0):
 *           obj.ai_handler = new_handler         ; +0xB7..+0xB9 если был NULL
 * ─────────────────────────────────────────────────────────────────── */
static void queue_state(ActiveObject* obj, void (*new_handler)()) {
    /* В реконструкции — простое присваивание.  16-разрядные слоты
     * для handler-указателей не вмещают современный 64-битный
     * указатель целиком, поэтому здесь — только структура вызова. */
    (void)new_handler;
    obj->state_byte = 1;
    /* obj->ai_handler_off = LO16(ptr);
     * obj->ai_handler_seg = HI16(ptr); */
}

/* Helper: pseudo random number (cs:0xD4B:0x105). */
static u16 rng_next() {
    /* В оригинале — линейный конгруэнтный генератор; здесь упрощённо. */
    return static_cast<u16>(std::rand() & 0xFFFF);
}

/* Helper: animation length (anim_script[0]). */
static u16 anim_frames(ActiveObject* obj) {
    /* В оригинале: les bx, [obj.state_seg:state_off]; ax = [es:bx]. */
    /* return *(u16*)obj.state_ptr; */
    (void)obj;
    return 1;
}

/* Helper: tile collision check (cs:0x6E:0x7A5).
 * Возвращает != 0, если bbox объекта пересекает «твёрдый» тайл карты. */
static int check_tile_collision(ActiveObject* obj) {
    /* TODO: cs:0x6E:0x7A5 — обход bboxes и проверка против map_buffer. */
    (void)obj;
    return 0;
}

/* ───────────────────────────────────────────────────────────────────
 * Type 1 — базовый враг (file 0x10A69, cs:0xE62:0xC49)
 *
 * Декомпиляция:
 *   void ai_type01() {
 *       switch (current_state_byte) {       // [DS:0xD8A2]
 *       case 0: case 1:                       // init
 *           goto init;
 *       case 2:                               // tick
 *           goto tick;
 *       default:                              // 3..N — выход
 *           return;
 *       }
 *
 *   init (0x10A86 в файле):
 *       les bx, [DS:0xD8A6]                   // current_obj
 *       obj.state_ext     = 0                 // +0xC7
 *       obj.active_flag   = 0                 // +0xDB (NB: 0, не 1!)
 *       obj.sub_timer     = 0                 // +0xC5
 *       obj.bbox[0].sprite_id = 0             // +0x20
 *       return
 *
 *   tick (0x10AB9):
 *       les bx, [DS:0xD8A6]
 *       if (obj.state_ext == 6) {
 *           pending_state_id = 0              // [DS:0xD88C]
 *           ai_result |= 6                    // suspend + transition
 *           obj.active_flag = 1
 *       }
 *       if (obj.active_flag != 0) {
 *           if (obj.sub_timer == anim_frames(obj) - 1) {
 *               queue_state(obj, type04_handler);  // переход в "fall"
 *           }
 *           ai_result |= 4                     // "animation tick"
 *       }
 *       return
 *   }
 * ─────────────────────────────────────────────────────────────────── */
void ai_type01() {
    ActiveObject* obj = current_obj;
    if (!obj) return;

    if (current_state_byte > 2) return;

    if (current_state_byte == 0 || current_state_byte == 1) {
        /* init */
        obj->state_ext         = 0;
        obj->active_flag       = 0;
        obj->sub_timer         = 0;
        obj->bboxes[0].sprite_id = 0;
        return;
    }

    /* state 2: tick */
    if (obj->state_ext == 6) {
        pending_state_id = 0;
        ai_result |= 6;                         /* suspend + transition */
        obj->active_flag = 1;
    }
    if (obj->active_flag != 0) {
        if (obj->sub_timer == anim_frames(obj) - 1) {
            /* Last frame of current animation — queue next state. */
            queue_state(obj, /* type04 handler */ nullptr);
            /* TODO: queue_state(obj, &ai_type04_handler); */
        }
        ai_result |= 4;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 2 — Карнов: нормальная физика игрока (file 0x111E6, cs:0xE62:0x13C6)
 *
 * Это handler НЕ обычного врага — он читает ввод игрока (DS:0xCCAC =
 * direction_bits) и обновляет физику. Поля +0x10/+0xE и +0x14/+0x12
 * хранят 32-битные fixed-point значения скорости (через
 * Turbo-C runtime _lshift_long в cs:0:0x41C).
 *
 *   void ai_type02() {
 *       ax = current_state_byte - 1
 *       if (ax > 3) return                              ; state 0,5+ — пропуск
 *       jmp [cs:state_minus_1*2 + JT]                     ; jump-table 4 ветки
 *
 *       JT[0]: state == 1 → INIT (0x11206) + fall through to TICK
 *       JT[1]: state == 2 → TICK  (0x11247)
 *       JT[2]: state == 3 → UPDATE_DIR_FLAGS (0x112C6)
 *       JT[3]: state == 4 → DESTROY (0x112F1)
 *   }
 *
 *   INIT (state==1):
 *       les bx, [DS:0xD8A6]                       // current_obj = Karnov
 *       obj.state_list = (DS:0x15A6)              // +0xD5/+0xD7 — анимации
 *       call cs:0xE62:0xFF7(4)                     // загрузить кадр анимации
 *       [DS:0xD8D4]:[DS:0xD8D2] = (DS:0x15B6)      // some "Karnov context"
 *       obj.flag_2E       = 1                      // visible
 *       pending_state_id  = 0
 *       ai_result        |= 2                      // signal transition
 *       obj.sub_timer_max = 4                       // +0xC9 — 4 кадра на цикл
 *       ; fall-through to TICK
 *
 *   TICK (state==2 || fall-through):
 *       if [DS:0xD8B6] <= 0:  goto MOVEMENT_BLOCKED
 *
 *       ; ИЗМЕНЕНИЕ НАПРАВЛЕНИЯ:
 *       ax = ~[DS:0xD8AE]                          // прошлый direction snapshot
 *       ax &= [DS:0xCCAC]                          // новые нажатые направления
 *       [DS:0xD968] = ax                            // "пресс-эдж" битовая маска
 *       call cs:0xD4B:0x8D(ax)                      // anim_for_direction
 *
 *       if [DS:0xD8B4] != 0:                       // флаг "двигаться можно"
 *           ; УСКОРЕНИЕ: x2 для vx и vy (lshift на 1).
 *           ; vx_hi:vx_lo = obj+0x10 : obj+0xE  (32-bit fixed-point velocity X)
 *           dx,ax = lshift_long({obj+0x10, obj+0xE}, cx=1)   // ×2
 *           obj+0x10 = dx;  obj+0xE = ax
 *           ; vy_hi:vy_lo = obj+0x14 : obj+0x12  (velocity Y)
 *           dx,ax = lshift_long({obj+0x14, obj+0x12}, cx=1)
 *           obj+0x14 = dx;  obj+0x12 = ax
 *
 *       call cs:0xD4B:0x3A5                         // apply physics
 *       ax = [DS:0xD8B0] & 1                        // vert_dir_cache low bit
 *       pending_state_id = ax                        // 0 или 1 (горизонталь/вертикаль)
 *       ai_result |= 6                               // suspend + transition
 *       return
 *
 *     MOVEMENT_BLOCKED:                              // [DS:0xD8B6] <= 0
 *       call cs:0xE62:0x40F(0)                       // "stop sound" / "no-op"
 *       return
 *
 *   UPDATE_DIR_FLAGS (state==3):
 *       for (i = 0; i < [DS:0xD8D8]; ++i):
 *           j = [DS:0xD8DA + i*2]                    // index in 1st table
 *           ax = [DS:0x15C0 + j*2]                   // value in 2nd table
 *           [DS:0xD8AE] |= ax                        // OR in direction state
 *       return
 *
 *   DESTROY (state==4):
 *       call cs:0xE62:0x51E                          // probably death FX
 *       return
 * ─────────────────────────────────────────────────────────────────── */
void ai_type02() {
    ActiveObject* obj = current_obj;
    if (!obj) return;

    /* Pre-dispatch: state - 1, фильтр 0,5+ */
    u16 state_m1 = static_cast<u16>(current_state_byte - 1);
    if (state_m1 > 3) return;

    switch (state_m1) {

    case 0: {  /* state == 1: INIT */
        /* obj.state_list = (DS:0x15A6) */
        /* В реконструкции анимации недоступны, но структура та же. */
        obj->state_list_off = 0x15A6;
        /* obj.state_list_seg = ds (текущий) */

        /* call cs:0xE62:0xFF7(4)  — load animation frame 4 */
        /* (...) */

        /* obj.flag_2E */
        /* obj->_pad2E = 1; */
        pending_state_id    = 0;
        ai_result          |= 2;
        obj->sub_timer_max  = 4;

        /* fall through to TICK */
        [[fallthrough]];
    }

    case 1: {  /* state == 2: TICK */
        /* if (DS:0xD8B6 <= 0): movement blocked */
        /* extern s16 movement_allowed; if (movement_allowed <= 0) ... */

        u16 dir_edge = static_cast<u16>(~/*DS:0xD8AE*/0 & direction_bits);
        /* DS:0xD968 = dir_edge; */
        (void)dir_edge;
        /* anim_for_direction(dir_edge); — cs:0xD4B:0x8D */

        /* if (DS:0xD8B4 != 0) — обновить fixed-point скорости (×2) */
        /* dx:ax = lshift_long(obj+0x10:obj+0xE, 1)
         * obj+0x10:obj+0xE = dx:ax
         * dx:ax = lshift_long(obj+0x14:obj+0x12, 1)
         * obj+0x14:obj+0x12 = dx:ax */

        /* call cs:0xD4B:0x3A5 — apply physics */
        u16 vd = static_cast<u16>(vert_dir_cache & 1);
        pending_state_id    = vd;
        ai_result          |= 6;
        return;
    }

    case 2: {  /* state == 3: UPDATE_DIR_FLAGS */
        /* for (i = 0; i < [DS:0xD8D8]; ++i):
         *     j = [DS:0xD8DA + i*2]
         *     ax = [DS:0x15C0 + j*2]
         *     [DS:0xD8AE] |= ax */
        /* TODO: декодирование таблиц DS:0xD8DA и DS:0x15C0. */
        return;
    }

    case 3: {  /* state == 4: DESTROY */
        /* call cs:0xE62:0x51E — death FX */
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 3 — Карнов: альтернативная физика (file 0x112F7, cs:0xE62:0x14D7)
 *
 * Аналогично type 2, но:
 *   • Скорости ДЕЛЯТСЯ пополам через cs:0:0x430 (_rshift_long), а не
 *     удваиваются. Это вероятно режим водного плавания (LEVEL 5) или
 *     полёта на крыльях (LEVEL 8), упомянутые в KARNOV.TXT.
 *   • Имеет дополнительный счётчик «misc_state» (+0xE9) с wrap 0..6.
 *   • Содержит ВЛОЖЕННЫЙ диспетчер в state 3 (7 веток через таблицу
 *     cs:0xE62:0x1643).
 *
 *   void ai_type03() {
 *       ax = state - 1
 *       if (ax > 3) return
 *       jmp [cs:ax*2 + JT_2]:
 *           [0] 0x14F7 → state 1: INIT (0x11317)
 *           [1] 0x1548 → state 2: TICK (0x11368)
 *           [2] 0x161E → state 3: SUB_DISPATCH (0x1143E)
 *           [3] 0x16ED → state 4: DESTROY (0x1150D)
 *   }
 *
 *   INIT (state==1, file 0x11317):
 *       obj.misc_state = 0                          // +0xE9
 *       call cs:0x41D:0xB4F(0x0F, 1)                 // sound? sprite_setup?
 *       call cs:0xE62:0xFF7(3)                       // load anim 3
 *       [DS:0xD8D4]:[DS:0xD8D2] = (DS:0x164A)        // context
 *       obj.flag_2E = 1
 *       obj.state_list = (DS:0x160A)                 // другие анимации!
 *       pending_state_id = 0
 *       ai_result |= 2
 *       obj.active_flag = 0                           // +0xDB = 0
 *       --obj.misc_state
 *       if (obj.misc_state < 0):
 *           obj.misc_state = 6                        // wrap к 6
 *           obj.active_flag = 1                       // активировать
 *
 *       ; fall-through to TICK (НЕ STATE 2, но IMMEDIATELY...)
 *       ax = ~[DS:0xD8AE] & [DS:0xCCAC]
 *       [DS:0xD968] = ax
 *       call cs:0xD4B:0x8D(ax)
 *
 *       if [DS:0xD8BA] == 0:                          // другой флаг чем type 2!
 *           ; ЗАМЕДЛЕНИЕ vx, vy /= 2 (rshift)
 *           dx,ax = rshift_long({obj+0x10, obj+0xE}, 1)
 *           obj+0x10, obj+0xE = dx, ax
 *           dx,ax = rshift_long({obj+0x14, obj+0x12}, 1)
 *           obj+0x14, obj+0x12 = dx, ax
 *
 *       ; Маска: оставить только знаковый бит vy (0x8000)
 *       obj+0x0E &= 0x8000
 *       call cs:0xD4B:0x3A5
 *
 *       ; Направление перехода:
 *       ax = 0
 *       dx = [DS:0xD8B0] & 1
 *       ax += dx
 *       if [DS:0xD8BA] != 0:  ax += 4
 *       pending_state_id = ax
 *       ai_result |= 6
 *       return
 *
 *   SUB_DISPATCH (state==3, file 0x1143E):
 *       for (i = 0; i < [DS:0xD8D8]; ++i):
 *           v = [DS:0xD8F2 + i*2] - 1
 *           if (v > 6): continue
 *           jmp [cs:0x1643 + v*2]:
 *              0,1 → 0x11471: clamp negative vy (если obj+0x14 < 0:
 *                              сбросить +0x14, +0x12, +0x1C, +0x1A в 0)
 *              2..5 → 0x114E7: (продолжение в следующем блоке кода)
 *              6 → 0x114BF: ...
 *
 *   DESTROY (state==4): тот же helper что type 2: cs:0xE62:0x51E.
 *
 * Резюме: type 3 — это «Карнов в воздухе» или «Карнов под водой» — у него:
 *   • замедленная физика (×0.5 вместо ×2)
 *   • дополнительный таймер misc_state с автоматическим перезапуском
 *   • clamp на отрицательную vy (нельзя слишком быстро вниз)
 *   • расширенные переходы (через [DS:0xD8BA] = «находится в воздухе»)
 * ─────────────────────────────────────────────────────────────────── */
void ai_type03() {
    ActiveObject* obj = current_obj;
    if (!obj) return;

    u16 state_m1 = static_cast<u16>(current_state_byte - 1);
    if (state_m1 > 3) return;

    switch (state_m1) {

    case 0: {  /* state == 1: INIT + immediate TICK */
        /* obj.misc_state = 0  // +0xE9 */
        /* call cs:0x41D:0xB4F(15, 1) — load sprite frame 15 */
        /* call cs:0xE62:0xFF7(3)     — load animation 3 */
        obj->state_list_off = 0x160A;
        pending_state_id    = 0;
        ai_result          |= 2;
        obj->active_flag    = 0;

        /* Decrement and wrap misc_state. */
        s16 misc = -1;  /* (--obj.misc_state in actual code) */
        if (misc < 0) {
            /* obj.misc_state = 6; */
            obj->active_flag = 1;
        }

        /* Compute direction edge mask. */
        u16 dir_edge = static_cast<u16>(~/*DS:0xD8AE*/0 & direction_bits);
        (void)dir_edge;
        /* call cs:0xD4B:0x8D(dir_edge) — anim_for_direction */

        /* if ([DS:0xD8BA] == 0):  rshift vx/vy by 1 (× 0.5) */
        /* dx,ax = rshift_long(obj+0x10:obj+0xE, 1);  store back */
        /* dx,ax = rshift_long(obj+0x14:obj+0x12, 1); store back */

        /* Mask vy to sign-bit only. */
        /* obj+0x0E &= 0x8000; */

        /* call cs:0xD4B:0x3A5 — apply physics */

        /* Compute pending_state_id: vert_dir & 1, plus 4 if [D8BA] != 0. */
        u16 ps = static_cast<u16>(vert_dir_cache & 1);
        /* if (DS:0xD8BA != 0) ps += 4; */
        pending_state_id    = ps;
        ai_result          |= 6;
        return;
    }

    case 1: {  /* state == 2: TICK (same body as state==1 from above) */
        /* В оригинале state 2 имеет ОТДЕЛЬНУЮ ветку, но почти идентичную
         * state 1 (без перезагрузки анимации). См. cs:0x11368.  */
        return;
    }

    case 2: {  /* state == 3: SUB_DISPATCH */
        /* for (i = 0; i < [DS:0xD8D8]; ++i):
         *     v = [DS:0xD8F2 + i*2] - 1   // 0..6 only
         *     switch (v) {
         *     case 0: case 1:
         *         // clamp negative vy: если obj+0x14 < 0 → сбросить
         *         //   +0x14, +0x12 (velocity Y),
         *         //   +0x1C, +0x1A (related fields) в 0
         *     case 2 .. 5: ...
         *     case 6: ...
         *     } */
        return;
    }

    case 3: {  /* state == 4: DESTROY */
        /* call cs:0xE62:0x51E — same death helper as type 2 */
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 4 — анимационный переход (file 0x12496, cs:0xFFA:0xCF6)
 *
 * Это handler, на который переходит type 1 (Карнов idle) при завершении
 * анимации.  Использует тот же helper RNG (cs:0xFFA:0x677 = file 0x12477),
 * что и type 5.  Скорее всего — «Карнов в полу-приседе» или анимация
 * подбора предмета.
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx+0xD0F].
 * Jump table (file 0x124AF, 7 entries):
 *   state 0,1 → 0x0D1D → file 0x124BD — INIT (fall-through в TICK)
 *   state 2   → 0x0D47 → file 0x124E7 — TICK
 *   state 3,5 → 0x0DA6 → file 0x12546 — RETURN
 *   state 4   → 0x0D85 → file 0x12525 — HURT
 *   state 6   → 0x0DA0 → file 0x12540 — DYING
 *
 *   INIT (0x124BD):
 *       push obj.sub_timer (+0xC5)
 *       push cs; call cs:0xFFA:0x677  // RNG/sprite helper
 *       sp += 2
 *       obj.misc_state = ax          // +0xE9 — sprite/behavior seed
 *       obj.num_bboxes = 1            // +0x1E
 *       obj.bbox[0].x_offset = 0      // +0x22
 *       obj.bbox[0].y_offset = 0      // +0x24
 *       — fall through to TICK
 *
 *   TICK (0x124E7):
 *       if ([DS:0xD8B8] == 0): jmp exit
 *       if (([DS:0xCDEA] & 8) == 0): jmp exit   // только когда frame & 8
 *       // tick всегда отрабатывает раз в 16 кадров
 *       ax = 1
 *       [bp-2] = ax
 *       ...
 * ─────────────────────────────────────────────────────────────────── */
void ai_type04() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT fall-through TICK */
        /* obj->_padE9 = rng_helper(obj.sub_timer); */
        obj->num_bboxes               = 1;
        obj->bboxes[0].x_offset       = 0;
        obj->bboxes[0].y_offset       = 0;
        [[fallthrough]];
    }
    case 2: {  /* TICK — frame-gated */
        /* if (!global_flag_D8B8) return;
         * if ((current_frame & 8) == 0) return;
         * ... */
        return;
    }
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6: {  /* DYING */
        ai_result |= 1;
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 8 — небесный спавн рядом с игроком (file 0x12B3F, cs:0xFFA:0x139F)
 *
 * LEVEL1+ враг, появляющийся в случайной точке РЯДОМ с игроком (в
 * квадрате ±32 px вокруг точки `player.x, player.y - 100`).  Использует
 * Turbo-C `_rand()` (cs:0x1CE7:0x17) дважды — раз для X, раз для Y.
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx+0x13B7].
 * Jump table (file 0x12B57, 7 entries):
 *   state 0,1 → 0x13C5 → file 0x12B65 — INIT (random spawn)
 *   state 2   → 0x1429 → file 0x12BC9 — TICK
 *   state 3,5 → 0x155B → file 0x12CFB — RETURN
 *   state 4   → 0x1547 → file 0x12CE7 — HURT
 *   state 6   → 0x1556 → file 0x12CF6 — DYING
 *
 *   INIT (0x12B65):
 *       obj.obj_type   = 1                     // +0xE5 (subtype = 1)
 *       obj.flag_2E    = 0                     // +0x2E (НЕВИДИМ сначала)
 *       obj.num_bboxes = 1                     // +0x1E
 *
 *       ax = rand_word()                       // cs:0x1CE7:0x17
 *       ax &= 0x3F                              // 0..63
 *       ax += primary_player.x_world
 *       ax += 0xFFF6                            // = -10 (sign-extended)
 *       obj.x_world = ax                       // = player.x + (rand & 0x3F) - 10
 *
 *       ax = primary_player.y_world
 *       ax += 0xFF9C                            // = -100
 *       push ax
 *       ax = rand_word()
 *       ax &= 0x3F
 *       dx = popped
 *       dx -= ax                                // player.y - 100 - (rand & 0x3F)
 *       obj.y_world = dx
 *
 *       obj.misc_state    = 0                  // +0xE9
 *       obj.state_list    = (ds:0x171C)        // +0xD5/+0xD7
 *       — ...continues with more setup ...
 *
 * Это объясняет ощущение «врагов, появляющихся из ниоткуда» в начальных
 * уровнях — они спавнятся в случайной точке у игрока И НЕВИДИМЫ сначала
 * (потому что flag_2E = 0).
 * ─────────────────────────────────────────────────────────────────── */
void ai_type08() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT — random spawn near player */
        obj->obj_type    = 1;
        /* obj.flag_2E = 0;  // invisible at first */
        obj->num_bboxes  = 1;

        /* X = player.x + (rand & 0x3F) - 10 */
        u16 rx = static_cast<u16>(rng_next() & 0x3F);
        obj->x_world     = static_cast<u16>(primary_player->x_world + rx - 10);

        /* Y = player.y - 100 - (rand & 0x3F) */
        u16 ry = static_cast<u16>(rng_next() & 0x3F);
        obj->y_world     = static_cast<u16>(primary_player->y_world - 100 - ry);

        /* obj.misc_state = 0; */
        obj->state_list_off = 0x171C;
        return;
    }
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6: {  /* DYING */
        ai_result |= 1;
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 5 — падающий бонусный объект (file 0x1254A, cs:0xFFA:0xDAA)
 *
 * Это объекты типа «бонус сверху»: появляются, падают по гравитации,
 * при коллизии с тайлом останавливаются. При попадании игрока — дроп
 * по таблице индексов.
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx + 0xDC1].
 * Jump table (7 entries) at file 0x12561:
 *   state 0,1 → 0x0DCF → file 0x1256F — INIT
 *   state 2   → 0x0E2B → file 0x125CB — TICK (gravity + collision)
 *   state 3,5 → 0x0EB6 → file 0x12656 — RETURN (no-op)
 *   state 4   → 0x0E81 → file 0x12621 — HURT (player took it)
 *   state 6   → 0x0EB0 → file 0x12650 — DYING
 *
 *   INIT (file 0x1256F):
 *       push obj.sub_timer                          // +0xC5 = drop index seed
 *       push cs; call cs:0xFFA:0x677 (file 0x12477)  // compute_drop_seed
 *       sp += 2
 *       obj.misc_state = ax                         // +0xE9 — индекс предмета
 *       obj.behavior_phase = 1                       // +0xED — нормальный режим
 *       obj.num_bboxes = 1                           // +0x1E
 *       obj.bbox[0].sprite_id = obj.misc_state + 0x27  // +0x20 — sprite #39..#50
 *       obj.bbox[0].x_offset = 0                     // +0x22
 *       obj.bbox[0].y_offset = 0                     // +0x24
 *       obj.flag_2E = 1                              // +0x2E
 *       obj.vy_hi = 2                                // +0x14 — гравитация = 2 px/frame
 *       obj.vy_lo = 0                                // +0x12
 *       call cs:0x6E:0x59A(obj)                     // apply pos to bbox
 *       — fall through to TICK
 *
 *   TICK (file 0x125CB):
 *       if (obj.behavior_phase == 0) return         // уже на полу
 *       ; Coll-check: ask map at (obj.x, obj.y + 10) what tile is there
 *       ax = obj+0x28 + 10
 *       push ax
 *       push obj.x_world (+0x8)
 *       call cs:0xD4B:0x5A                           // tile_at_xy
 *       sp += 4
 *       bx = ax * 2
 *       if ([bx + 0x84] & 1) == 0: return            // не «твёрдый» тайл
 *       ; HIT GROUND:
 *       push 0; call cs:0xD4B:0x8D(0)                // anim_for_direction(NONE)
 *       sp += 2
 *       push obj; call cs:0x6E:0x52B(obj)            // finalize state
 *       sp += 4
 *       obj.behavior_phase = 0                       // → застыл
 *       return
 *
 *   HURT (file 0x12621):
 *       call play_sound(5)                           // звук подбора
 *       push obj; call cs:0x6E:0x4B4(obj)            // enemy_unlink_and_think
 *       sp += 4
 *       push cs; call cs:0xFFA:0x1EC4                 // local helper
 *       push obj.misc_state                           // +0xE9 — индекс bonus
 *       call cs:0x41D:0x9BE                           // award_item_at_index
 *       sp += 2
 *       — fall through to DYING
 *
 *   DYING (file 0x12650):
 *       ai_result |= 1                                // suspend
 *       return
 * ─────────────────────────────────────────────────────────────────── */
void ai_type05() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {

    case 0:
    case 1: {  /* INIT — fall-through в TICK */
        /* obj.misc_state = compute_drop_seed(obj.sub_timer);  // +0xE9 */
        /* obj.behavior_phase = 1;     // +0xED */
        obj->num_bboxes               = 1;
        /* obj.bbox[0].sprite_id = misc_state + 0x27;  // 0x20 */
        obj->bboxes[0].sprite_id      = 0x27;   /* placeholder */
        obj->bboxes[0].x_offset       = 0;
        obj->bboxes[0].y_offset       = 0;
        /* obj.flag_2E = 1; */
        /* obj.vy = (2 << 16); — 2 px/frame в fixed-point */
        /* call cs:0x6E:0x59A(obj); */
        [[fallthrough]];
    }

    case 2: {  /* TICK */
        /* if (obj.behavior_phase == 0) return; */
        /* tile_id = tile_at_xy(obj.x, obj.y + 10);
         * if (!(tile_attrs[tile_id] & 1)) return;
         * — solid tile reached:
         * anim_for_direction(0);
         * finalize_state(obj);
         * obj.behavior_phase = 0; */
        return;
    }

    case 3:
    case 5:
        return;

    case 4: {  /* HURT — игрок подобрал */
        play_sound(5);
        /* enemy_unlink_and_think(obj); */
        /* award_item_at_index(obj.misc_state); */
        [[fallthrough]];
    }

    case 6: {  /* DYING */
        ai_result |= 1;
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 7 — невидимый трекер/триггер (file 0x127D2, cs:0xFFA:0x1032)
 *
 * Объект сохраняет себя в глобальной паре DS:0xD94F / DS:0xD951
 * («активный trigger»), невидим, отслеживает игрока. Похоже на
 * скрытый чек-пойнт или триггер закрытия двери.
 *
 * Диспетчер: cmp ax, 5; jna; jmp [cs:bx + 0x104C].
 * Jump table (6 entries) at file 0x127EC:
 *   state 0,1 → 0x1058 → file 0x127F8  — INIT
 *   state 2   → 0x109D → file 0x1283D  — TICK (sub-dispatch by misc_state)
 *   state 3,4 → 0x133C → file 0x12ADC  — RETURN
 *   state 5   → 0x12C1 → file 0x12A61  — END
 *
 *   INIT (file 0x127F8):
 *       obj.num_bboxes = 0                          // невидим
 *       obj.bbox[0].sprite_id = 0x90                 // 144 (на случай показа)
 *       obj.bbox[0].x_offset, y_offset = 0
 *       obj.flag_2E = 0                              // не отрисовывать
 *       obj.misc_state = 0                           // +0xE9
 *       obj.behavior_phase = 0                       // +0xED
 *       [DS:0xD94F] = obj.offset                     // запомнить как «текущий trigger»
 *       [DS:0xD951] = obj.segment
 *       call cs:0x6E:0x59A(obj)
 *       — fall through to TICK
 *
 *   TICK (file 0x1283D) с вложенным диспетчером по misc_state:
 *       ax = obj.misc_state
 *       if (ax > 3) return
 *       jmp [cs:bx + 0x10B7]    // sub-table 4 entries (file 0x12857)
 *           0 → 0x10BF → file 0x1285F — phase 0: tracking player
 *           1 → 0x11D3 → file 0x12973 — phase 1: ???
 *           2 → 0x133C → file 0x12ADC — phase 2: return
 *           3 → 0x1298 → file 0x12C38 — phase 3: ???
 *
 *   phase 0 (file 0x1285F) — отслеживание игрока:
 *       if (obj.behavior_phase == 0) return
 *       ; compare positions:
 *       les bx, [DS:0xD95F]                          // primary_player
 *       ax = player.x_world
 *       les bx, [DS:0xD8A6]
 *       ax -= obj.x_world
 *       if (ax > 0): use ax  else: use -ax            // abs distance
 *       … условия активации триггера (вероятно «в радиусе») …
 *
 *   END (file 0x12A61):
 *       — очистка регистров и возврат с suspend.
 *
 * Этот тип особенно интересен тем, что у него ВЛОЖЕННАЯ state-машина:
 * внешняя по state_byte и внутренняя по misc_state. Это позволяет
 * объекту иметь до 4 «фаз» внутри состояния «активен» (state==2).
 * ─────────────────────────────────────────────────────────────────── */
void ai_type07() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 5) return;

    switch (current_state_byte) {

    case 0:
    case 1: {  /* INIT + fall-through TICK */
        obj->num_bboxes               = 0;          /* невидим */
        obj->bboxes[0].sprite_id      = 0x90;        /* 144 */
        obj->bboxes[0].x_offset       = 0;
        obj->bboxes[0].y_offset       = 0;
        /* obj.flag_2E = 0; */
        /* obj.misc_state = 0;       // +0xE9 */
        /* obj.behavior_phase = 0;   // +0xED */
        /* [DS:0xD94F]:[DS:0xD951] = obj far ptr (как "active trigger") */
        /* call cs:0x6E:0x59A(obj); */
        [[fallthrough]];
    }

    case 2: {  /* TICK с sub-диспетчером */
        /* u16 sub_state = obj.misc_state;
         * if (sub_state > 3) return; */
        u16 sub_state = 0;  /* placeholder */
        switch (sub_state) {

        case 0: {  /* phase 0: tracking player */
            /* if (obj.behavior_phase == 0) return; */
            if (!primary_player) return;
            s16 dx_p = static_cast<s16>(primary_player->x_world) -
                       static_cast<s16>(obj->x_world);
            u16 abs_dx = static_cast<u16>(dx_p < 0 ? -dx_p : dx_p);
            (void)abs_dx;
            /* if (abs_dx < ACTIVATION_RADIUS) {
             *     trigger fires...
             * } */
            return;
        }
        case 1:
            /* phase 1 — file 0x12973 (детали не декомпилированы). */
            return;
        case 2:
            return;
        case 3:
            /* phase 3 — file 0x12C38 (детали не декомпилированы). */
            return;
        }
        return;
    }

    case 3:
    case 4:
        return;

    case 5: {  /* END */
        ai_result |= 1;
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 6 — рандомизированный враг (file 0x12658, cs:0xFFA:0xEB8)
 *
 * Это handler для общих врагов уровня 0 (например, "кенгуру"/"обезьяны").
 * Использует RNG для выбора одного из 11+ вариантов поведения.
 *
 *   void ai_type06() {
 *       switch (current_state_byte) {
 *       case 0,1:      goto init;
 *       case 2:        goto tick;
 *       case 3,5:      goto exit_state;
 *       case 4:        goto state4;
 *       case 6:        goto exit;
 *       }
 *
 *   init (0x1267E):
 *       call rng_subroutine(seed = level-related)   // cs:0xE62:0x877
 *       obj.misc_state = ax                          // +0xE9
 *       obj.sub_timer_max = 100                      // +0xC9
 *       if (obj.misc_state < 11) {
 *           obj.num_bboxes = 1
 *           obj.bbox[0].sprite_id = obj.misc_state + 39
 *           obj.bbox[0].x_offset  = 0
 *           ax = obj.y_world + [DS:0xFD3]
 *           obj.bbox[0].y_offset = -(ax % 10)
 *           obj.flag_2E        = 1
 *           obj.behavior_phase = 0   // +0xED
 *       } else {
 *           obj.num_bboxes     = 0       // invisible
 *           obj.behavior_phase = 1
 *           obj.alt_sub_phase  = 0       // +0xEF
 *       }
 *       complete_init(obj)                            // cs:0x6E:0x52B
 *       si = rng_next()
 *       if (obj.behavior_phase == 0) return
 *       if (obj.alt_sub_phase != 0 && si > 40) {
 *           // Активировать вторую фазу
 *           ...
 *       }
 *       return
 *
 *   tick: похожая структура — увеличивает sub_timer, проверяет анимацию,
 *         при последнем кадре передаёт управление через queue_state.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type06() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    /* Все ветки в одном switch.  В оригинале — jump table cs:0xED0,
     * содержащая 7 word'ов (один на state 0..6). */
    switch (current_state_byte) {

    case 0:
    case 1: {
        /* init (file 0x1267E): */
        u16 misc_state = static_cast<u16>(rng_next() % 16);  /* +0xE9 */
        /* В оригинале +0xE9 — это WORD внутри _padE9[12], но точное
         * смещение требует более тонкой разметки. */
        obj->sub_timer_max = 100;
        if (misc_state < 11) {
            obj->num_bboxes               = 1;
            obj->bboxes[0].sprite_id      = static_cast<u16>(misc_state + 39);
            obj->bboxes[0].x_offset       = 0;
            u16 v = (obj->y_world + /*DS:0xFD3*/ 0) % 10;
            obj->bboxes[0].y_offset       = static_cast<u16>(-static_cast<s16>(v));
            /* obj.flag_2E = 1;   // +0x2E */
            /* obj.behavior_phase = 0;  // +0xED */
        } else {
            obj->num_bboxes = 0;
            /* obj.behavior_phase = 1;  // +0xED
             * obj.alt_sub_phase  = 0;  // +0xEF */
        }
        /* call cs:0x6E:0x52B(obj)  — финализация init */
        /* u16 r = rng_next();
         * if (behavior_phase == 0) return;
         * if (alt_sub_phase != 0 && r <= 40) { ... } */
        return;
    }

    case 2:
        /* tick — играет анимацию, при последнем кадре queue_state. */
        if (obj->state_ext == 6) {
            pending_state_id = 0;
            ai_result |= 6;
        }
        if (obj->active_flag != 0) {
            if (obj->sub_timer + 1 >= anim_frames(obj)) {
                queue_state(obj, nullptr /* next state */);
            }
            ai_result |= 4;
        }
        return;

    case 3:
    case 5:
        /* Промежуточные состояния — выход или ожидание. */
        return;

    case 4:
        /* "Hurt": pair-loop type-0 ставит +0xB6 = 4 при коллизии. */
        /* TODO: cs:0xFFA:0xFE2 — обработка получения урона */
        return;

    case 6:
        /* "Dying" — после прощёлкивания анимации смерти, suspend. */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 9 — двух-спрайтный «boss-like» враг (file 0x14BA4, cs:0x11D6:0x1644)
 *
 * Уникальная особенность: num_bboxes = 2, два спрайта расположены рядом
 * по горизонтали (offset = 5 px). Это позволяет создать «составного»
 * врага (например, два соединённых сегмента).
 *
 * Диспетчер: cmp ax, 6; ja (return); jmp [cs:bx+0x1658].
 *   state 0,1 → 0x1666 → file 0x14BC6 — INIT
 *   state 2,4 → 0x16B2 → file 0x14C12 — TICK (call helper)
 *   state 3,5,6 → return
 *   state 6+  → suspend
 *
 *   INIT (file 0x14BC6):
 *       obj.active_flag        = 0          // +0xDB
 *       obj.num_bboxes         = 2          // +0x1E  *** 2 спрайта! ***
 *       obj.bbox[0].sprite_id  = 0x91       // +0x20 — основной (=145)
 *       obj.bbox[0].x_offset   = 0          // +0x22
 *       obj.bbox[0].y_offset   = 0          // +0x24
 *       obj.bbox[1].sprite_id  = 0x92       // +0x32 — второй (=146)
 *       obj.bbox[1].x_offset   = 5          // +0x34 — справа на 5 пикселей
 *       obj.bbox[1].y_offset   = [DS:0x103D] // +0x36 — берётся из таблицы
 *       obj.sub_timer_max      = 1          // +0xC9 — тик каждый кадр
 *       call cs:0x6E:0x52B(obj)             // apply pos to both bboxes
 *       — fall through to TICK
 *
 *   TICK (file 0x14C12):
 *       call cs:0x11D6:0x4B36                // type-9 specific helper
 *                                            // (file 0x14B36 — функция
 *                                            //  ПЕРЕД handler'ом в коде)
 *       return
 *
 *   HURT/DESTROY (file 0x14C18):
 *       ai_result |= 1                       // suspend → removed
 * ─────────────────────────────────────────────────────────────────── */
void ai_type09() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {

    case 0:
    case 1: {  /* INIT — fall-through в TICK */
        obj->active_flag         = 0;
        obj->num_bboxes          = 2;           /* TWO sprite-боксов! */
        obj->bboxes[0].sprite_id = 0x91;        /* primary */
        obj->bboxes[0].x_offset  = 0;
        obj->bboxes[0].y_offset  = 0;
        obj->bboxes[1].sprite_id = 0x92;        /* attached */
        obj->bboxes[1].x_offset  = 5;
        /* obj.bboxes[1].y_offset = sprite_y_offset_table[0x103D]; */
        obj->bboxes[1].y_offset  = 0;
        obj->sub_timer_max       = 1;
        /* call cs:0x6E:0x52B(obj); */
        [[fallthrough]];
    }

    case 2:
    case 4: {  /* TICK / hurt-tick */
        /* call cs:0x11D6:0x4B36 — type-9-specific behavior helper.
         * Эта функция находится в коде ПЕРЕД handler'ом (file 0x14B36),
         * и вероятно содержит движение/AI для составного врага. */
        return;
    }

    case 3:
    case 5:
        return;

    case 6:
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 10 — горизонтальный маятник (file 0x13973, cs:0x11D6:0x0413)
 *
 * Движется горизонтально по фиксированной траектории, при достижении
 * предельной точки flip'ит направление (через NEG двойной точности).
 * Базовая скорость: vx = 0xFFFF:0x1000 = -0xF000 (медленно влево).
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx+0x42A].
 *   state 0,1 → 0x0438 → file 0x13998 — INIT (fall-through в TICK)
 *   state 2   → 0x0488 → file 0x139E8 — TICK (flip-direction logic)
 *   state 3,5 → 0x04C8 → file 0x13A28 — RETURN
 *   state 4   → 0x04B4 → file 0x13A14 — HURT (death FX)
 *   state 6   → 0x04C3 → file 0x13A23 — DYING
 *
 *   INIT (file 0x13998):
 *       obj.state_list = (ds:0x180A)         // +0xD5/+0xD7
 *       pending_state_id = 0
 *       if (obj.sub_timer >= 0x0A):           // +0xC5
 *           target = 0xFFFF:0x6000            // backward, distance 0x6000
 *       else:
 *           target = 0x0000:0xA000            // forward, distance 0xA000
 *       obj+0x1C = target.hi
 *       obj+0x1A = target.lo
 *       obj+0x10 = 0xFFFF                     // vx_hi = -1
 *       obj+0x0E = 0x1000                     // vx_lo = 0x1000
 *       ai_result |= 6                        // suspend + transition
 *       obj.state_ext = 8                     // +0xC7 = 8
 *
 *   TICK (file 0x139E8):
 *       if ((obj.state_ext & 0x0F) != 0):
 *           goto exit                          // ещё не достигли target
 *       ; иначе FLIP: NEG dx:ax
 *       dx:ax = -(obj+0x1C:obj+0x1A)           // двойной отриц.
 *       obj+0x1C = dx
 *       obj+0x1A = ax
 *     exit:
 *       ai_result |= 4
 *       return
 *
 *   HURT (file 0x13A14):
 *       call cs:0x41D:0x4D7(0x00000110)       // FX system: distance=272?
 *       — fall through to DYING
 *
 *   DYING (file 0x13A23):
 *       call cs:0xD4B:0x4A5                    // death/explode
 *       return
 * ─────────────────────────────────────────────────────────────────── */
void ai_type10() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {

    case 0:
    case 1: {  /* INIT + fall-through TICK */
        obj->state_list_off = 0x180A;
        pending_state_id    = 0;
        /* if (obj.sub_timer >= 10) target = -0x6000 else target = +0xA000 */
        /* obj+0x10:0x0E = -0xF000 (slow leftward velocity) */
        ai_result      |= 6;
        obj->state_ext  = 8;
        [[fallthrough]];
    }

    case 2: {  /* TICK */
        if ((obj->state_ext & 0x0F) == 0) {
            /* FLIP direction: NEG (obj+0x1C : obj+0x1A) */
            /* TODO: NEG двойной точности */
        }
        ai_result |= 4;
        return;
    }

    case 3:
    case 5:
        return;

    case 4: {  /* HURT */
        /* call cs:0x41D:0x4D7(0x110) — FX distance/sound */
        [[fallthrough]];
    }

    case 6: {  /* DYING */
        /* call cs:0xD4B:0x4A5 — death helper */
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 11 — range-based трекер (file 0x13A2A, cs:0x11D6:0x04CA)
 *
 * Вычисляет |dx| = |player.x − obj.x| ДО state-dispatch и хранит
 * "направление к игроку" в [bp-2]. Имеет ВЛОЖЕННЫЙ диспетчер по
 * behavior_phase (+0xED) внутри state 2 с порогом 10 пикселей.
 *
 * Pre-dispatch (file 0x13A31..0x13A5B):
 *   si = player.x_world - obj.x_world          // signed
 *   di = abs(si)                                // distance
 *   dir = (si > 0) ? 1 : 0                       // 1 = вправо
 *   [bp-2] = dir
 *   pending_state_id = dir
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx+0x512].
 *   state 0   → 0x0520 → file 0x13A80 — INIT (spawn FX)
 *   state 1   → 0x0540 → file 0x13AA0 — REINIT (state list setup)
 *   state 2   → 0x0564 → file 0x13AC4 — TICK (inner switch by phase)
 *   state 3,5 → 0x0611 → file 0x13B71 — RETURN
 *   state 4   → 0x05FD → file 0x13B5D — HURT
 *   state 6   → 0x060C → file 0x13B6C — DYING
 *
 *   INIT (state 0, file 0x13A80):
 *       call cs:0xD4B:0x19F(obj, sprite=0x4E=78, 0)   // spawn animation
 *       ai_result |= 0x10                              // bit 4 (rare signal)
 *
 *   REINIT (state 1, file 0x13AA0):
 *       obj.sub_timer_max = 2                          // +0xC9
 *       obj.behavior_phase = 1                         // +0xED = 1 (start in phase 1)
 *       obj.state_list = (ds:0x1856)                   // +0xD5/+0xD7
 *       ai_result |= 6                                  // transition
 *
 *   TICK (state 2, file 0x13AC4) с inner dispatcher по +0xED:
 *       ax = obj.behavior_phase
 *       if (ax > 1) return
 *       jmp [cs:bx + 0x57E]:
 *           [0] → 0x582 → file 0x13AE2 — phase 0: «далеко»
 *           [1] → 0x5A9 → file 0x13B09 — phase 1: «близко»
 *
 *       phase 0 (далеко, file 0x13AE2):
 *           if (|di| >= 10): return                    // нет смены поведения
 *           obj.behavior_phase = 1
 *           call cs:0xD4B:0x8D(0)                       // anim_for_direction(0)
 *           pending_state_id += 2
 *           ai_result |= 2                              // transition
 *
 *       phase 1 (близко, file 0x13B09):
 *           if (|di| <= 10): return                    // нет смены
 *           obj.behavior_phase = 0
 *           if ([bp-2] != 0):  ax = 1                    // → пинг вправо
 *           else:              ax = 2                    // → пинг влево
 *           push ax; call cs:0xD4B:0x???                // anim helper
 *           ...
 *
 *   HURT (state 4, file 0x13B5D):
 *       call cs:0x41D:0x4D7(0x1400)                    // bigger FX (5120)
 *       — fall through to DYING
 *
 *   DYING (state 6, file 0x13B6C):
 *       call cs:0xD4B:0x4A5                            // death helper
 * ─────────────────────────────────────────────────────────────────── */
void ai_type11() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;

    /* Pre-dispatch player-direction logic. */
    s16 si_dx = static_cast<s16>(primary_player->x_world) -
                static_cast<s16>(obj->x_world);
    u16 abs_dx = static_cast<u16>(si_dx < 0 ? -si_dx : si_dx);
    u16 dir    = (si_dx > 0) ? 1 : 0;
    pending_state_id = dir;

    if (current_state_byte > 6) return;

    switch (current_state_byte) {

    case 0: {  /* INIT: spawn FX */
        /* call cs:0xD4B:0x19F(obj, 0x4E, 0); */
        ai_result |= 0x10;
        return;
    }

    case 1: {  /* REINIT */
        obj->sub_timer_max  = 2;
        /* obj.behavior_phase = 1; */
        obj->state_list_off = 0x1856;
        ai_result          |= 6;
        return;
    }

    case 2: {  /* TICK с inner dispatch */
        u16 phase = 0;     /* obj->behavior_phase  -- +0xED */
        if (phase > 1) return;
        switch (phase) {
        case 0:    /* далеко */
            if (abs_dx >= 10) return;
            /* obj.behavior_phase = 1; */
            /* anim_for_direction(0); */
            pending_state_id += 2;
            ai_result        |= 2;
            return;
        case 1:    /* близко */
            if (abs_dx <= 10) return;
            /* obj.behavior_phase = 0; */
            /* if (dir != 0) ax = 1; else ax = 2; */
            /* call anim_helper(ax); */
            return;
        }
        return;
    }

    case 3:
    case 5:
        return;

    case 4: {  /* HURT */
        /* call cs:0x41D:0x4D7(0x1400); */
        [[fallthrough]];
    }

    case 6: {  /* DYING */
        /* call cs:0xD4B:0x4A5; */
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 12 — наземный патрульный (file 0x13E13, cs:0x11D6:0x08B3)
 *
 * Простой враг, ходящий по земле с фиксированной скоростью ±3.
 * Имеет HP=20 (+0xE7=0x14) — самый «жирный» среди декомпилированных
 * базовых врагов.
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx+0x8CE].
 *   state 0,1 → 0x08DC → file 0x13E3C — INIT
 *   state 2   → 0x09A4 → file 0x13F04 — TICK
 *   state 3,5 → 0x0AC5 → file 0x14025 — RETURN
 *   state 4   → 0x0AB1 → file 0x14011 — HURT
 *   state 6   → 0x0AC0 → file 0x14020 — DYING
 *
 *   INIT (file 0x13E3C):
 *       ax = obj.sub_timer_max                 // +0xC3 (initial val)
 *       obj.behavior_phase = ax                // +0xED
 *       obj.misc_state     = 0                 // +0xE9
 *       obj.obj_type       = 1                 // +0xE5  (subtype = 1)
 *       obj.hit_points     = 0x14 = 20          // +0xE7
 *       obj.flag_2E        = 1                 // +0x2E
 *       obj.num_bboxes     = 1                 // +0x1E
 *
 *       if (obj.behavior_phase == 0):           // фаза 0 — снос Y
 *           obj.y_world -= [DS:0xFC1]           // подкорректировать Y
 *
 *           if (obj.sub_timer != 0):            // +0xC5
 *               dx = 3;  ax = 0                 // vx = +3 (вправо)
 *           else:
 *               dx = -3; ax = 0                 // vx = -3 (влево)
 *           obj+0x10 = dx                       // vx_hi
 *           ...
 *
 *   HURT (file 0x14011):
 *       call cs:0x41D:0x4D7(0x1000)            // FX, 4096
 *       call cs:0xD4B:0x4FF                    // death FX (другой чем 0x4A5!)
 *
 * Замечание: HURT использует cs:0xD4B:0x4FF, а не 0x4A5 (как type 10/11).
 * Это указывает на разные «классы» смертельных эффектов.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type12() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {

    case 0:
    case 1: {  /* INIT */
        /* obj.behavior_phase = obj.sub_timer_max;  // +0xED = +0xC3 */
        /* obj.misc_state = 0; */
        obj->obj_type    = 1;
        obj->hit_points  = 0x14;       /* 20 HP */
        /* obj.flag_2E = 1; */
        obj->num_bboxes  = 1;

        /* if (behavior_phase == 0):
         *     obj.y_world -= [DS:0xFC1];
         *     vx = (sub_timer != 0) ? +3 : -3; */
        return;
    }

    case 2: {  /* TICK — file 0x13F04 (детали не декомпилированы) */
        return;
    }

    case 3:
    case 5:
        return;

    case 4: {  /* HURT */
        /* call cs:0x41D:0x4D7(0x1000); */
        [[fallthrough]];
    }

    case 6: {  /* DYING */
        /* call cs:0xD4B:0x4FF;  -- ДРУГОЙ helper чем у type 10/11 */
        return;
    }
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 13 — Y-snap трекер (file 0x13C08, cs:0x11D6:0x06A8)
 *
 * Pre-dispatch:
 *   di = player.x_world - obj.x_world
 *   si = (di < 0) ? 0 : 1                     // направление к игроку
 *   pending_state_id = si
 *
 * Jump table (cs:0x11D6:0x6E5, file 0x13C43, 7 entries):
 *   state 0 → 0x13C51 — Y-snap + FX
 *   state 1 → 0x13C67 — main INIT
 *   state 2 → 0x13CA8 — TICK
 *   state 3 → 0x13E0D — RETURN
 *   state 4 → 0x13DF9 — HURT
 *   state 5 → 0x13DDC — HURT-FX
 *   state 6 → 0x13E08 — DYING
 *
 *   state 0 (0x13C51): Y-snap + ai_result |= 0x10
 *       obj.y_world -= [DS:0x1002]              // визуальная коррекция
 *       ai_result |= 0x10                        // spawn FX bit
 *
 *   state 1 (0x13C67): main INIT
 *       obj.sub_timer_max = 4                    // +0xC9
 *       obj.state_list    = (ds:0x18CC)          // +0xD5/+0xD7
 *       if (obj.sub_timer >= 10):  ax = 1 else: ax = 0   // sprite variant
 *       ...                                       // (детали не декомпилированы)
 * ─────────────────────────────────────────────────────────────────── */
void ai_type13() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;

    s16 di_dx = static_cast<s16>(primary_player->x_world) -
                static_cast<s16>(obj->x_world);
    u16 dir = (di_dx < 0) ? 0 : 1;
    pending_state_id = dir;

    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:  /* Y-snap + spawn FX */
        /* obj->y_world -= sprite_y_offset_table[0x1002]; */
        ai_result |= 0x10;
        return;
    case 1:  /* main INIT */
        obj->sub_timer_max  = 4;
        obj->state_list_off = 0x18CC;
        /* sprite variant based on sub_timer >= 10 */
        return;
    case 2:  /* TICK */
        /* file 0x13CA8 — детали не декомпилированы */
        return;
    case 3:
        return;
    case 4:  /* HURT */
        /* file 0x13DF9 */
        return;
    case 5:  /* HURT-FX */
        /* file 0x13DDC */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 14 — спавн со спрайтом 0x95 (file 0x14368, cs:0x11D6:0x0E08)
 *
 * Pre-dispatch (player-tracking): di = player.x - obj.x; читает таблицы
 * по [DS:0x100E] и [DS:0x1042] для базовых смещений.
 *
 * Jump table (cs:0x11D6:0xE60, file 0x143D4):
 *   state 0 → 0x143E2 — spawn FX (sprite 0x95)
 *   state 1 → 0x14402 — main INIT (state_list 0x1958)
 *   state 2 → 0x1445D — TICK
 *   state 3,5 → 0x1458D — RETURN
 *   state 4 → 0x14579 — HURT
 *   state 6 → 0x14588 — DYING
 *
 *   state 0 (0x143E2): spawn(obj, sprite=0x95, 0) + ai_result |= 0x10
 *   state 1 (0x14402): state_list 0x1958, misc_state=2, obj+0xEB = si
 * ─────────────────────────────────────────────────────────────────── */
void ai_type14() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;

    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0:  /* spawn FX */
        /* spawn_helper(obj, sprite=0x95, 0); */
        ai_result |= 0x10;
        return;
    case 1:  /* INIT */
        obj->state_list_off = 0x1958;
        /* obj.misc_state = 2;          // +0xE9 */
        /* obj+0xEB = direction_si; */
        return;
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 15 — anchor-based AI, LEVEL3 (file 0x1402B, cs:0x11D6:0x0ACB)
 *
 * Pre-dispatch вычисляет анкер-точку через две константы из DS:
 *   anchor_x = player.x + [DS:0x100E]            // = player_cache_x + dx_off
 *   anchor_y = player.y + [DS:0x1045] - obj.y    // = разность к anchor
 *   [bp-2] = anchor_y                             // знаковая дистанция
 *
 * Jump table (cs:0x11D6:0xB1D, file 0x14080):
 *   state 0,1 → 0x1408E — INIT (state_list 0x1918)
 *   state 2   → 0x140C6 — TICK
 *   state 3   → 0x14362 — RETURN
 *   state 4   → 0x1434E — HURT
 *   state 5   → 0x14339 — HURT-FX
 *   state 6   → 0x1435D — DYING
 *
 *   INIT (0x1408E):
 *       obj.sub_timer_max  = 3                   // +0xC9
 *       obj.state_list_off = 0x1918              // +0xD5
 *       ax = si + 2                              // (si from pre-dispatch)
 *       pending_state_id = ax                    // 2 или 3
 *       ai_result |= 6                            // transition
 *       obj.misc_state = 0                        // +0xE9
 *       obj+0xEB = obj.x_world                    // запомнить начальный X
 * ─────────────────────────────────────────────────────────────────── */
void ai_type15() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1:  /* INIT */
        obj->sub_timer_max  = 3;
        obj->state_list_off = 0x1918;
        pending_state_id    = 2;          /* si + 2 */
        ai_result          |= 6;
        /* obj+0xEB = obj.x_world; */
        return;
    case 2:  /* TICK */
    case 3:
        return;
    case 4:  /* HURT */
    case 5:  /* HURT-FX */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 16 — горизонтальный снаряд (file 0x14593, cs:0x11D6:0x1033)
 *
 * Pre-dispatch (simpler): ax = player.x - obj.x; [bp-2]=ax; si=(ax>0?1:0);
 *                          pending_state_id = si.
 *
 * Jump table (cs:0x11D6:0x107C, file 0x145D0):
 *   state 0 → 0x145DE — spawn FX (ai_result |= 0x10)
 *   state 1 → 0x145E7 — main INIT (vx ≈ -1.5 px/frame)
 *   state 2 → 0x14625 — TICK
 *   state 3,5 → 0x147D7 — RETURN
 *   state 4 → 0x147C3 — HURT
 *   state 6 → 0x147D2 — DYING
 *
 *   state 1 INIT (0x145E7):
 *       obj.sub_timer_max  = 3                   // +0xC9
 *       obj+0x10 = 0xFFFE                        // vx_hi = -2 (signed)
 *       obj+0x0E = 0x8000                        // vx_lo = 0x8000
 *       // → 32-bit signed velocity = 0xFFFE8000 ≈ -1.5 px/frame
 *       obj.misc_state = 0x0D = 13                // +0xE9 (sprite param)
 *       obj+0xEB = 0                             // +0xEB
 *       obj.state_list_off = 0x1984              // +0xD5
 * ─────────────────────────────────────────────────────────────────── */
void ai_type16() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;

    s16 dx = static_cast<s16>(primary_player->x_world) -
             static_cast<s16>(obj->x_world);
    u16 dir = (dx > 0) ? 1 : 0;
    pending_state_id = dir;

    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0:  /* spawn FX */
        ai_result |= 0x10;
        return;
    case 1:  /* INIT */
        obj->sub_timer_max  = 3;
        /* obj+0x10:0x0E = 0xFFFE:0x8000  (-1.5 px/frame fixed-point) */
        /* obj.misc_state = 0x0D; */
        /* obj+0xEB = 0; */
        obj->state_list_off = 0x1984;
        return;
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 17 — стандартный спавн (file 0x13B77, cs:0x11D6:0x0617)
 *
 * Простейший шаблон БЕЗ player-tracking.
 *
 * Jump table (cs:0x11D6:0x628, file 0x13B8E):
 *   state 0 → 0x13B9C — spawn(sprite=0xA1)
 *   state 1 → 0x13BBB — main INIT (state_list 0x1880)
 *   state 2 → 0x13BEA — TICK
 *   state 3,5 → 0x13C06 — RETURN
 *   state 4 → 0x13BF2 — HURT
 *   state 6 → 0x13C01 — DYING
 *
 *   state 0 (0x13B9C):
 *       spawn_helper(obj, sprite=0xA1=161, 0)    // cs:0xD4B:0x19F
 *       ai_result |= 0x10
 *   state 1 (0x13BBB):
 *       obj.sub_timer_max  = 2                    // +0xC9
 *       obj.state_list_off = 0x1880               // +0xD5
 *       pending_state_id   = 0
 * ─────────────────────────────────────────────────────────────────── */
void ai_type17() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:  /* spawn FX (sprite 0xA1) */
        /* spawn_helper(obj, 0xA1, 0); */
        ai_result |= 0x10;
        return;
    case 1:  /* INIT */
        obj->sub_timer_max  = 2;
        obj->state_list_off = 0x1880;
        pending_state_id    = 0;
        return;
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 18 — Y-snap к игроку (file 0x147DD, cs:0x11D6:0x127D)
 *
 * Pre-dispatch (player-tracking): di = player.x - obj.x; si = (di<=0?0:1).
 *
 * Jump table (cs:0x11D6:0x12BA, file 0x14814):
 *   state 0,1 → 0x14822 — INIT (Y-snap)
 *   state 2   → 0x148B1 — TICK
 *   state 3,5 → 0x14B30 — RETURN
 *   state 4   → 0x14B1C — HURT
 *   state 6   → 0x14B2B — DYING
 *
 *   INIT (0x14822):
 *       obj.alt_sub_phase  = 0                    // +0xEF
 *       obj.behavior_phase = obj.sub_timer        // +0xED ← +0xC5
 *       les bx, [DS:0xD95F]                       // primary_player
 *       ax = player.y_world
 *       les bx, [DS:0xD8A6]
 *       obj+0xEB = ax                             // запомнить y_player
 *       obj.sub_timer_max  = 3                    // +0xC9
 *       obj.misc_state     = 2                    // +0xE9
 *       if (si != 0): dx = 1 else: ...            // direction
 * ─────────────────────────────────────────────────────────────────── */
void ai_type18() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;

    s16 dx = static_cast<s16>(primary_player->x_world) -
             static_cast<s16>(obj->x_world);
    u16 dir = (dx <= 0) ? 0 : 1;
    (void)dir;

    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT — Y-snap */
        /* obj.alt_sub_phase = 0;       // +0xEF */
        /* obj.behavior_phase = obj.sub_timer; */
        /* obj+0xEB = primary_player->y_world; */
        obj->sub_timer_max  = 3;
        /* obj.misc_state = 2; */
        return;
    }
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 19 — медленный AI с helper'ом 0x4B36 (file 0x14C20, cs:0x11D6:0x16C0)
 *
 * НЕ player-tracking. Очень медленный (sub_timer_max=0x3E8=1000).
 * Делегирует TICK к helper'у `cs:0x11D6:0x4B36` — ТАКОМУ ЖЕ, как у type 9.
 *
 * Jump table (cs:0x11D6:0x16D4, file 0x14C34):
 *   state 0,1 → 0x14C42 — INIT
 *   state 2   → 0x14C79 — TICK (call helper 0x4B36)
 *   state 3,5 → 0x14C85 — RETURN
 *   state 4   → 0x14C7F — HURT
 *   state 6   → 0x14C85 — DYING
 *
 *   INIT (0x14C42):
 *       obj.active_flag         = 0              // +0xDB
 *       obj.num_bboxes          = 1              // +0x1E
 *       obj.bbox[0].sprite_id   = 0xAC = 172     // +0x20
 *       obj.bbox[0].x_offset    = 0              // +0x22
 *       obj.bbox[0].y_offset    = 0              // +0x24
 *       obj.sub_timer_max       = 0x3E8 = 1000   // +0xC9 — ОЧЕНЬ медленный
 *       call cs:0x6E:0x52B(obj)                  // apply position
 *       call cs:0x11D6:0x4B36                    // type-9/19 shared helper
 * ─────────────────────────────────────────────────────────────────── */
void ai_type19() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT — fall-through to TICK */
        obj->active_flag        = 0;
        obj->num_bboxes         = 1;
        obj->bboxes[0].sprite_id = 0xAC;
        obj->bboxes[0].x_offset  = 0;
        obj->bboxes[0].y_offset  = 0;
        obj->sub_timer_max       = 0x3E8;       /* 1000 frames */
        /* call cs:0x6E:0x52B(obj); */
        /* call cs:0x11D6:0x4B36; */
        return;
    }
    case 2:  /* TICK — same helper */
        /* call cs:0x11D6:0x4B36; */
        return;
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 20 — multi-bbox «snake» (file 0x1535D, cs:0x1348:0x06DD)  LEVEL2
 *
 * Самый сложный из 13-20: динамический num_bboxes (определяется
 * из sub_timer), 12 байт локальных переменных (sub sp, 0xC).
 *
 * Jump table (cs:0x1348:0x6F9, file 0x15379):
 *   state 0,1 → 0x15387 — INIT
 *   state 2   → 0x15429 — TICK (длинный цикл по bboxes)
 *   state 3,6 → 0x15660 — RETURN
 *   state 4   → 0x15666 — HURT
 *   state 5   → 0x155FC — HURT-FX
 *
 *   INIT (0x15387):
 *       obj.active_flag = 0                       // +0xDB
 *       obj.obj_type    = 9                       // +0xE5 ← magic = "snake"
 *       ax = obj.sub_timer                        // +0xC5
 *       obj.num_bboxes  = ax                      // +0x1E ← ДИНАМИЧЕСКИЙ!
 *
 *       // Align obj.x_world to 8-pixel grid + 2
 *       obj.x_world &= 0xFFF8                      // обнулить low 3 bits
 *       obj.x_world += 2                           // оффсет 2
 *
 *       // Init each bbox (stride 0x12 = 18 bytes)
 *       for (si = 0; si < num_bboxes; ++si):
 *           bbox_ptr = obj + 0x20 + si * 0x12
 *           // ... initialize each bbox to chain along X
 *           bbox.sprite_id = ...                   // зависит от позиции в цепи
 *           bbox.x_offset  = -si * 8               // каждый bbox на 8 px влево
 *           bbox.y_offset  = ...
 *
 * Это **многосегментный враг типа "змея"** — N bbox'ов соединены в
 * цепочку. Объект единый, но визуально — длинная анимированная линия.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type20() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT */
        obj->active_flag = 0;
        obj->obj_type    = 9;
        /* num_bboxes = sub_timer  (динамическая длина "змеи") */
        /* obj.num_bboxes = obj.sub_timer; */

        /* Выравнивание x_world по 8-px сетке. */
        obj->x_world = static_cast<u16>((obj->x_world & 0xFFF8) + 2);

        /* Per-bbox init со страйдом 18 байт. */
        for (u16 si = 0; si < obj->num_bboxes; ++si) {
            auto& bb = obj->bboxes[si];
            /* bb.sprite_id = chain_sprite[si]; */
            bb.x_offset  = static_cast<u16>(-static_cast<s16>(si * 8));
            /* bb.y_offset = chain_y_offset; */
        }
        return;
    }
    case 2:  /* TICK */
        return;
    case 3:
    case 6:
        return;
    case 4:  /* HURT */
        return;
    case 5:  /* HURT-FX */
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 21 — 7-сегментная цепь (file 0x15DD3, cs:0x1348:0x1153)
 *
 * Двоюродный брат type 20, но с ФИКСИРОВАННЫМ числом сегментов = 7.
 * Каждый bbox со страйдом 0x12 (18 байт), все sprite_id одинаковые,
 * x/y offsets создают цепочку «гусеницы».
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx+0x116F].
 * Jump table (file 0x15DEF, 7 entries):
 *   state 0,1 → 0x117D → file 0x15DFD — INIT (7 segments)
 *   state 2,3 → 0x12C9 → file 0x15F49 — RETURN/TICK
 *   state 4   → 0x1276 → file 0x15EF6 — HURT
 *   state 5   → 0x1254 → file 0x15ED4 — HURT-FX
 *   state 6   → 0x1276 → file 0x15EF6 — DYING (same as HURT)
 *
 *   INIT (0x15DFD):
 *       obj.num_bboxes  = 7              // +0x1E ← FIXED!
 *       obj.active_flag = 0              // +0xDB
 *
 *       // First bbox setup:
 *       bbox0_ptr = obj + 0x20
 *       bbox0.x_offset = 2                // +0x22
 *       bbox0.y_offset = 0xFFFD = -3      // +0x24
 *       bbox0.sprite_id = 0xB2 = 178      // +0x20
 *       bbox0.[+0xE] = 1                  // +0x2E
 *
 *       [bp-0xA] = 0x10                   // counter init
 *       bbox_ptr = obj + 0x32             // start with 2nd bbox
 *       si = 0xFFE2 = -30                  // y_offset accumulator
 *
 *       for (i = 0; i < 6; ++i):           // 6 more bboxes (всего 7)
 *           bbox_ptr.sprite_id = 0xB2     // тот же спрайт
 *           bbox_ptr.x_offset = ...
 *           bbox_ptr.y_offset = si        // увеличивается на 5 (или другую константу)
 *           bbox_ptr += 0x12
 *           si += 5  (приблизительно)
 *
 * Это **визуально длинная цепочка** — например, многозвенная многоножка
 * или каменный поезд.  HURT использует cs:0x1348:??? (state 5 = HURT-FX
 * с дополнительной анимацией).
 * ─────────────────────────────────────────────────────────────────── */
void ai_type21() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT — 7 fixed segments */
        obj->num_bboxes  = 7;              /* FIXED 7! */
        obj->active_flag = 0;

        /* First bbox */
        obj->bboxes[0].x_offset  = 2;
        obj->bboxes[0].y_offset  = static_cast<u16>(-3);
        obj->bboxes[0].sprite_id = 0xB2;

        /* Remaining 6 bboxes, y_offset increases by ~5 each */
        s16 y_off = -30;
        for (int i = 1; i < 7; ++i) {
            obj->bboxes[i].sprite_id = 0xB2;
            obj->bboxes[i].x_offset  = static_cast<u16>(2);
            obj->bboxes[i].y_offset  = static_cast<u16>(y_off);
            y_off += 5;
        }
        return;
    }
    case 2:
    case 3:
        return;
    case 4:  /* HURT */
        return;
    case 5:  /* HURT-FX */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 22 — tile-aware AI (file 0x15902, cs:0x1348:0x0C82)  LEVEL2+
 *
 * Уникальная особенность: handler **читает карту тайлов** для принятия
 * решений.  Это первый из декомпилированных, использующий map data.
 *
 * Диспетчер: cmp ax, 6; jna; jmp [cs:bx+0xC9E].
 * Jump table (file 0x1591E, 7 entries):
 *   state 0,1 → 0x0CAC → file 0x1592C — INIT (tile lookup)
 *   state 2   → 0x0D1B → file 0x1599B — TICK
 *   state 3   → 0x0E05 → file 0x15A85 — RETURN
 *   state 4   → 0x0D24 → file 0x159A4 — HURT
 *   state 5   → 0x0D33 → file 0x159B3 — HURT-FX
 *   state 6   → 0x0DF1 → file 0x15A71 — DYING
 *
 *   INIT (0x1592C):
 *       obj.behavior_phase = obj.sub_timer        // +0xED ← +0xC5
 *       obj.state_list_off = 0x1A78               // +0xD5
 *       pending_state_id   = obj.behavior_phase
 *
 *       // Convert obj.x_world → tile_x via shift + lookup
 *       bx = obj.x_world
 *       bx >>= [DS:0xB828]                         // tile_size_shift (обычно 4)
 *       bx <<= 1                                    // ×2 для WORD-таблицы
 *       di = px_to_tile_x[bx]                      // адресуется [bx - 0x44A2]
 *
 *       // Convert obj.y_world → tile_y
 *       bx = obj.y_world
 *       bx <<= 1
 *       si = px_to_tile_y[bx]                      // адресуется [bx - 0x50DE]
 *
 *       push si; push di
 *       call cs:0xD4B:0x21                         // tile_at(tile_x, tile_y)
 *       sp += 4
 *       bx = ax * 2                                 // tile_id для lookup
 *       ...                                         // дальнейшие решения по тайлу
 *
 * Это позволяет AI «знать», на каком тайле он стоит — например, остановиться
 * на лестнице или сменить поведение на воде.  Открывает дверь к
 * map-зависимой логике, которая отсутствует у простых типов.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type22() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT — reads tile map */
        /* obj.behavior_phase = obj.sub_timer; */
        obj->state_list_off = 0x1A78;
        /* pending_state_id = obj.behavior_phase; */

        /* Преобразование позиции в тайловые координаты через LUT */
        /* extern u16 px_to_tile_x[], px_to_tile_y[];
         * extern int tile_size_shift; // DS:0xB828
         * u16 tx = px_to_tile_x[obj->x_world >> tile_size_shift];
         * u16 ty = px_to_tile_y[obj->y_world];
         * u16 tile = tile_at_xy(tx, ty);  // cs:0xD4B:0x21
         * // tile is used to dispatch further behavior */
        return;
    }
    case 2:  /* TICK */
        return;
    case 3:
        return;
    case 4:  /* HURT */
        return;
    case 5:  /* HURT-FX */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 24 — multi-spawner (file 0x15029, cs:0x1348:0x03A9)
 *
 * INIT создаёт 7 ДОЧЕРНИХ объектов через вызов cs:0x1348:0xF57 в цикле.
 * Это вероятно «формация птиц/насекомых», где материнский объект (type 24)
 * рождает группу врагов разом.  Шаблон «mob-spawner».
 *
 * Jump table (file 0x15041, 7 entries):
 *   state 0,1 → 0x03CF → file 0x1504F — INIT
 *   state 2   → 0x042F → file 0x150AF — TICK
 *   state 3,5 → 0x059B → file 0x1521B — RETURN
 *   state 4   → 0x0587 → file 0x15207 — HURT
 *   state 6   → 0x0596 → file 0x15216 — DYING
 *
 *   INIT (0x1504F):
 *       [DS:0xD97C] = 0                              // глобальный счётчик
 *       spawn_fx(obj, sprite=0xBB=187, 0)            // cs:0xD4B:0x19F
 *
 *       for (si = 0; si < 7; ++si):                  // ровно 7 детей
 *           push 4                                    // arg2 = type/category
 *           push si                                   // arg1 = index в формации
 *           call cs:0x1348:0xF57(si, 4)               // spawn_child_object
 *
 *       obj.misc_state = 4                            // +0xE9
 * ─────────────────────────────────────────────────────────────────── */
void ai_type24() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT — spawn 7 children */
        /* global_counter_D97C = 0; */
        /* spawn_fx(obj, 0xBB, 0); */
        for (u16 si = 0; si < 7; ++si) {
            /* spawn_child_object(si, 4); */
            (void)si;
        }
        /* obj.misc_state = 4; */
        return;
    }
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 25 — PRNG-timer (file 0x156A0, cs:0x1348:0x0A20)
 *
 * Использует Turbo-C `_rand()` для случайной задержки между действиями.
 * Полезно для «непредсказуемых» врагов, чтобы избегать паттерн-абуза.
 *
 * Jump table (file 0x156BB, 7 entries):
 *   state 0   → 0x0A49 → file 0x156C9 — spawn FX
 *   state 1   → 0x0A56 → file 0x156D6 — main INIT (PRNG timer)
 *   state 2   → 0x0A78 → file 0x156F8 — TICK
 *   state 3,5 → 0x0BAC → file 0x1582C — RETURN
 *   state 4   → 0x0B93 → file 0x15813 — HURT
 *   state 6   → 0x0BA2 → file 0x15822 — DYING
 *
 *   state 0 (0x156C9):
 *       ++[DS:0xD97A]                                // глоб. счётчик
 *       ai_result |= 0x10                            // spawn FX
 *
 *   state 1 (0x156D6):
 *       obj.sub_timer_max = 4                        // +0xC9
 *       obj.state_list_off = 0x1A58                  // +0xD5
 *       anim_for_direction(4)                         // cs:0xD4B:0x8D
 *       obj.flag_DD = 0                              // +0xDD
 *       obj.flag_2E = 1                               // +0x2E (visible)
 *
 *       ax = rand_word()                              // cs:0x1CE7:0x17
 *       ax &= 0x0F                                    // 0..15
 *       ax += 0x1E = 30                               // = 30..45
 *       // ax используется как задержка/таймер
 *       ...
 * ─────────────────────────────────────────────────────────────────── */
void ai_type25() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:  /* spawn FX */
        /* ++global_counter_D97A; */
        ai_result |= 0x10;
        return;
    case 1: {  /* main INIT */
        obj->sub_timer_max  = 4;
        obj->state_list_off = 0x1A58;
        /* anim_for_direction(4); */
        /* obj.flag_DD = 0; obj.flag_2E = 1; */
        u16 delay = static_cast<u16>((rng_next() & 0x0F) + 30);  /* 30..45 */
        (void)delay;
        return;
    }
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 26 — player-direction mover (file 0x15AC6, cs:0x1348:0x0E46)
 *
 * Быстрый враг (sub_timer_max=2), выбирающий направление к игроку
 * на INIT и устанавливающий 32-bit velocity 0xC000:0 (= -0x4000 в
 * signed) направленно.
 *
 * Jump table (file 0x15ADE, 7 entries):
 *   state 0,1 → 0x0E6C → file 0x15AEC — INIT
 *   state 2   → 0x0ECD → file 0x15B4D — TICK
 *   state 3,5 → 0x1150 → file 0x15DD0 — RETURN
 *   state 4   → 0x113C → file 0x15DBC — HURT
 *   state 6   → 0x114B → file 0x15DCB — DYING
 *
 *   INIT (0x15AEC):
 *       obj.sub_timer_max  = 2                        // +0xC9 (быстрый!)
 *       obj.state_list_off = 0x1AA4                   // +0xD5
 *       obj.misc_state     = 2                         // +0xE9
 *
 *       // Direction to player:
 *       ax = primary_player.x_world
 *       cmp ax, obj.x_world
 *       if (ax > obj.x): dir = 1 else: dir = 0
 *       obj.behavior_phase = dir                      // +0xED
 *
 *       if (behavior_phase != 0):
 *           // moving right: velocity high word
 *           dx = 0, ax = 0xC000   // negative 0x4000 in upper half
 *       else:
 *           // moving left: opposite
 *           dx = ?
 *           ...
 * ─────────────────────────────────────────────────────────────────── */
void ai_type26() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT */
        obj->sub_timer_max  = 2;
        obj->state_list_off = 0x1AA4;
        /* obj.misc_state = 2; */
        bool to_right = primary_player->x_world > obj->x_world;
        /* obj.behavior_phase = to_right ? 1 : 0; */
        /* if (to_right): velocity = 0xC000:0  else: velocity = -velocity */
        (void)to_right;
        return;
    }
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 27 — атака сверху (file 0x1642B, cs:0x1348:0x17AB)
 *
 * Появляется на 70 пикселей ВЫШЕ позиции игрока (зафиксированной в
 * player_cache_y) — это «бомбардировщик», падающий на голову.
 *
 * Jump table (file 0x16443, 7 entries):
 *   state 0   → 0x17D1 → file 0x16451 — Y-snap + FX
 *   state 1   → 0x17E8 → file 0x16468 — main INIT
 *   state 2   → 0x182F → file 0x164AF — TICK
 *   state 3,5 → 0x1979 → file 0x165F9 — RETURN
 *   state 4   → 0x1965 → file 0x165E5 — HURT
 *   state 6   → 0x1974 → file 0x165F4 — DYING
 *
 *   state 0 (0x16451):
 *       ax = [DS:0xD8BE]                              // player_cache_y
 *       ax += 0xFFBA = -70                            // 70 px НАД игроком
 *       obj.y_world = ax                              // +0x0C
 *       ai_result |= 0x10                             // spawn FX
 *
 *   state 1 (0x16468):
 *       obj.state_list_off = 0x1B46                   // +0xD5
 *       obj.sub_timer_max  = 3                         // +0xC9
 *       obj.misc_state     = 0                         // +0xE9
 *       ax = obj.sub_timer_start                       // +0xC3
 *       ax *= 15                                       // спрайтовый шаг
 *       ax = -ax
 *       obj+0xEB = ax                                 // обратный счётчик
 *       if (obj.sub_timer_start > 2):  ax = ...        // больше 2 — special
 * ─────────────────────────────────────────────────────────────────── */
void ai_type27() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:  /* Y-snap above player + FX */
        obj->y_world = static_cast<u16>(player_cache_y - 70);
        ai_result |= 0x10;
        return;
    case 1: {  /* main INIT */
        obj->state_list_off = 0x1B46;
        obj->sub_timer_max  = 3;
        /* obj.misc_state = 0; */
        /* obj+0xEB = -(obj.sub_timer_start * 15); */
        return;
    }
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 28 — FX-spawn с direction (file 0x165FC, cs:0x1348:0x197C)
 *
 * Jump table (file 0x16613, 7 entries):
 *   state 0   → 0x19A1 → file 0x16621 — spawn FX
 *   state 1   → 0x19C1 → file 0x16641 — main INIT
 *   state 2   → 0x1A1F → file 0x1669F — TICK
 *   state 3,5 → 0x1B18 → file 0x16798 — RETURN
 *   state 4   → 0x1B04 → file 0x16784 — HURT
 *   state 6   → 0x1B13 → file 0x16793 — DYING
 *
 *   state 0 (0x16621):
 *       spawn_fx(obj, sprite=0xCF=207, 0)             // cs:0xD4B:0x19F
 *       ai_result |= 0x10
 *
 *   state 1 (0x16641):
 *       obj.sub_timer_max  = 4                        // +0xC9
 *       obj.behavior_phase = 0                         // +0xED
 *       obj.state_list_off = 0x1B92                   // +0xD5
 *       ai_result |= 2                                 // transition
 *
 *       ax = primary_player.x_world
 *       // ... direction logic
 * ─────────────────────────────────────────────────────────────────── */
void ai_type28() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:  /* spawn FX */
        /* spawn_fx(obj, 0xCF, 0); */
        ai_result |= 0x10;
        return;
    case 1: {  /* main INIT */
        obj->sub_timer_max  = 4;
        /* obj.behavior_phase = 0; */
        obj->state_list_off = 0x1B92;
        ai_result          |= 2;
        return;
    }
    case 2:  /* TICK */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 29 — multi-phase с внутренним switch (file 0x161B1, cs:0x1348:0x1531)
 *
 * Сложный AI: внешний state_byte (0..6) + внутренний switch по misc_state
 * (8 веток!).  Шаблон похож на type 7, но с 8 sub-фазами вместо 4.
 *
 * Jump table (file 0x161C9, 7 entries):
 *   state 0,1 → 0x1557 → file 0x161D7 — INIT
 *   state 2   → 0x1589 → file 0x16209 — TICK (inner switch)
 *   state 3,5 → 0x17A8 → file 0x16428 — RETURN
 *   state 4   → 0x1794 → file 0x16414 — HURT
 *   state 6   → 0x17A3 → file 0x16423 — DYING
 *
 *   INIT (0x161D7):
 *       obj.sub_timer_max  = 8                         // МЕДЛЕННЫЙ
 *       obj.misc_state     = 0                         // +0xE9
 *       obj.state_list_off = 0x1AF0                   // +0xD5
 *       spawn_fx(obj, sprite=0xD3=211, 0)             // cs:0xD4B:0x19F
 *
 *       si = obj.misc_state                            // 0 на init
 *       obj.misc_state |= 1                            // set bit 0
 *       ax = si
 *       if (ax > 7): jmp far_return
 *       bx = ax * 2
 *       jmp [cs:bx + JT_INNER]                         // 8-way inner switch
 * ─────────────────────────────────────────────────────────────────── */
void ai_type29() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT с inner dispatch */
        obj->sub_timer_max  = 8;
        obj->state_list_off = 0x1AF0;
        /* spawn_fx(obj, 0xD3, 0); */

        u16 inner = 0;  /* obj->misc_state */
        /* obj->misc_state |= 1; */
        if (inner > 7) return;
        switch (inner) {
        case 0: /* phase 0 */ break;
        case 1: case 2: case 3:
        case 4: case 5: case 6: case 7:
            /* phase 1..7 — отдельные обработчики */
            break;
        }
        return;
    }
    case 2:  /* TICK with inner switch */
    case 3:
    case 5:
        return;
    case 4:  /* HURT */
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 30 — динамический multi-bbox (file 0x15F4F, cs:0x1348:0x12CF)
 *
 * Похож на type 20, но bboxes растут СО ВРЕМЕНЕМ (а не задаются сразу
 * по sub_timer). Alternates sprite_id между 0x05 и 0x98=152.
 *
 * Jump table (file 0x15F6B, 7 entries):
 *   state 0,1   → 0x12F9 → file 0x15F79 — INIT (num_bboxes = 0)
 *   state 2     → 0x140A → file 0x1608A — TICK (++num_bboxes)
 *   state 3,4,5 → 0x152B → file 0x161AB — RETURN
 *   state 6     → 0x147C → file 0x160FC — DYING
 *
 *   INIT (0x15F79):
 *       [DS:0xD97A]   = 0                              // глоб. счётчик
 *       obj.behavior_phase = 0                          // +0xED
 *       obj.misc_state     = 0                          // +0xE9
 *       obj+0xEB           = primary_player.x_world    // anchor X
 *       obj.num_bboxes     = 0                          // +0x1E (НИЧЕГО ВНАЧАЛЕ)
 *
 *       // Pre-populate bbox templates с чередованием спрайтов:
 *       for (si = 0; si < N; ++si):
 *           bbox_ptr = obj + 0x20 + si * 0x12
 *           if (si & 1):  bbox.sprite_id = 0x98 = 152   // нечётный
 *           else:         bbox.sprite_id = 0x05         // чётный
 *           bbox.x_offset = ...
 *           bbox.y_offset = ...
 *
 *   TICK (0x1608A):
 *       // Каждый кадр (после задержки) увеличивает num_bboxes на 1,
 *       // показывая новый сегмент. Когда достигает максимума —
 *       // переход в активную фазу.
 *
 * Это создаёт визуальный эффект «вытягивания» — например, длинного
 * языка или цепи, которые медленно растут перед атакой.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type30() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;
    if (current_state_byte > 6) return;

    switch (current_state_byte) {
    case 0:
    case 1: {  /* INIT — num_bboxes = 0 */
        /* global_counter_D97A = 0; */
        /* obj.behavior_phase = 0; obj.misc_state = 0; */
        /* obj+0xEB = primary_player->x_world;  // anchor X */
        obj->num_bboxes = 0;

        /* Pre-populate bbox templates с чередующимися sprite_ids */
        for (int si = 0; si < 8; ++si) {
            obj->bboxes[si].sprite_id = (si & 1) ? 0x98 : 0x05;
            /* obj->bboxes[si].x_offset/y_offset = ...; */
        }
        return;
    }
    case 2: {  /* TICK — постепенный рост num_bboxes */
        /* if (++sub_timer >= threshold): ++obj->num_bboxes; */
        return;
    }
    case 3:
    case 4:
    case 5:
        return;
    case 6:  /* DYING */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 31 — spawn (0xDD) (file 0x1679A, cs:0x1348:0x1B1A)
 *
 * Простой "появляющийся" враг: spawn FX → main behavior.
 *
 *   state 0,1 (0x167C0):
 *       obj.sub_timer_max = 4
 *       obj.misc_state    = 0
 *       spawn_fx(obj, sprite=0xDD=221, 0)
 *       obj.state_list    = (ds:0x1BC4)
 *       ai_result |= 2
 *   state 2 → TICK; state 4 → HURT; state 6 → DYING
 * ─────────────────────────────────────────────────────────────────── */
void ai_type31() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 4;
        obj->state_list_off = 0x1BC4;
        ai_result          |= 2;
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 32 — spawn (0xDE) (file 0x168B4, cs:0x1348:0x1C34)
 *
 * Близнец type 31: тот же шаблон, sprite_id +1.
 *
 *   state 0,1 (0x168D9):
 *       obj.sub_timer_max = 4
 *       spawn_fx(obj, sprite=0xDE=222, 0)
 *       obj.state_list    = (ds:0x1BE0)
 *       pending_state_id  = 0
 *       ai_result        |= 2
 *
 * Различие с type 31: sprite_id (0xDE vs 0xDD), state_list (0x1BE0 vs
 * 0x1BC4), отсутствие явного misc_state init. Скорее всего это варианты
 * одного и того же врага (например, разные позиции/направления).
 * ─────────────────────────────────────────────────────────────────── */
void ai_type32() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 4;
        obj->state_list_off = 0x1BE0;
        pending_state_id    = 0;
        ai_result          |= 2;
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 33 — 7-bbox с HP-кэшем (file 0x16A6D, cs:0x151A:0x00CD)
 *
 * Подобен type 21 (7 сегментов), но дополнительно:
 *   • Кэширует `obj.hit_points` (+0xE7) в `obj+0xEB` — это позволяет
 *     визуализировать "оставшееся здоровье" через окраску сегментов.
 *   • Активный флаг сбрасывается в 0 (включается отдельно).
 *
 *   INIT (0x16A97):
 *       obj.misc_state  = 0                    // +0xE9
 *       obj+0xEB        = obj.hit_points        // кэш HP
 *       obj.active_flag = 0                    // +0xDB
 *       obj.num_bboxes  = 7                    // +0x1E ← FIXED
 *
 *       for (si = 0; si < 7; ++si):             // init each segment
 *           bbox_ptr = obj+0x20 + si*0x12
 *           if (si == 0):
 *               sprite_id = first_sprite       // другой для головы
 *           else:
 *               sprite_id = body_sprite        // одинаковый для тела
 *           // ... x_offset / y_offset
 * ─────────────────────────────────────────────────────────────────── */
void ai_type33() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {
        /* obj.misc_state = 0;          // +0xE9
         * obj+0xEB = obj.hit_points;   // cache for damage visualization */
        obj->active_flag = 0;
        obj->num_bboxes  = 7;
        for (int si = 0; si < 7; ++si) {
            /* obj->bboxes[si].sprite_id = ...; */
        }
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 34 — 6-bbox HP-indicator (file 0x16C46, cs:0x151A:0x02A6)
 *
 * 6 сегментов, каждый отображает 1/6 общего HP. При попадании сегмент
 * визуально "ломается".
 *
 *   INIT (0x16C6F):
 *       obj.active_flag = 0
 *       obj.num_bboxes  = 6                    // +0x1E (FIXED)
 *       obj.misc_state  = 0                    // +0xE9
 *       obj+0xED        = obj.hit_points        // HP cache
 *       obj+0xEB        = 5                    // ??? счётчик/сегмент
 *       obj+0xEF        = 0                    // alt_sub_phase
 *
 *       si = obj.misc_state
 *       // ... per-segment init
 * ─────────────────────────────────────────────────────────────────── */
void ai_type34() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {
        obj->active_flag = 0;
        obj->num_bboxes  = 6;
        /* obj.misc_state = 0;
         * obj+0xED = obj.hit_points;
         * obj+0xEB = 5;
         * obj+0xEF = 0; */
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 35 — dual-mode по sub_timer_start (file 0x17250, cs:0x151A:0x08B0)
 *
 * При INIT смотрит на `obj.sub_timer_start` (+0xC3) — поле, переданное
 * spawn'ером:
 *   • Если > 10:  behavior_phase = 1 (агрессивный режим)
 *   • Иначе:      behavior_phase = 0 (пассивный)
 *
 * Это позволяет один handler использовать для двух «вариаций» одного
 * врага.
 *
 *   INIT (0x17279):
 *       obj.sub_timer_max  = 4
 *       obj.misc_state     = 0
 *       obj+0xEB           = 0
 *       if (obj.sub_timer_start > 10):  ax = 1   // aggressive
 *       else:                            ax = 0   // passive
 *       obj.behavior_phase = ax
 *       obj.state_list_seg = ds
 *       obj.state_list_off = ???               // depends on phase
 * ─────────────────────────────────────────────────────────────────── */
void ai_type35() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {
        obj->sub_timer_max = 4;
        /* obj.misc_state = 0;  obj+0xEB = 0;
         * bool aggressive = (obj.sub_timer_start > 10);
         * obj.behavior_phase = aggressive ? 1 : 0; */
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * ★★★ Type 36 — RYU, ФИНАЛЬНЫЙ БОСС (LEVEL9) ★★★
 *
 * file 0x16FBB, cs:0x151A:0x061B
 *
 * Это AI handler **главного антагониста Karnov** — дракона Ryu, с
 * которым игрок сражается в финале (см. сюжет в KARNOV.TXT):
 *
 *   «The Treasure has been sought by Ryu, a huge and evil dragon,
 *    for thousands of years…»
 *
 * Технические характеристики:
 *   • 2-bbox дракон: голова (sprite 0xF5=245) + тело (sprite 0xF9=249)
 *   • x_world выровнен по 8-px (как type 20 — для пиксельной точности)
 *   • Начинает невидимым (flag_2E = 0)
 *
 * Jump table (file 0x16FD3, 7 entries):
 *   state 0,1 → 0x0641 → file 0x16FE1 — INIT
 *   state 2   → 0x06F8 → file 0x17098 — TICK
 *   state 3   → 0x07F7 → file 0x17197 — RETURN
 *   state 4   → 0x07DE → file 0x1717E — HURT
 *   state 5   → 0x07A0 → file 0x17140 — HURT-FX (особый!)
 *   state 6   → 0x07F1 → file 0x17191 — DYING
 *
 *   INIT (0x16FE1):
 *       obj.misc_state     = 0                   // +0xE9
 *       obj.behavior_phase = 0                   // +0xED
 *       obj.num_bboxes     = 2                   // +0x1E  ← 2 части
 *       obj.x_world       &= 0xFFF8              // align 8-px
 *
 *       // BBox[0] — голова/основное тело
 *       obj.bbox[0].sprite_id = 0xF5 = 245       // +0x20
 *       obj.bbox[0].x_offset  = 2                // +0x22
 *       obj.bbox[0].y_offset  = 0                // +0x24
 *       obj.flag_2E           = 0                // +0x2E (НЕВИДИМ)
 *
 *       // BBox[1] — хвост / вторая часть
 *       obj.bbox[1].sprite_id = 0xF9 = 249       // +0x32
 *       obj.bbox[1].x_offset  = ?               // +0x34
 *       obj.bbox[1].y_offset  = ?               // +0x36
 *
 * Note: разница между bbox[0] и bbox[1] — это sprite_id (245 vs 249).
 * 4 промежуточных sprite (246, 247, 248) вероятно — другие фреймы
 * анимации (открытая пасть, дыхание огня и т.п.), переключаемые
 * через state_list во время боя.
 *
 * Уникальное состояние 5 (HURT-FX) в Ryu — отдельный обработчик для
 * "вспышки урона", чтобы босс мигал при попадании файрболом.
 * ═══════════════════════════════════════════════════════════════════ */
void ai_type36() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {  /* INIT — Ryu spawn */
        /* obj.misc_state = 0; obj.behavior_phase = 0; */
        obj->num_bboxes               = 2;             /* 2-bbox dragon! */
        obj->x_world                  = static_cast<u16>(obj->x_world & 0xFFF8);

        /* bbox[0] — primary part */
        obj->bboxes[0].sprite_id      = 0xF5;          /* 245 */
        obj->bboxes[0].x_offset       = 2;
        obj->bboxes[0].y_offset       = 0;
        /* obj.flag_2E = 0 (invisible at first); */

        /* bbox[1] — secondary part (tail or head) */
        obj->bboxes[1].sprite_id      = 0xF9;          /* 249 */
        obj->bboxes[1].x_offset       = 0;
        obj->bboxes[1].y_offset       = 0;
        return;
    }
    case 2:   /* TICK — основная боевая логика Ryu */
        return;
    case 3:
        return;
    case 4:   /* HURT */
        return;
    case 5:   /* HURT-FX — мигание при попадании */
        return;
    case 6:   /* DYING — финальный взрыв */
        ai_result |= 1;
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 37 — slow boss с shared helper (file 0x175D1, cs:0x151A:0x0C31)
 *
 * Использует helper `cs:?:0x4B36` — ТОТ ЖЕ, что у types 9 и 19 — это
 * **общий движок для крупных врагов**. Долгий тик (sub_timer_max=7).
 *
 *   INIT (0x175F7):
 *       obj.sub_timer_max  = 7                    // долгий цикл
 *       obj.misc_state     = 0, obj+0xEB = 0
 *       obj+0xED           = obj.hit_points        // HP cache
 *       obj.state_list_off = 0x1C86
 *       spawn_with_sprite(obj, sprite=0xFB=251)    // через cs:?:0x?
 * ─────────────────────────────────────────────────────────────────── */
void ai_type37() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 7;
        obj->state_list_off = 0x1C86;
        /* obj.misc_state = 0; obj+0xEB = 0;
         * obj+0xED = obj.hit_points;
         * spawn_helper(obj, 0xFB, ...); */
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 38 — long-timer enemy (file 0x17867, cs:0x151A:0x0EC7)
 *
 * Особенность: state_ext (+0xC7) инициализируется в 75 — это
 * длительный счётчик, который тикает каждый кадр. Через 75 кадров
 * (≈2.5 секунды при 30 fps) триггерится переход состояния.
 *
 *   INIT (0x1788D):
 *       obj.sub_timer_max  = 7
 *       obj.misc_state     = 0
 *       obj+0xED           = obj.hit_points
 *       obj.alt_sub_phase  = 0
 *       obj.state_list_off = 0x1CA4
 *       obj.flag_2E        = 0                   // НЕВИДИМ ВНАЧАЛЕ
 *       obj.state_ext      = 75                   // +0xC7 (длинный таймер)
 *
 * Использование state_ext = 75 + flag_2E = 0 = «появляется через 75
 * кадров после спавна». Это **delayed-appearance enemy** — игрок не
 * успевает заранее подготовиться.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type38() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 7;
        obj->state_list_off = 0x1CA4;
        obj->state_ext      = 75;          /* long delayed-appearance */
        /* obj.flag_2E = 0; */
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 39 — 6-сегментная цепь (sprite 0x103) (file 0x179F2, cs:0x161F:0x0002)
 *
 * Аналог type 21 (7-сегмент) и type 33 (7-segment HP), но **6 сегментов**
 * с **высоким sprite_id 0x103=259** (выходит за стандартный диапазон 0..0xFF —
 * специальная нумерация для extended-sprites).
 *
 *   INIT (0x17A18):
 *       obj.sub_timer_max = 4
 *       obj.num_bboxes    = 6                     // +0x1E
 *
 *       for (si = 0; si < 6; ++si):
 *           bbox_ptr = obj+0x20 + si*0x12
 *           bbox.sprite_id = 0x103 = 259           // EXTENDED sprite!
 *           // ... x_offset, y_offset
 * ─────────────────────────────────────────────────────────────────── */
void ai_type39() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {
        obj->sub_timer_max = 4;
        obj->num_bboxes    = 6;
        for (int si = 0; si < 6; ++si) {
            obj->bboxes[si].sprite_id = 0x103;     /* 259 — extended */
        }
        return;
    }
    case 2: case 3: case 4: case 5: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 40 — self-init multi-phase (file 0x17E37, cs:0x161F:0x0447)
 *
 * Использует трюк «misc_state |= 1» (как type 29) — первый раз через
 * INIT misc_state == 0, после установки бита 0 последующие тики
 * пропускают init-логику.
 *
 *   INIT (0x17E5F):
 *       obj.sub_timer_max  = 2                    // быстрый
 *       obj.misc_state     = 0
 *       obj.state_list_off = 0x1D22
 *       obj.behavior_phase = 0
 *
 *       si = obj.misc_state                        // = 0 при первом запуске
 *       obj.misc_state    |= 1                     // set bit 0 — INIT done
 *       les bx, [DS:0xD95F]                       // primary_player
 *       // ... player-dependent setup
 * ─────────────────────────────────────────────────────────────────── */
void ai_type40() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 2;
        obj->state_list_off = 0x1D22;
        /* obj.misc_state = 0;  obj.behavior_phase = 0;
         * if ((obj.misc_state & 1) == 0):
         *     obj.misc_state |= 1;
         *     // first-time init using primary_player */
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 41 — mid-boss с высоким HP (file 0x17BFA, cs:0x161F:0x020A)
 *
 * Большой одиночный враг с HP=100 (vs типичные 20 у type 12). Скорее
 * всего mini-boss или важный сюжетный противник.
 *
 *   INIT (0x17C1F):
 *       obj.obj_type     = 3                   // +0xE5 (subtype = 3)
 *       obj.hit_points   = 100                  // +0xE7 — высокое
 *       obj.flag_2E      = 1                   // visible
 *       obj.sub_timer_max= 2                    // быстрый тик
 *       obj.num_bboxes   = 1
 *       obj.state_list   = (ds:0x1CBE)
 *       pending_state_id = 0
 *       ai_result       |= 2                    // signal transition
 * ─────────────────────────────────────────────────────────────────── */
void ai_type41() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->obj_type       = 3;
        obj->hit_points     = 100;        /* высокое HP — mid-boss */
        obj->sub_timer_max  = 2;
        obj->num_bboxes     = 1;
        obj->state_list_off = 0x1CBE;
        pending_state_id    = 0;
        ai_result          |= 2;
        return;
    case 2: case 3: case 4: case 5: return;
    case 6: ai_result |= 1; return;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Type 42 — 3-state objekt (УНИКАЛЬНО!) (file 0x17C8F, cs:0x161F:0x029F)
 *
 * Единственный handler из всех 57, использующий `cmp ax, 2` вместо
 * `cmp ax, 6`. У него только 3 ветки (state 0, 1, 2) — это короткоживущий
 * объект (probably projectile, particle FX, или временный эффект).
 *
 * Jump table (file 0x17CA7, ТОЛЬКО 3 entries):
 *   state 0,1 → 0x02BD → file 0x17CAD — INIT
 *   state 2   → 0x031B → file 0x17D0B — TICK/DIE
 *
 *   INIT (0x17CAD):
 *       obj.sub_timer_max  = 3
 *       obj.misc_state     = 0                  // +0xE9
 *
 *       ax = obj.sub_timer (+0xC5)
 *       ax &= 1                                  // нижний бит → direction
 *       obj.behavior_phase = ax                  // +0xED
 *
 *       ax = obj.sub_timer * 10                  // timing parameter
 *       obj+0xEB = ax
 *
 *       if (behavior_phase != 0):
 *           obj.y_world -= 20                    // sub: переместить вверх
 *
 *       // ...
 *
 * Отсутствие state 3..6 означает, что у объекта НЕТ отдельных HURT/DYING
 * фаз — он либо удаляется через ai_result|=1, либо живёт только TICK
 * до естественного конца через timer.
 * ═══════════════════════════════════════════════════════════════════ */
void ai_type42() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 2) return;        /* ONLY 3 states! */
    switch (current_state_byte) {
    case 0: case 1: {  /* INIT */
        obj->sub_timer_max = 3;
        /* obj.misc_state = 0;
         * obj.behavior_phase = obj.sub_timer & 1;
         * obj+0xEB = obj.sub_timer * 10;
         * if (behavior_phase != 0): obj.y_world -= 20; */
        return;
    }
    case 2:  /* TICK / single behavior */
        return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 43 — self-init с HP cache (file 0x18325, cs:0x1692:0x0205)
 *
 * Использует трюк "misc_state |= 1" (как types 29, 40) для one-shot
 * инициализации + кэширование HP в +0xEB.
 *
 *   INIT (0x1834E):
 *       obj.misc_state     = 0                  // +0xE9
 *       obj+0xEB           = obj.sub_timer       // cache initial timer
 *       obj.sub_timer_max  = 3
 *       obj.state_list     = (ds:0x1D7E)
 *
 *       si = obj.misc_state                      // = 0 при первом init
 *       obj.misc_state    |= 1                   // mark as init'd
 *       if (si != 0): jmp 0x83AD                 // skip re-init
 *       // ... continue init only on first call
 * ─────────────────────────────────────────────────────────────────── */
void ai_type43() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 3;
        obj->state_list_off = 0x1D7E;
        /* obj.misc_state = 0; obj+0xEB = obj.sub_timer;
         * if ((obj.misc_state & 1) == 0):
         *     obj.misc_state |= 1;
         *     // perform first-time init */
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 44 — random-velocity projectile (file 0x1864E, cs:0x1692:0x052E)
 *
 * При INIT вызывает Turbo-C `_rand()` и формирует **рандомизированную
 * 32-bit скорость** через стандартный паттерн:
 *   ax = rand & 0x1FFF                  // 0..8191
 *   cwd                                  // sign-extend (dx = 0)
 *   ax += 0x2000                          // +0x2000 → range 0x2000..0x3FFF
 *   adc dx, -1                            // -1 в dx если carry (== 0xFFFF)
 *   → 32-bit signed velocity в обоих знаках
 *
 *   INIT (0x18675):
 *       obj+0xEB = obj.sub_timer
 *       ax = rand_word() & 0x1FFF
 *       cwd; ax += 0x2000; adc dx, -1
 *       obj+0x10 = dx                   // vy_hi (sign)
 *       obj+0x0E = ax                    // vy_lo
 *       obj+0x14 = 0                    // vx_hi
 *       obj+0x12 = 0x3000                // vx_lo (fixed)
 *       obj.sub_timer_max = 3            // быстрый
 *
 * Это **рандомизированный снаряд**: vy случайный по направлению и
 * силе, vx фиксированный. Создаёт «брызги» или огненные искры.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type44() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {  /* INIT random projectile */
        u16 r = static_cast<u16>(rng_next() & 0x1FFF);
        /* vy = 32-bit sign-extended: (r + 0x2000) с учётом знака */
        std::int32_t vy = static_cast<std::int32_t>(r) + 0x2000;
        if (vy & 0x10000) vy |= static_cast<std::int32_t>(0xFFFF0000);
        /* obj+0x10:0x0E = vy; obj+0x14 = 0; obj+0x12 = 0x3000 */
        obj->sub_timer_max = 3;
        (void)vy;
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 45 — slower random-velocity projectile (file 0x181D4, cs:0x1692:0x00B4)
 *
 * Зеркало type 44 с другими параметрами:
 *
 *   INIT (0x181FB):
 *       obj+0xEB = obj.sub_timer
 *       ax = rand & 0x1FFF
 *       cwd; ax += 0x8000; adc dx, -1        // +0x8000 вместо +0x2000
 *       obj+0x10 = dx; obj+0x0E = ax           // vy (выше скорость)
 *       obj+0x14 = 0; obj+0x12 = 0x2000        // vx меньше (0x2000)
 *       obj.sub_timer_max = 7                  // 7 (vs 3 у type 44 — медленнее)
 *
 * type 44 — мелкий быстрый projectile
 * type 45 — крупный медленный projectile с большей вертикальной скоростью
 * ─────────────────────────────────────────────────────────────────── */
void ai_type45() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {  /* INIT slower random projectile */
        u16 r = static_cast<u16>(rng_next() & 0x1FFF);
        std::int32_t vy = static_cast<std::int32_t>(r) + 0x8000;
        if (vy & 0x10000) vy |= static_cast<std::int32_t>(0xFFFF0000);
        /* obj+0x10:0x0E = vy; obj+0x14 = 0; obj+0x12 = 0x2000 */
        obj->sub_timer_max = 7;
        (void)vy;
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 46 — extended-sprite враг (file 0x1878B, cs:0x1692:0x066B)
 *
 * Использует sprite_id 0x124 = 292 — выходит за 8-bit диапазон.
 * Это пока что только **второй** decompiled handler с extended sprite
 * (первый был type 39 с 0x103=259).
 *
 *   INIT (0x187B3):
 *       spawn_fx(obj, sprite=0x124, 0)         // EXTENDED sprite ID
 *       obj.misc_state    = 0
 *       obj.sub_timer_max = 9                   // средне-медленный
 *       obj.state_list    = (ds:0x1DE4)
 *       les bx, [DS:0xD95F]                    // primary_player
 *       // ... player-dependent setup
 * ─────────────────────────────────────────────────────────────────── */
void ai_type46() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        /* spawn_fx(obj, 0x124, 0);  -- extended sprite ID */
        obj->sub_timer_max  = 9;
        obj->state_list_off = 0x1DE4;
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 47 — Y-snap с PRNG (file 0x18599, cs:0x1692:0x0479)
 *
 * Очень медленный (sub_timer_max=10), смещает Y по таблице, использует
 * PRNG для рандомизации поведения.
 *
 *   INIT (0x185BE):
 *       obj+0xEB           = obj.sub_timer       // cache
 *       obj.sub_timer_max  = 10                  // очень медленный
 *       ax = [DS:0x10D4]                          // Y-offset из таблицы
 *       obj.y_world       -= ax                   // снести Y вверх
 *       obj.state_list     = (ds:0x1DA4)
 *       pending_state_id   = 0
 *       ai_result         |= 2
 *       call rand_word()                          // cs:0x1CE7:0x17
 *       // ... rand-based further setup
 *
 * Это медленный, "размышляющий" враг — каждое действие происходит раз
 * в ~10 кадров, с элементом случайности. Подходит для босс-помощников
 * или периодически-стреляющих турелей.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type47() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 10;                  /* очень медленный */
        /* obj.y_world -= [DS:0x10D4]; */
        obj->state_list_off = 0x1DA4;
        pending_state_id    = 0;
        ai_result          |= 2;
        /* rand_word() для дополнительной рандомизации */
        (void)rng_next();
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 48 — extended sprite spawn (file 0x18A4C, cs:0x170C:0x018C)
 *
 *   INIT (0x18A74):
 *       spawn_fx(obj, sprite=0x12B=299, 0)       // EXTENDED sprite ID
 *       obj.misc_state    = 0
 *       obj.sub_timer_max = 4
 *       obj.state_list seg/off = ...
 * ─────────────────────────────────────────────────────────────────── */
void ai_type48() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        /* spawn_fx(obj, 0x12B, 0);  -- extended sprite */
        obj->sub_timer_max = 4;
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 49 — LEVEL9-conditional (file 0x18C85, cs:0x170C:0x03C5)
 *
 * УНИКАЛЬНО: ветвится по `max_level_reached == 9` И `obj.x_world > 0x44C`!
 * Это AI handler, активирующийся **ТОЛЬКО на финальном уровне** при
 * определённой позиции X — скорее всего **помощник Ryu** (type 36),
 * появляющийся когда игрок добирается до арены босса.
 *
 *   INIT (0x18CAB):
 *       obj.misc_state = 0
 *       obj+0xEB       = obj.hit_points        // HP cache
 *
 *       if (max_level_reached == 9 && obj.x_world > 0x44C):
 *           ax = 1                              // special LEVEL9 branch
 *       else:
 *           ax = 0
 *
 *       // ax управляет дальнейшим выбором поведения/sprite_id
 *       // ...
 *
 * Это первый ХАРДКОДИРОВАННЫЙ "уровень-зависимый" handler — обычно
 * AI не знают, на каком уровне они находятся (логика управляется
 * через level-load), но этот handler специально проверяет.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type49() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {
        /* obj.misc_state = 0; obj+0xEB = obj.hit_points; */
        bool level9_arena = (max_level_reached == 9) && (obj->x_world > 0x44C);
        (void)level9_arena;
        /* if (level9_arena): activate Ryu helper mode */
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 50 — 7-chain extended (file 0x18DEF, cs:0x175E:0x000F)
 *
 * Близнец type 21/33/39 но со sprite 0x132=306 (extended).
 *
 *   INIT (0x18E19):
 *       obj.num_bboxes  = 7
 *       obj.active_flag = 0
 *       bbox[0].x_offset = 0xFFFC = -4
 *       bbox[0].y_offset = 0
 *       bbox[0].sprite_id = 0x132 = 306        // EXTENDED!
 *       // ... iterate remaining 6 bboxes
 * ─────────────────────────────────────────────────────────────────── */
void ai_type50() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {
        obj->num_bboxes  = 7;
        obj->active_flag = 0;
        obj->bboxes[0].x_offset  = static_cast<u16>(-4);
        obj->bboxes[0].y_offset  = 0;
        obj->bboxes[0].sprite_id = 0x132;        /* extended sprite */
        for (int si = 1; si < 7; ++si) {
            obj->bboxes[si].sprite_id = 0x132;
        }
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 51 — мега-фиксированный stage-объект (file 0x19278, cs:0x175E:0x0498)
 *
 * Большой постоянный объект с 8 bbox'ами. Сохраняет себя в глобальной
 * паре DS:0xD886/D888 как «active stage object» (аналогично type 7,
 * который сохраняет в D94F/D951).
 *
 *   INIT (0x192A0):
 *       [DS:0xD886] = obj.offset                // global "active stage obj"
 *       [DS:0xD888] = obj.segment
 *       obj.sub_timer_max = 0xFF = 255           // ВЕЧНЫЙ (раз в 255 кадров)
 *       obj.num_bboxes    = 8                    // 8 СЕГМЕНТОВ!
 *       obj.bbox[0].sprite_id = 0x135 = 309      // extended
 *       obj.bbox[0].x_offset/y_offset = 0
 *
 *       // Y-modulo логика:
 *       ax = obj.y_world
 *       bx = 10
 *       idiv bx                                  // ax / 10, dx = remainder
 *       obj.y_world -= dx                        // выравнивание Y по 10
 *
 * Это **stage decoration / arena element** — самый большой "пассивный"
 * объект в игре (8 bbox, 255-кадровый tick). Возможно фон арены босса
 * или часть сцены.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type51() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1: {
        /* [DS:0xD886/D888] = obj far ptr (mark as active stage object) */
        obj->sub_timer_max = 0xFF;                  /* effectively eternal */
        obj->num_bboxes    = 8;                      /* eight segments! */
        obj->bboxes[0].sprite_id = 0x135;            /* extended */
        obj->bboxes[0].x_offset  = 0;
        obj->bboxes[0].y_offset  = 0;
        /* Y-align к сетке 10 */
        obj->y_world = static_cast<u16>(obj->y_world - (obj->y_world % 10));
        return;
    }
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 52 — Y-snap по 10 (file 0x18FE3, cs:0x175E:0x0203)
 *
 * Простой враг с Y-выравниванием по сетке 10 пикселей. Полезно для
 * объектов, привязанных к платформам/лестницам.
 *
 *   INIT (0x19009):
 *       obj.misc_state    = 0
 *       obj.sub_timer_max = 3
 *       ax = obj.y_world
 *       idiv 10                                  // dx = ax % 10
 *       obj.y_world -= dx                         // align к 10
 *       obj.state_list = ...
 * ─────────────────────────────────────────────────────────────────── */
void ai_type52() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max = 3;
        obj->y_world = static_cast<u16>(obj->y_world - (obj->y_world % 10));
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 53 — простой шаблон (file 0x193DD, cs:0x17B6:0x007D)
 *
 *   INIT (0x19403):
 *       obj.misc_state    = 0
 *       obj.behavior_phase = 0
 *       obj.sub_timer_max = 2
 *       obj.state_list    = (ds:0x1E96)
 *       call cs:0x6E:0x59A(obj)                  // apply pos
 * ─────────────────────────────────────────────────────────────────── */
void ai_type53() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 2;
        obj->state_list_off = 0x1E96;
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 54 — extended sprite spawn (0x140) (file 0x1953B, cs:0x17B6:0x01DB)
 *
 *   INIT (0x19561):
 *       obj.sub_timer_max = 4
 *       obj.misc_state    = 0
 *       obj.state_list    = (ds:0x1EB0)
 *       spawn_fx(obj, sprite=0x140=320, 0)       // EXTENDED
 * ─────────────────────────────────────────────────────────────────── */
void ai_type54() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max  = 4;
        obj->state_list_off = 0x1EB0;
        /* spawn_fx(obj, 0x140, 0);  -- extended */
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ───────────────────────────────────────────────────────────────────
 * Type 55 — extended sprite (0x142) (file 0x196E8, cs:0x17EC:0x0028)
 *
 *   INIT (0x1970E):
 *       obj.sub_timer_max = 4
 *       obj+0xEB          = obj.hit_points        // HP cache
 *       obj.misc_state    = 2
 *       spawn_fx(obj, sprite=0x142=322, 0)
 * ─────────────────────────────────────────────────────────────────── */
void ai_type55() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 6) return;
    switch (current_state_byte) {
    case 0: case 1:
        obj->sub_timer_max = 4;
        /* obj+0xEB = obj.hit_points;
         * obj.misc_state = 2;
         * spawn_fx(obj, 0x142, 0); */
        return;
    case 2: case 3: case 5: return;
    case 4: return;
    case 6: ai_result |= 1; return;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * ★★★ Type 56 — ТРИГГЕР ЗАВЕРШЕНИЯ УРОВНЯ (5 state-веток!) ★★★
 *
 * file 0x19A72, cs:0x17EC:0x03B2
 *
 * Особенность: использует `cmp ax, 4` (5 веток вместо обычных 7).
 *
 * Это специальный AI handler, **активирующийся при завершении уровня**:
 *
 *   INIT (0x19A94):
 *       play_sound(9)                                 // победный звук
 *       call cs:0x41D:0x206(&current_level)            // process_level_complete
 *       call cs:0x41D:0x1CF                            // save_progress
 *       call cs:0x41D:0x244                            // increment_level
 *       obj.sub_timer_max = 5
 *
 * 3 функции из сегмента 0x41D — это **внешний level-management код**,
 * вероятно:
 *   • cs:0x41D:0x206  — process_level_complete(level_num)
 *   • cs:0x41D:0x1CF  — save_progress
 *   • cs:0x41D:0x244  — increment_level_counter
 *
 * sub_timer_max=5 — handler живёт ~5 кадров и затем самоудаляется.
 * Это **самоуничтожающийся триггер**, запускаемый по достижению
 * определённой точки в уровне.
 * ═══════════════════════════════════════════════════════════════════ */
void ai_type56() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 4) return;             /* ONLY 5 states! */
    switch (current_state_byte) {
    case 0: case 1:
        play_sound(9);                              /* victory sound */
        /* process_level_complete(current_level);
         * save_progress();
         * increment_level_counter(); */
        obj->sub_timer_max = 5;
        return;
    case 2: case 3: case 4:
        return;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * ★★★ Type 57 — СТАТИЧЕСКАЯ ДЕКОРАЦИЯ (ОНЛИ 2 STATE!) ★★★
 *
 * file 0x19CC7, cs:0x17EC:0x0607
 *
 * **Самый минимальный handler из всех 57!**
 * Оба state (0 и 1) ведут в один и тот же код:
 *
 *   INIT (0x19CDF):
 *       obj.sub_timer_max  = 0xFF = 255              // НИКОГДА не тикает заметно
 *       obj.num_bboxes     = 1
 *       ax = obj.sub_timer_start (+0xC3)               // параметр от spawn
 *       ax += 0x14E                                    // = 334 + offset
 *       obj.bbox[0].sprite_id = ax                    // EXTENDED sprite!
 *       call cs:0x6E:0x52B(obj)                       // apply pos
 *       return
 *
 * НИЧЕГО БОЛЬШЕ. Это **СТАТИЧЕСКИЙ ОБЪЕКТ-ДЕКОРАЦИЯ**:
 *   • Нет HURT/DYING веток
 *   • Нет анимации
 *   • Никогда не двигается
 *   • Единственный параметр — sprite_id, вычисляемый из spawn-параметра
 *
 * Это **знаки/таблички/декоративные элементы**, размещаемые на уровнях.
 * Используется один handler для всех декораций, разные sprite_id
 * (диапазон 0x14E..0x14E+255 = 334..589) выбирают конкретный спрайт.
 *
 * Завершает список AI handler'ов на максимально элегантной ноте —
 * простейший возможный объект, всё ещё в системе AI.
 * ═══════════════════════════════════════════════════════════════════ */
void ai_type57() {
    ActiveObject* obj = current_obj;
    if (!obj) return;
    if (current_state_byte > 1) return;              /* ONLY 2 states! */

    /* Both states do the same thing — INIT is the entire behavior. */
    obj->sub_timer_max       = 0xFF;                  /* effectively eternal */
    obj->num_bboxes          = 1;
    /* sprite_id = sub_timer_start + 0x14E (range 0x14E..0x24D = 334..589) */
    obj->bboxes[0].sprite_id = static_cast<u16>(/*obj.sub_timer_start*/ 0 + 0x14E);
    /* call cs:0x6E:0x52B(obj) — apply position to bbox */
}

/* ───────────────────────────────────────────────────────────────────
 * Type 23 — преследователь (file 0x14C87, cs:0x1348:0x007)
 *
 * Двухфазное движение: горизонтальная погоня за игроком + проверка
 * на коллизии с тайлами.
 *
 * Декомпиляция:
 *   void ai_type23() {
 *       les bx, [DS:0xD95F]              ; primary_player
 *       si = player.x_world              ; +0x08
 *       les bx, current_obj              ; [DS:0xD8A6]
 *       si -= obj.x_world
 *       di = (si > 0) ? 1 : 0            ; player_to_right
 *
 *       switch (current_state_byte) {
 *       case 0,1:                         ; init
 *           obj.sub_timer_max  = 2
 *           obj.flag_2E        = 1
 *           obj.bbox[0].sprite_id = 0xB9   ; 185
 *           if (player_to_right) {
 *               obj.vx = +3                ; +0x10
 *               obj.vy =  0                ; +0x0E
 *           } else {
 *               obj.vx = -3
 *               obj.vy =  0
 *           }
 *           obj.behavior_phase = 0         ; +0xED
 *           return
 *       case 2:                            ; tick
 *           if (obj.behavior_phase != 0) {
 *               // Фаза 1: продолжать движение, проверять коллизию
 *               if (check_tile_collision(obj) == 0) {
 *                   ai_result |= 1         ; suspend (закончили)
 *               }
 *               return
 *           } else {
 *               // Фаза 0: ждать первой коллизии
 *               if (check_tile_collision(obj) != 0) {
 *                   obj.behavior_phase = 1   ; переход в фазу преследования
 *               }
 *               return
 *           }
 *       }
 *   }
 *
 * Это типичный «brick-launcher» или «ball» — движется в направлении
 * игрока, до первого касания.
 * ─────────────────────────────────────────────────────────────────── */
void ai_type23() {
    ActiveObject* obj = current_obj;
    if (!obj || !primary_player) return;

    s16 dx_player = static_cast<s16>(primary_player->x_world) -
                    static_cast<s16>(obj->x_world);
    bool player_to_right = dx_player > 0;

    if (current_state_byte == 0 || current_state_byte == 1) {
        obj->sub_timer_max          = 2;
        /* obj.flag_2E = 1; */
        obj->bboxes[0].sprite_id    = 0xB9;
        if (player_to_right) {
            /* obj.vx = +3; obj.vy = 0; */
        } else {
            /* obj.vx = -3; obj.vy = 0; */
        }
        /* obj.behavior_phase = 0; */
        return;
    }

    if (current_state_byte == 2) {
        /* В оригинале behavior_phase в +0xED. */
        bool phase1 = false;     /* obj->_padED != 0 */
        if (phase1) {
            if (check_tile_collision(obj) == 0) {
                ai_result |= 1;
            }
        } else {
            if (check_tile_collision(obj) != 0) {
                /* obj.behavior_phase = 1; */
            }
        }
        return;
    }
    /* state 3..6 — обычно no-op для этого типа */
}

} /* namespace karnov */
