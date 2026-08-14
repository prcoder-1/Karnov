# DECOMPILE_NOTES — таблица соответствий

Карта «адрес в `KARNOV.EXE` → имя в C++ реконструкции».

Адреса даны как `cs:offset` (далёкие вызовы) или `file 0xXXXXX` (смещение в
сыром EXE). Сегментная компоновка Borland Turbo-C compact:

* Стартовый CS = `0x2200`; заголовок 6144 байт (`0x1800`). Точка входа в
  файле — `0x1800`.
* DS = `0x1D99` (загружается в `cs:0x1801`). DS-сегмент в файле начинается
  с `0x1F190`.

## Подсистемы

### File I/O (RLE-аware)

| EXE | Файл/Смещение | Имя в C++ |
|-----|----------------|------------|
| `cs:0x58B:0x006` | `file 0x70B6` | `bin_fopen()` |
| `cs:0x58B:0x04C` | `file 0x70FC` | `bin_fclose()` |
| `cs:0x58B:0x10B` | `file 0x7116` | `bin_reset()` |
| `cs:0x58B:0x077` | `file 0x7127` | `bin_fread()` (RLE-распаковка) |
| `cs:0x58B:0x135` | `file 0x71E5` | `read_byte()` (буфер 512 байт) |

Глобальное состояние декомпрессора:

| Адрес | Имя в C++ | Назначение |
|-------|-----------|-----------|
| `DS:0xD568` | `rle_in_run`   | флаг «в режиме повтора» |
| `DS:0xD76A` | `rle_run_byte` | повторяемый байт |
| `DS:0xD76D` | `rle_run_left` | оставшийся счётчик повторов |
| `DS:0xD566` | `io_buf_avail` | сколько байт в буфере |
| `DS:0xD76B` | `io_buf_pos`   | позиция чтения |
| `DS:0xD56A` | `io_buffer[]`  | 512-байтный буфер |

### Загрузка уровня

| EXE | Имя в C++ |
|-----|-----------|
| `cs:0x5350` | `load_level()` |
| `cs:0x51B6` | `read_object_regions()` |
| `cs:0x5216` | `read_enemies()` |

### Демо

| EXE | Имя в C++ |
|-----|-----------|
| `cs:0x6CB9` | `is_demo_at_end()` |
| `cs:0x6D00` | `demo_reset()` |
| `cs:0x6D27` | `demo_advance()` |
| `cs:0x6D5D` | `demo_record_event()` |
| `cs:0x6DC0` | `tick_demo()` |
| `cs:0x6E16` | `load_demo()` |
| `cs:0x6FCC` | `save_demo()` |

### Ввод

| EXE | Имя в C++ |
|-----|-----------|
| `cs:0x394C` | `read_joystick()` |
| `cs:0x39CA` | `read_keyboard()` |
| `cs:0x3C50`–`0x3D04` | `process_input()` |
| `cs:0x3BFE` | `handle_menu_keys()` (частично, основной диспетчер) |

### Кадровый диспетчер (главный игровой цикл)

Главный цикл — функция по адресу `file 0x6966` (cs:0x6E:0x166). Внутри
один проход кадра выполняет 11 фаз через FAR-вызовы:

| # | FAR call | file_off | Имя фазы |
|---|----------|----------|----------|
| 1 | `cs:0x6E:0x6D3`  | `0x025B3` | UPDATELOCS |
| 2 | `cs:0x6E:0xCDB`  | `0x02BBB` | POSTHITS |
| 3 | `cs:0x20F:0x2FB` | `0x03BEB` | DOCONTROLS |
| 4 | `cs:0x365:0x1C1` | `0x051D1` | CHECKTRIGGERS |
| 5 | `cs:0xFFA:0x1A1A`| `0x125BA` | PRE-KARNOV |
| **6** | **`cs:0xD4B:0xE03`** | **`0x0FAB3`** | **THINKCYCLE** |
| 7 | `cs:0xFFA:0x1C04`| `0x127A4` | POST-KARNOV |
| 8 | `cs:0x15F:0x296` | `0x03086` | SCROLLVIEW |
| 9 | `cs:0x6E:0xC37`  | `0x02B17` | DRAWCYCLE (условный) |
| 10| `cs:0x15F:0x195` | `0x02F85` | SCROLLUPDATE (условный) |
| 11| `cs:0x6E:0x846`  | `0x02726` | ERASECYCLE (условный) |

