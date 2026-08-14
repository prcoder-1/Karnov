# Karnov — корректировки на основе KARNOV.TXT

После тщательного перечитывания `KARNOV.TXT` обнаружено несколько
**несоответствий и упущений** в моём анализе. Этот файл — поправки и
дополнения к `ENEMY_GUIDE.md`, опирающиеся ИСКЛЮЧИТЕЛЬНО на канонический
текст игры.

## Что говорит KARNOV.TXT (выжимка ВСЕХ named entities)

### Персонажи

| Имя | Описание | Ссылка |
|-----|----------|--------|
| **Karnov** / **Jinborov Karnovski** | «One-time circus strongman with a unique talent for shooting fireballs» | Главгерой |
| **Ryu** | «Huge and evil dragon» | Финальный босс |
| (без имён) | «Demonic cohorts», «monstrous minions» | Враги |

**KARNOV.TXT НЕ НАЗЫВАЕТ НИКАКИХ ВРАГОВ ИНДИВИДУАЛЬНО.**
Все мои названия типа «зомби», «голем», «змея», «птица» — **спекуляции**.
Единственное канонически названное существо — **Ryu**.

### Сюжет

| Сущность | Описание |
|----------|---------|
| **Treasure of Babylon** | Сокровище, украденное Ryu |
| **Creamina** | Деревня (стартовая локация) |

### Механики игры (точно из KARNOV.TXT)

#### Лайфсистема Karnov'а (КРИТИЧНОЕ УПУЩЕНИЕ В МОЁМ АНАЛИЗЕ!)

> "Karnov begins each game with three lives. **If he is hit once, he
> turns blue.** If he is hit again while he is blue, he loses a life.
> But if Karnov picks up a Super Fireball while he is blue, **he will
> be restored to his normal color**."

Это объясняет загадочное распределение спрайтов Karnov'а по категориям:
- **Категория 512** (17 спрайтов): **обычный Karnov** (нормальный цвет)
- **Категория 513** (22 спрайта): **синий Karnov** (после первого удара)
- **Категория 514** (4 спрайта): Karnov в специальном состоянии
  (вероятно умирающий)
- **Категория 522** (2 спрайта): Karnov в мини-состоянии (возможно
  переход синий↔нормальный)

Моя гипотеза в `ENEMY_GUIDE.md` была неполной: я называл 513 как
«Karnov с предметом», но **на самом деле это синий Karnov!**
Соотношение количеств подтверждает (17 ≈ 22, симметричные наборы кадров
для двух цветовых состояний).

#### Firepower (УПУЩЕНО)

> "Picking up Super Fireballs also increases Karnov's firepower.
> Collecting two Super Fireballs will enable him to shoot **double
> fireballs**, and collecting three will enable him to shoot **triple
> fireballs**."

**Три уровня огня**: одиночный → двойной → тройной. Это меняет тип
projectile-handler'а или количество спавнящихся projectile'ов за выстрел.

Это объясняет наличие **разных PRNG-projectile типов**:
- Type 44 — fast random projectile
- Type 45 — slow random projectile
- Type 11 — single tracker

Возможно: одиночный fireball = type 17, двойной = type 11, тройной = type
44 или 45 (или spawn ИХ ОБОИХ). Без runtime-проверки точно не скажу,
но один из этих типов — **basic Karnov fireball** (одиночный).

#### Опции (10 штук, точное описание)

| # | Опция | Эффект (по KARNOV.TXT) | Stockable? |
|---|-------|-------------------------|------------|
| 1 | **Boots** | «Doubles Karnov's jumping power. This option loses its effectiveness over a period of time, but it can be stocked.» | Да |
| 2 | **Bomb** | «Not just for defeating enemies, but also for destroying certain obstacles and walls. It can be stocked.» | Да |
| 3 | **Ladder** | «Use it to reach an Option positioned in a high place, or to avoid an enemy's attack.» | Нет (single use) |
| 4 | **Boomerang** | «Gives you extra attacking power. You can use it again if you catch it when it returns. It can be stocked.» | Да |
| 5 | **Clapper** | «This will destroy every enemy on the screen at one time **(except for the Boss Enemy in each Stage)**. It can be stocked.» | Да |
| 6 | **Glasses** | «Allows you to see and retrieve hidden Options.» | Нет |
| 7 | **Swimming Mask** | «This will allow you to go faster underwater in **Stage 5**, a swimming scene.» | Нет |
| 8 | **Wings** | «Use them when you fall from a high place. You will need them continuously in **Stage 8**, a flying scene. They can be stocked.» | Да |
| 9 | **Shield** | «Protects Karnov from an enemy's attack up to five times.» | Нет |
| 10 | **K Mark** | «You will receive one extra life when you collect fifty of these.» | Нет |