Условные (9–11) выполняются только если `[DS:0xCE02] != 0` (в игре, не на паузе).

### THINKCYCLE — AI врагов (детальная декомпиляция)

| EXE | Имя в C++ | Описание |
|-----|-----------|---|
| `cs:0xD4B:0xE03` (file `0x0FAB3`) | `think_cycle()` | Главный AI-тик |
| `cs:0xD4B:0xBD5` (file `0x0F885`) | `ai_dispatch()` | Диспетчер state-машины объекта |
| `cs:0xD4B:0xB6E` (file `0x0F81E`) | `check_collision()` | Пересечение bbox двух объектов |
| `cs:0xD4B:0x1124` | `play_sound()` | Звук |
| `cs:0x6E:0x4B4` (file `0x02394`) | `process_object_chain()` | Обход цепочки активных объектов |
| `cs:0x6E:0x21C` (file `0x021DC`) | `update_world_bboxes()` | Пересчёт мировых координат боксов |
| `cs:0x6E:0x14E` (file `0x02106`) | (часть update_players) | Per-player диспетчер |

#### Архитектура объекта (см. `include/think.h`)

* Каждый активный объект — структура **245 байт (`0xF5`)** в массиве
  `DS:0x2824`, индексируется uint16: `ptr = DS:(idx * 0xF5 + 0x2824)`.
* Поля:
  | смещ. | назначение |
  |-------|-----------|
  | `+0x00..0x1F` | заголовок (x_screen, y_screen, x_world, y_world, num_bboxes) |
  | `+0x1E` | num_bboxes (количество спрайт-боксов) |
  | `+0x20..0xAF` | массив `SpriteBox` (до 8 шт. × 18 байт) |
  | `+0xB0` | alive_flag (1=активен) |
  | `+0xB2` | next_idx (связный список активных) |
  | `+0xB4` | prev_idx |
  | `+0xB6` | state_byte (0..5) |
  | **`+0xB7/+0xB9`** | **FAR-указатель на AI handler** |
  | `+0xBD/+0xBF` | far ptr на ai_init (для reload при state==1) |
  | `+0xC5` | sub_timer |
  | `+0xC9` | sub_timer_max |
  | `+0xCB` | collision_cnt (s16, decrement каждый тик) |
  | `+0xCD/+0xCF` | far ptr на текущий анимскрипт |
  | `+0xD5/+0xD7` | far ptr на массив `StateScript` (список состояний) |
  | `+0xD9` | next_state_id (id перехода) |
  | `+0xDB` | active_flag |
  | `+0xDD` | сбрасывается в pair-loop |
  | `+0xE5` | obj_type (0..4 — управляет диспетчером в pair-loop) |
  | `+0xE7` | hit_points (s16) |

#### Глобальные переменные AI

| DS | C++ | назначение |
|----|-----|-----------|
| `0x271E` | `num_pair_entries` | размер pair table (×2 от числа пар) |
| `0x2722` | `pair_table[]` | пары индексов для коллизий |
| `0x2594` | `active_index[]` | массив активных индексов |
| `0x2694` | `active_count[2]` | счётчики (по 1 на игрока) |
| `0x2698` | `dead_chain[]` | dead-list |
| `0x2824` | `all_objects[]` | массив структур ActiveObject |
| `0xA2AE` | `dead_count` | размер dead-list |
| `0xCCA6` | `god_mode` | флаг неуязвимости |
| `0xD780` | `revival_list[]` | очередь оживления в текущем кадре |
| `0xD8A2` | `current_state_byte` | копия `obj.state_byte` для AI |
| `0xD8A6/0xD8A8` | `current_obj` | far ptr на обрабатываемый объект |
| `0xD8BC/0xD8BE` | `player_cache_x/y` | кэш позиции игрока |
| `0xD88C` | `pending_state_id` | целевое состояние при переходе |
| `0xD953` | `revival_count` | счётчик revival в кадре |
| `0xD955` | `frame_timer` | пер-кадровый таймер |
| `0xD959` | `target_idx` | индекс выбранной цели |
| `0xD963` | `ai_result` | флаг результата AI handler (1=suspend, 2=transition) |

#### Логика THINKCYCLE

**Фаза A — pair-коллизии** (cs:0xFAC1..0xFCEB): итерирует `pair_table`
парами `(a_idx, b_idx)`, диспетчер по `a.obj_type` через jump-table в
`cs:0xE8A`:
- `type==0` (только уровень 6): второй jump-table `cs:0xEB8` по `b.type-1`
- `type==2`: третий jump-table `cs:0xF56` по `b.type-1` (урон от удара
  игрока: вычитание HP, переход в state=4 при смерти)
- остальные типы (1, 3, 4): no-op (только сброс `+0xDD`)

**Фаза B — per-player AI** (cs:0xFCF0..0xFDA8): для каждого из 2 игроков
обходит цепочку активных объектов через `active_index[]`. Для каждого
объекта с установленным `ai_handler_off|seg`:
- Декрементирует `collision_cnt` (+0xCB)
- Если `collision_cnt > 0` и `state == 2` — пропускает кадр
- Иначе восстанавливает `collision_cnt` из `sub_timer_max` и вызывает `ai_dispatch()`

**Фаза C — revival** (cs:0xFDAD..0xFDCC): для каждого индекса в
`revival_list` вызывает `cs:0x6E:0x226` (восстановление объекта).

#### `ai_dispatch` (cs:0xF885)

State-машина с jump-table в `cs:0xC32` (6 веток):
- state 0/1: инициализация полей (`+0xDB=1`, `+0xCB=0`, `+0xC9=1`, `+0xD9=0x3E7`, `state_byte=2`)
- state 1/3: дополнительно перезагрузка handler из `+0xBD/+0xBF` → `+0xB7/+0xB9`
- state 2: тикает `sub_timer` и проверяет против `*state_off` (timing анимации)
- state 4/5: прямой вызов handler

Сам вызов — `call far [obj+0xB7]` в `cs:0xF949`. Handler пишет результат
в `ai_result`:
- bit 0 → объект suspended, его `next_idx` добавляется в `revival_list`
- bit 1 → переход в новое состояние из массива `obj.state_list`,
  индексируемого `pending_state_id` (× 8 = размер StateScript)

### Этапы кадра (имена строк в DS:0x318..0x389)

| EXE | DS-строка | Имя в C++ |
|-----|-----------|-----------|
| `DS:0x318` | `UPDATELOCS`    | `update_locs()`     |
| `DS:0x323` | `POSTHITS`      | `post_hits()`       |
| `DS:0x32C` | `DOCONTROLS`    | `do_controls()`     |
| `DS:0x337` | `CHECKTRIGGERS` | `check_triggers()`  |
| `DS:0x345` | `PRE-KARNOV`    | `pre_karnov()`      |
| `DS:0x350` | `THINKCYCLE`    | `think_cycle()`     |
| `DS:0x35B` | `POST-KARNOV`   | `post_karnov()`     |
| `DS:0x367` | `SCROLLVIEW`    | `scroll_view()`     |
| `DS:0x372` | `DRAWCYCLE`     | `draw_cycle()`      |
| `DS:0x37C` | `SCROLLUPDATE`  | `scroll_update()`   |
| `DS:0x389` | `ERASECYCLE`    | `erase_cycle()`     |

### Инициализация и игровой цикл

| EXE | Назначение | Имя в C++ |
|-----|------------|-----------|
| `cs:0x4BE0` | sequential init (bk, level, sprite) | `init_all_subsystems()` |
| `cs:0x4C5A` | обрамляющая функция (старт уровня) | `run_game()` (упрощённо) |
| `cs:0x6E:0x2D0` (`file 0x21B0`) | update_players | `update_players()` |
| `cs:0x6E:0x0D1` | post-init  | (TODO) |
| `cs:0x15F:0x646` | bk_init | (TODO) |
| `cs:0x365:0x00B` | level_init | (TODO) |
| `cs:0xBFB:0x004` | sprite_init | `sprite_init()` |
| `cs:0xBFB:0x0B2` | splash | `splash_screen()` |