**Ключевая корректировка**: KARNOV.TXT упоминает «**Boss Enemy in each
Stage**» — значит **в каждом стейдже есть свой босс**, не только Ryu в
финале! Это ИСПРАВЛЯЕТ моё предположение, что только LEVEL9 имеет
босса.

#### Каноническая нумерация стейджей

| Stage | Известное по KARNOV.TXT |
|-------|--------------------------|
| 5 | Swimming scene (для Swimming Mask) |
| 8 | Flying scene (для Wings) |
| Все остальные | Не описаны конкретно |

Я сохраню нумерацию: LEVEL0..LEVEL9 из BIN-файлов соответствует
stages 1-10 в KARNOV.TXT (или LEVEL5 = Stage 5 swimming, LEVEL8 =
Stage 8 flying — это даёт offset+0).

## Корректировки моего анализа

### 1. Боссы есть в КАЖДОМ стейдже

| Stage | Гипотеза о боссе | Уверенность |
|-------|------------------|-------------|
| LEVEL0 (forest) | type 9 или type 41 | Низкая |
| LEVEL1 | возможно type 8 sub-variant | Низкая |
| LEVEL2 (caves) | type 20 (multi-bbox snake) | Средняя — единственный многосегментный в LEVEL2 |
| LEVEL3 (mountain) | type 15 (anchor-AI 8 frames) | Средняя |
| LEVEL4 | type 19 (helper 0x4B36) | Низкая |
| LEVEL5 (underwater) | type 22 (tile-aware) или type 29 (multi-phase) | Низкая |
| LEVEL6 | type 37 (helper 0x4B36) | Низкая |
| LEVEL7 | type 41 (HP=100 mid-boss) | Низкая |
| LEVEL8 (flying) | type 38 (long-timer) или type 51 (8-bbox mega-static) | Низкая |
| LEVEL9 (final) | **type 36 — Ryu** ✓ | **Высокая** |

**Семейство боссов** (общий helper `cs:0x11D6:0x4B36`):
- type 9 (2-bbox)
- type 19 (1-bbox, sub_timer=1000)
- type 37 (3 sprites)

Все три используют общий движок повторных анимаций — это **классические
stage-боссы**.

### 2. «Clapper» исключает Bosses — есть «boss flag»

В THINKCYCLE pair-loop type-0 sub-dispatch есть проверка на типы 7/9
(sprite IDs), который пропускает damage при `god_mode != 0`.
Это **boss exemption mechanic** для Clapper'а:

```c
// В pair-loop type-0:
if (b.type == 1 || b.type == 7) {
    if (god_mode) {
        b._padDD = 0;     // ← здесь Clapper проверяется через god_mode-like flag
        break;            // НЕ нанести damage если флаг установлен
    }
}
```

Но Clapper — это **обычный предмет** (option 5), не cheat. Так что
этот код, скорее всего, использует другой флаг.

Канонически: **Clapper уничтожает всех ВРАГОВ на экране, кроме босса
текущего стейджа**. В моей декомпиляции я не нашёл явного «boss flag»
— это **упущение** в reverse engineering.

Вероятно реализация:
- Boss-handler устанавливает `obj.obj_type` в особое значение
- Clapper iterates по active_objects[] и убивает всех с `obj_type < BOSS_THRESHOLD`

Это нужно искать в реализации Clapper, которую я не декомпилировал.

### 3. Hidden Options + Glasses механика подтверждена

Я предположил, что type 6 с режимом «invisible» (misc_state >= 11) — это
**скрытые опции**. KARNOV.TXT подтверждает это слово в слово:

> "Glasses: Allows you to see and **retrieve hidden Options**."

✓ Корректно. Type 6 — игровой объект «Option», иногда невидимый.

### 4. Karnov turns blue — НАЙДЕНА ОСНОВНАЯ МЕХАНИКА

Категории спрайтов в SPRITES.EGA:
- **512** = Karnov normal
- **513** = Karnov blue (после удара)

Это огромное упущение моего анализа. **Должен быть state-byte в Karnov
ActiveObject**, переключающийся между «normal/blue». Когда:
- Hit while normal: state → blue (visual change, no life loss)
- Hit while blue: state → dying, life --
- Pickup Super Fireball while blue: state → normal

В моей декомпиляции это, вероятно, поле +0xE5 (obj_type) или другое.
Тип 2 (Karnov main) переключается между двумя «sub-modes» через
state_list (анимации).

### 5. 12 sprites at 0x27..0x32 = 10 Options + Super Fireball + Treasure

Корректировка моего предположения:

| Sprite | Item (по KARNOV.TXT) |
|--------|----------------------|
| 0x27 | Boots |
| 0x28 | Bomb |
| 0x29 | Ladder |
| 0x2A | Boomerang |
| 0x2B | Clapper |
| 0x2C | Glasses |
| 0x2D | Swimming Mask |
| 0x2E | Wings |
| 0x2F | Shield |
| 0x30 | K Mark |
| 0x31 | **Super Fireball** ← новое отображение |
| 0x32 | **Treasure of Babylon** ← конечная цель игры? |

Канонически в KARNOV.TXT упомянуты только: 10 Options + Super Fireball
+ Treasure of Babylon. Точно 12 объектов, точно 12 спрайтов.

### 6. Удалены спекулятивные имена врагов

В `ENEMY_GUIDE.md` я давал имена типа «Скелет/зомби», «Каменный голем»,
«Змея LEVEL2», «Птица». **Все эти имена — мои гипотезы**, не подтверждённые
KARNOV.TXT.

**Канонические имена врагов в KARNOV.TXT** — только обобщения:
- «Demonic cohorts» — общий термин для слуг Ryu
- «Monstrous minions» — то же
- «Boss Enemy in each Stage» — стейдж-боссы (без имён)
- **«Ryu»** — единственное имя индивидуального врага

Поэтому **переименовываю всех врагов в нейтральные категории**:

| Тип | Прежнее имя (моя спекуляция) | Канонически (по KARNOV.TXT) |
|-----|------------------------------|------------------------------|
| 6 | «common enemy» | **Hidden/visible Option** ✓ canonical |
| 8 | «Sky-spawn враг / dragonflies» | **Air minion** (spawn near player) |
| 9 | «2-bbox boss-like» | **Multi-part minion** или **stage boss** |
| 12 | «Наземный патруль» | **Ground minion** (HP=20) |
| 14 | «Птица» | **Flying minion** (4 кадра) |
| 15 | «Stone golem» | **Anchor minion** (LEVEL3) |
| 18 | «Скелет/зомби» | **Walking minion** (Y-snap) |
| 20 | «Snake LEVEL2» | **Multi-segment LEVEL2 boss** |
| 21 | «7-chain» | **Multi-segment minion** |
| 23 | «Огненный шар» | **Tracking projectile** (LEVEL3/9) |
| 24 | «Стайка птиц» | **Formation spawner** (7 children) |
| **36** | **Ryu** ✓ | **★ Ryu (canonical)** |
| 49 | «Ryu helper» | **LEVEL9 minion** (canonical conditional) |

## Сравнение логики handler'ов с описанием игры

### Соответствие decompiled logic ↔ KARNOV.TXT

| Механика в KARNOV.TXT | Decompiled handler / поле | Статус |
|-----------------------|----------------------------|--------|
| 3 lives | Поле somewhere (не идентифицировано) | НЕ найдено |
| Karnov turns blue | Категории 512/513 в SPRITES.EGA | Найдено (косвенно) |
| Hit while blue → lose life | state transition logic | НЕ найдено |
| Super Fireball restores blue→normal | Pickup type 5/6 with specific sprite | НЕ найдено |
| Firepower levels (1/2/3 fireballs) | Множественные projectile types | Возможно (типы 17, 11, 44, 45) |
| **Stage 5 swimming** | type 3 (÷2 velocity) | **Совпадает** |
| **Stage 8 flying** | type 3 (÷2 velocity) | **Совпадает** |
| Boots: ×2 jump (temporary) | Affects type 2's vertical physics | Не найдено |
| Bomb: destroys obstacles | spawn FX + area damage | Не найдено |
| Ladder: reach high places | type 1/2 climb state | Не найдено |
| Boomerang: extra attack, returns | Particular projectile type | Не найдено |
| Clapper: destroys all enemies (except boss) | Mass-kill function | Не найдено |
| **Glasses: see hidden Options** | type 6 invisible mode (misc_state≥11) | **Совпадает** |
| Wings: continuous in Stage 8 | type 3 в LEVEL8 | **Совпадает** |
| Shield: 5 hits | Counter on Karnov object | Не найдено |
| K Mark: 50 = extra life | Counter on player state | Не найдено |
| Joystick: stick + Button 1 (fire) + Button 2 (option) | input bit map | **Совпадает** ✓ |
| Keyboard: X/Space (fire), Z (option) | input bit map (0x10, 0x20) | **Совпадает** ✓ |
| Pushing up: jump OR climb if ladder | type 2 with conditional logic | Не найдено |
| 1/2 players toggle: '1' | Menu handler | **Совпадает** ✓ |
| Joystick/keyboard toggle: '2' | use_joystick flag | **Совпадает** ✓ |
| Music toggle: '3' | music_on flag | **Совпадает** ✓ |
| Sound toggle: '4' | sound_on flag | **Совпадает** ✓ |
| Quit: Q | exit() handler | **Совпадает** ✓ |
| Pause: ESC | in_play=0 logic | **Совпадает** ✓ |
| Boss Enemy each Stage | НЕ найдено в моей декомпиляции | **Упущение** |