### Видео

| EXE | Назначение | Имя в C++ |
|-----|------------|-----------|
| `cs:0x15F:0x91`  | clear_screen | `clear_screen()` |
| `cs:0x4E4:0x0B`  | draw_text    | `draw_text()` |
| Меню `C/E/T/M`  | DS:0x573, 0x597..0x5CD | `prompt_video_mode()` |
| Автодетект     | DS:0x2B6, 0x2BC, 0x2C5, 0x2CD | `autodetect_video_mode()` |

## Глобальное состояние (DS)

| Адрес | C++ имя | Описание |
|-------|---------|----------|
| `DS:0x098`  | `map_buffer`        | far ptr на буфер карты |
| `DS:0x354`  | `music_on`          | флаг музыки |
| `DS:0x356`  | `sound_on`          | флаг звука |
| `DS:0x358`  | `use_joystick`      | 0=keyboard, 1=joystick |
| `DS:0x35A`  | `num_players`       | 1 или 2 |
| `DS:0x35C`  | `in_play`           | в игре (не в меню) |
| `DS:0x360`  | `level_mode`        | 0/2/3 — режим загрузки уровня |
| `DS:0x47C`  | `level_objects`     | far ptr на ObjectRegion[] |
| `DS:0x48C`  | `obj0_x1`           | копия из level_objects[0].x1 |
| `DS:0x48E`  | `obj0_y1`           | копия из level_objects[0].y1 |
| `DS:0x494`  | `level_enemies`     | far ptr на EnemySpawn[] |
| `DS:0xB82A` | `tile_pixels`       | far ptr на графику тайлов |
| `DS:0xB830` | `num_unique_tiles`  | количество тайлов |
| `DS:0xCC96` | `tile_size_words`   | размер тайла в WORD-ах |
| `DS:0xCC98` | `tile_size_bytes`   | то же × 2 |
| `DS:0xCC9C` | `key_counter`       | счётчик длительности удержания |
| `DS:0xCC9E` | `input_state`       | текущая маска ввода |
| `DS:0xCC9F` | `last_key_code`     | последний ASCII |
| `DS:0xCCA0` | `prev_input_state`  | (предыдущий) |
| `DS:0xCCA1` | `input_history`     | архив |
| `DS:0xCCA2` | `option_pressed`    | бит OPTION |
| `DS:0xCCA8` | `raw_keyboard_state`| накопитель в read_keyboard |
| `DS:0xCCAA` | `fire_pressed`      | бит FIRE |
| `DS:0xCCAC` | `direction_bits`    | биты направления |
| `DS:0xCCAE` | `direction_changed` | изменилось ли направление |
| `DS:0xCCCA` | `hint_counter`      | счётчик подсказки |
| `DS:0xCDEA` | `current_frame`     | текущий игровой кадр |
| `DS:0xCDEC` | `in_game`           | флаг |
| `DS:0xCDEE` | `video_mode`        | выбранный режим |
| `DS:0xCDF0` | `scroll_counter`    | |
| `DS:0xCDF4` | `bg_palette`        | |
| `DS:0xCDFA` | `num_level_enemies` | |
| `DS:0xCDFE` | `num_level_objects` | |
| `DS:0xCE00..CE04` | `hud_state[]` | UI |
| `DS:0xCF4E` | `lives_counter`     | |
| `DS:0xD55C` | `demo_play`         | far ptr |
| `DS:0xD560` | `demo_record`       | far ptr |
| `DS:0xD8AA` | `current_level`     | 0..9 |
| `DS:0xD8B0` | `vert_dir_cache`    | up/down кэш |
| `DS:0xD8C8..D8CC` | `level_state_a/b/c` | |
| `DS:0xD8CE` | `max_level_reached` | 9 → конец |

## Прокрутка экрана (SCROLLVIEW + SCROLLUPDATE)

### Архитектура

Двухфазная схема, типичная для DOS-эры:

* **SCROLLVIEW** (`cs:0x15F:0x296`, file `0x3086`) — каждый кадр, безусловно.
  Логическая работа: считает дельту камеры (`view_x` − `prev_view_x`),
  конвертирует пиксельные координаты в тайловые через таблицы,
  перерисовывает «раскрывшийся» край карты (только новые тайлы).
* **SCROLLUPDATE** (`cs:0x15F:0x195`, file `0x2F85`) — только в игре.
  Аппаратная часть: переключает CGA-видеостраницу или
  blit'ит подготовленный буфер в `B800:0` (см. `cs:0x5A3:0x9`).

### Глобальное состояние

| DS | C++ имя | Назначение |
|----|---------|-----------|
| `0xCC8E/0xCC90` | `view_x/view_y` | Текущая позиция камеры (пиксели мира) |
| `0xBA70/0xBA72` | `prev_view_x/prev_view_y` | Камера на предыдущем кадре |
| `0xB81E..0xB824` | `prev_tile_box[4]` | Прежний bbox в тайлах |
| `0xA2B4..0xA2BA` | `cur_tile_box[4]` | Новый bbox в тайлах |
| `0xA2B0/0xA2B2` | `anchor[2]` | Якорь буфера |
| `0xABB8` | `pt_workspace[]` | Промежуточный буфер для `pt_copy` |
| `0xAD50` | `video_offset` | Текущее смещение видеостраницы |
| `0xBA64` | `video_segment` | Сегмент off-screen буфера |
| `0xBB5E` | `px_to_tile_x[]` | LUT: pixel-X → tile-X (адр. `[bx*2 - 0x44A2]`) |
| `0xAF22` | `px_to_tile_y[]` | LUT: pixel-Y → tile-Y (адр. `[bx*2 - 0x50DE]`) |

### Вспомогательные функции