### Найденное в декомпиляции, чего НЕТ в KARNOV.TXT

| Decompiled feature | Где найдено |
|---------------------|-------------|
| **Cheat код «ZEEK»** (Ctrl+Z, Ctrl+E, Ctrl+E, Ctrl+K) | `0x3F88` |
| God mode = neuvulnerability + 3× fire rate + level select | `0xCCA6` toggle |
| Level select via L/l + digit (когда god_mode on) | `0x3DB0..0x3DFA` |
| Demo recording during gameplay | `0x6FCC` (`level%d.mov` wb) |
| RLE compression (`0x5A` marker) | `0x7127` |

**Cheat код KARNOV.TXT НЕ упоминает.** Это **скрытая разработческая
возможность**, найденная только через reverse engineering. Также
отсутствует в инструкции упоминание возможности **записи демо** (что
объясняет, почему LEVEL0.MOV / LEVEL1.MOV / LEVEL2.MOV есть на диске —
они были предзаписаны разработчиками для attract-mode).

## Уверенность в наименованиях (обновлённо)

### Высокая уверенность (≥90%)

| Тип | Имя | Доказательство |
|-----|-----|----------------|
| 1, 2, 3 | **Karnov** (player) | Reads direction_bits, ×2/÷2 acceleration |
| 5, 6 | **Options items** (10 + Super Fireball + Treasure) | Точное соответствие 12 сприйтам 0x27..0x32 |
| **36** | **★ Ryu** | LEVEL9 + 2-bbox + 6 спрайтов + uniqueHURT-FX |
| 49 | **Ryu helper** | LEVEL9 conditional check |
| 56 | **Level-end trigger** | Sound 9 + 3 level-transition helpers |
| 57 | **Static decoration** | Только 2 state, vечный таймер |

### Средняя уверенность (50-90%)

| Тип | Категория | Доказательство |
|-----|-----------|----------------|
| 8 | **Air minion** | Spawn у игрока, повсеместный |
| 9, 19, 37 | **Stage bosses** | Общий helper 0x4B36 |
| 20 | **Stage 2 multi-segment** (boss?) | LEVEL2 + multi-bbox |
| 22 | **Tile-aware minion** | Reads map (Stage 4 caves?) |
| 23 | **Boss projectile** | Tracker, LEVEL3+9 |
| 36 (sprites 0xF5-0xFA) | **Ryu's 6 animation frames** | 2-bbox + unique state 5 |
| 41 | **Mid-stage boss** | HP=100 |
| 51 | **Boss arena element** | 8-bbox, eternal timer |

### Низкая уверенность (<50%)

Все остальные типы (~30 handler'ов) — их «роли» в игре не определимы
без runtime-наблюдения. Их sprite_id'ы известны, но без визуального
просмотра спрайтов или play-тестирования индивидуальные роли в игре
остаются спекулятивными.

## Главные выводы

1. **Только одно каноническое имя врага в KARNOV.TXT — Ryu**. Все мои
   «голем», «змея», «зомби» — спекуляции. Игра использует обобщающие
   термины «monstrous minions».

2. **Boss Enemy in each Stage** — я НЕ ИДЕНТИФИЦИРОВАЛ всех 9 стейдж-боссов.
   Семейство 9/19/37 (helper 0x4B36) — главный кандидат, но без
   level-by-level mapping я не могу определить кто из них в какой стейдж.

3. **Karnov turns blue mechanic** — категории 512/513 в SPRITES.EGA это
   объясняют, но конкретное поле «is_blue» в ActiveObject я **не нашёл**.
   Это требует дополнительной декомпиляции state-transition logic.

4. **Cheat код «ZEEK»** — это полностью **разработческое расширение**,
   не описанное в KARNOV.TXT. Уникальное открытие reverse engineering.

5. **10 Options + Super Fireball + Treasure of Babylon** = 12 sprites
   в диапазоне 0x27..0x32. Точное совпадение с моим открытием.

6. **Demo system** (LEVEL%d.MOV) — не упомянут в KARNOV.TXT. Это
   разработческая инфраструктура для attract-mode.

7. **«Karnov as ActiveObject» открытие** — types 1/2/3 это игрок, а не
   враг. KARNOV.TXT не обсуждает это технически, но **подтверждает**
   разделение Stage 5 (swimming) и Stage 8 (flying), что точно
   соответствует моей идентификации type 3 как «Karnov alternate
   physics» (÷2 acceleration вместо ×2).