| EXE | Имя в C++ | Роль |
|-----|-----------|------|
| `cs:0x287:0x06` (file `0x4076`) | `draw_tile(tx, ty)` | Один тайл в видеопамять, использует `tile_pointers` из level loader |
| `cs:0x5A3:0x09` (file `0x7239`) | `scroll_blit(off)` | Копирование в CGA `B800:0`, чередование плоскостей 0x0000/0x2000 |
| `cs:0x2B5:0x9D` (file `0x3CED`) | `pt_copy(dst, ws, src)` | Копирование 8 байт (4 WORD'а) через буфер |

### SCROLLUPDATE jump-table (`cs:0x15F:0x1A9`, file `0x2F99`)

4 ветки по `level_mode`:

| mode | offset | арг для `scroll_blit` |
|------|--------|----------------------|
| 0    | 0x01B1 | `level_mode \| 0x8000` |
| 1    | 0x01CE | `video_offset + 0x4000` |
| 2    | 0x01B1 | `level_mode \| 0x8000` (та же, что 0) |
| 3    | 0x01C1 | `video_offset` |

`0x8000/0x4000` — высокобайтовые флаги «какая CGA-плоскость».

### SCROLLVIEW: пословный алгоритм

```
delta_x = view_x - prev_view_x
delta_y = view_y - prev_view_y
pt_copy(prev_tile_box, workspace, &prev_view_x)   ; обновить prev_*
pt_copy(cur_tile_box,  workspace, &view_x)         ; обновить cur_*

; Конвертация через LUT
prev_tx0 = px_to_tile_x[prev_tile_box[0]]
... (и т.д., 8 значений: tx0/ty0/tx1/ty1 для prev и cur,
     причём tx1/ty1 — px_to_tile_*[edge - 1] + 1)

if (delta_x != 0):
    if (delta_x < 0): from = prev_tx1; to = cur_tx1   ; перерисовать ПРАВЫЙ край
    else:             from = cur_tx0;  to = prev_tx0  ; перерисовать ЛЕВЫЙ край
    for sx in [from, to):
        for sy in [prev_ty0, prev_ty1):
            draw_tile(sx, sy)
    ; затем inc-копирование области (cs:0x31B6)

if (delta_y != 0):
    ; симметричный блок для Y (cs:0x31E5+)
```

## Конкретные AI-скрипты по типу врага (`call far [+0xB7]`)

### Диспетчер: spawn → handler

Spawn-функция (`file 0x4F30..0x500F`, в окрестности уровневого
загрузчика) преобразует `EnemySpawn` (20 байт из BIN) в активный
объект (245 байт в `all_objects[]`).  Главный шаг:

```c
spawn_active_object(EnemySpawn* spawn, ActiveObject* obj):
    obj.obj_type     = spawn[2]              // subtype → +0xE5
    obj.hit_points   = spawn[0x11]           // HP → +0xE7
    obj.x_world      = spawn[0xC]            // → +0x08
    obj.y_world      = spawn[0xE]            // → +0x0C
    obj.num_bboxes   = 1
    obj.bbox[0].xoff = 0
    obj.bbox[0].yoff = 0
    init_arg         = spawn[0x10]           // → sub_timer
    bx              = spawn[0] * 4           // PRIMARY type → table index
    handler_far_ptr = (DS:[bx+0x10FE], DS:[bx+0x1100])
    init_active_object(obj, handler_far_ptr, init_arg)
    spawn[0x12]      = 1                      // mark activated
```

**`init_active_object`** (cs:0xD4B:0xAF5, file `0xF7A5`) сохраняет
`handler_far_ptr` сразу в ДВА слота:
- `+0xB7/+0xB9` — текущий AI handler
- `+0xBB/+0xBD` — default handler (для возможного «жёсткого» reset)
- `+0xC3/+0xC5` — sub_timer и его текущее значение

**`queue_state`** (cs:0xD4B:0xB31, file `0xF7E1`) — переход в новое
состояние:
- Сохраняет new_handler в `+0xBF/+0xC1`
- Ставит `state_byte = 1` (триггер reload в `ai_dispatch`)
- При нуле — также прописывает в `+0xB7/+0xB9` (первый запуск)

### Таблица handler'ов (DS:0x10FE, file 0x2028E)

Извлечено **57 уникальных handler'ов** (тип 0 = NULL).
Полный список — в `src/ai_scripts.cpp`. Краткие данные:

| Type | EXE addr | file | Появляется в |
|------|----------|------|---|
| 1 | cs:0xE62:0xC49 | 0x10A69 | базовый |
| 2 | cs:0xE62:0x13C6 | 0x111E6 | LEVEL2 |
| 6 | cs:0xFFA:0xEB8 | 0x12658 | LEVEL0 (общий враг) |
| 8 | cs:0xFFA:0x139F | 0x12B3F | LEVEL1+ (boss-like) |
| 15 | cs:0x11D6:0xACB | 0x1402B | LEVEL3 |
| 20 | cs:0x1348:0x6DD | 0x1535D | LEVEL2 |
| 23 | cs:0x1348:0x007 | 0x14C87 | LEVEL3/9 (преследователь) |
| 36 | cs:0x151A:0x61B | 0x16FBB | LEVEL9 (вероятно Ryu) |

Распределение по сегментам:

| Segment | Types | Кол-во |
|---------|-------|--------|
| 0x0E62 | 1–3 | 3 |
| 0x0FFA | 4–8 | 5 |
| 0x11D6 | 9–19 | 11 |
| 0x1348 | 20–32 | 13 |
| 0x151A | 33–38 | 6 |
| 0x161F | 39–42 | 4 |
| 0x1692 | 43–47 | 5 |
| 0x170C | 48–49 | 2 |
| 0x175E | 50–52 | 3 |
| 0x17B6 | 53–54 | 2 |
| 0x17EC | 55–57 | 3 |

### Шаблон handler'а

Каждый handler — функция вида:
```c
void ai_typeN(void) {           // зовётся как call far [obj+0xB7]
    obj = current_obj;          // [DS:0xD8A6]
    state = current_state_byte; // [DS:0xD8A2]
    if (state > MAX_STATE) return;
    jmp [cs:state*2 + JMP_TABLE]    // jump-table по state_byte
}
```

Типовые ветки:
- **state 0,1** — init: установка полей `bbox[0].sprite_id`, `sub_timer_max`,
  `vx/vy`, `behavior_phase=0` и т.п.
- **state 2** — tick: воспроизведение анимации. На последнем кадре
  (`sub_timer == anim_frames - 1`) вызывается `queue_state(obj, next_handler)`.
- **state 3,5** — промежуточные/выходы.
- **state 4** — "hurt" (устанавливается pair-loop type-0 при попадании).
- **state 6** — "dying" (после анимации смерти ставит `ai_result |= 1`,
  что добавляет объект в `revival_list` через ai_dispatch).

Сигналы возврата через `ai_result` (DS:0xD963):
- bit 0 → объект приостановлен, индекс в `revival_list`
- bit 1 → переход в новое состояние из `obj.state_list[pending_state_id]`
- bit 2 → "animation tick" (внутренний)

### Декомпилированные примеры (см. src/ai_scripts.cpp)

* **`ai_type01`** (file 0x10A69) — простейший шаблон: init сбрасывает
  sub_timer и sprite, tick проигрывает анимацию, в конце → `queue_state`
  на тип 4 (`cs:0xFFA:0xCF6`, file 0x12496).

* **`ai_type02`** (file 0x111E6) — **handler Карнова (нормальная физика)**:
  Это НЕ враг! Читает `DS:0xCCAC` (direction_bits ИГРОКА) и
  обновляет 32-битные fixed-point скорости через Turbo-C runtime
  `_lshift_long(cx=1)` (= ×2). 4 ветки: state 1 (init + falls through
  в TICK), state 2 (TICK с движением), state 3 (UPDATE_DIR_FLAGS —
  итерация по таблицам DS:0xD8DA и DS:0x15C0), state 4 (DESTROY →
  `cs:0xE62:0x51E`). Анимации в state_list по DS:0x15A6.

* **`ai_type03`** (file 0x112F7) — **handler Карнова (альтернативная физика)**:
  Зеркальная функция type 2, но:
   - скорости ДЕЛЯТСЯ пополам через `_rshift_long(cx=1)` (вместо ×2)
   - есть счётчик `misc_state` (+0xE9) с wrap-аround на 6
   - vy маскируется по 0x8000 (только знак)
   - переход через `[DS:0xD8BA]` (флаг "в воздухе")
   - state 3 — вложенный диспетчер на 7 sub-веток (sub-table cs:0xE62:0x1643)
   - анимации по DS:0x160A (другой набор)

  Скорее всего это режим плавания (LEVEL 5) или полёта (LEVEL 8),
  упоминаемые в `KARNOV.TXT`: «swimming scene» и «flying scene» с
  предметами «Swimming Mask» и «Wings».

* **`ai_type06`** (file 0x12658) — рандомизированный спавн: вызывает
  RNG (cs:0xD4B:0x105), по seed выбирает один из 11+ вариантов sprite_id
  (диапазон 0x27..0x32), либо невидимое состояние (`num_bboxes=0`).
* **`ai_type23`** (file 0x14C87) — преследователь: каждый init читает
  положение игрока (`DS:0xD95F` — primary_player) и задаёт `vx = ±3`
  в его направлении. tick проверяет коллизию с тайлами через
  `cs:0x6E:0x7A5`. Двухфазное: «полёт» → «попадание».

### Открытие: типы 1–3 — это Карнов

Декомпиляция handler'ов type 2 и 3 показала, что они работают с
direction_bits ИГРОКА (`DS:0xCCAC`), а не с AI. Это значит:

* **Игрок Карнов** — это один из активных объектов в общем массиве, и
  его «AI handler» — это просто **обработчик ввода + физика**.
* Type 1 (cs:0xE62:0xC49) — вероятно Карнов-в-покое (idle)
* Type 2 (cs:0xE62:0x13C6) — Карнов нормальный (бег + прыжки)
* Type 3 (cs:0xE62:0x14D7) — Карнов в воде/воздухе (плавание/полёт)
* Types 4–57 — настоящие враги

Это объясняет, почему таблица AI хэндлеров содержит спецслучаи для
"Карнов-подобных" типов в pair-loop коллизий THINKCYCLE (cs:0xFB44+):
обе стороны коллизии могут быть «активные объекты», и игрок — тоже.

## Чит-код «god mode» (Ctrl+Z, Ctrl+E, Ctrl+E, Ctrl+K)

Скрытая 4-клавишная последовательность, переключающая флаг `DS:0xCCA6`
(`god_mode`). Состояние ввода хранится в счётчике `DS:0xCCCA`:

| Шаг | Клавиша | ASCII | Условие `[CCCA]` | Действие |
|-----|---------|-------|------------------|----------|
| 1 | Ctrl+Z | 0x1A | `==0` | `CCCA = 1` |
| 2 | Ctrl+E | 0x05 | `==1` | `CCCA = 2` |
| 3 | Ctrl+E | 0x05 | `==2` | `CCCA = 3` |
| 4 | Ctrl+K | 0x0B | `==3` | **`god_mode ^= 1`**, `play_sound(6)`, `CCCA = 0` |

Любая «неподходящая» клавиша → `CCCA = 0`. На шаге 4 звук 6 играется в
любом случае — слышимое подтверждение успеха.

Адреса:
* `cs:0x3F5E` — case Ctrl+Z (шаг 1)
* `cs:0x3F6A` — case Ctrl+E (шаги 2–3)
* `cs:0x3F81` — case Ctrl+K (шаг 4, **сам XOR — `cs:0x3F88`**)
* `cs:0x3D40..0x3DAD` — главный диспетчер клавиш меню/игры
* `cs:0x38FA` — `read_key()` (читает через BIOS, scancode → `[0xCC9C]`)
* `cs:0x06C92` — единственное место сброса в `0` (старт новой игры)

Эффекты `god_mode` (3 читателя помимо XOR):

| Адрес | Эффект |
|-------|--------|
| `0x03DB0` | Разблокирует **level select**: `L`/`l` + цифра 0–9 → `cs:0xE62:0x683` (`load_level`) |
| `0x0FB8A`, `0x0FBAE` | В THINKCYCLE pair-loop type 0 (уровень 6): пропускает урон Карнову |
| `0x1302C` | Кулдаун стрельбы: `god_mode? 1 : 3` тика (× 3 быстрее) |

## Сообщения

| Адрес | Текст |
|-------|-------|
| `DS:0x0DA` | "video device error" |
| `DS:0x0ED` | "Not enough memory for buffer" |
| `DS:0x10A` | "Not enough memory for map" |
| `DS:0x126` | "Exit to DOS (Y/N)?" |
| `DS:0x3A8` | "bkinit failed!" |
| `DS:0x3B8` | "level init failed!" |
| `DS:0x3CC` | "SpriteInit failed!" |
| `DS:0x37D` | "Please insert data disk" |
| `DS:0x395` | "press key when ready" |
| `DS:0x498` | "level%1d.bin" |
| `DS:0x4A5/0x4A8` | "rb" |
| `DS:0x573` | "press key for desired video mode" |
| `DS:0x597..0x5CD` | "C - CGA", "E - EGA", "T - TGA", "M - Monochrome Graphics" |
| `DS:0x5F0` | "can't force Hercules mode" |
| `DS:0x649` | "sprite bummer #%d" |
| `DS:0xC6D` | "Karnov (C) 1987 Data East USA Inc." |
| `DS:0xC90` | "Adaptation by Quicksilver Software Inc" |
| `DS:0xCE0/CF2` | "level%d.mov" |
| `DS:0xCEC/CEF` | "rb" |
| `DS:0xCFE` | "wb" |
| `DS:0xDEB` | "Ready Player %1d" |
| `DS:0xE47` | "THE END  Player %1d" |
| `DS:0xE5B` | "Game Over Player %1d" |

## Адреса runtime Turbo-C

* `cs:0x1D0E:0x038` — `sprintf` (вспомогательная)
* `cs:0x1BF7:0x203` — `fopen` (нижний уровень)
* `cs:0x1BA1:0x00B` — `fclose` (нижний уровень)
* `cs:0x1C24:0x062` — `fread` (нижний уровень)
* `cs:0x1C92:0x00E` — внутреннее копирование (memcpy?)
* `cs:0x4E4:0x00B` — `_print` (вывод текста)
* `cs:0x4E4:0x4FA` — print int  
* `cs:0x330:0x30B` — `exit(0)`
* `cs:0x20F:0x00A` — `getch`/wait_for_key
* `cs:0x20F:0x416` — push_key
