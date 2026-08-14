# Karnov — звуковая подсистема: реверс-инжиниринг и порт на SDL2

> **Уточнение 2026-08-15 (ЗВУК ВЫСТРЕЛА).** Выстрел игрока играет **SFX 0**
> (`DS:0x0B20`, 31 байт, шесть нот по 1 тику ≈55 мс — короткий «бип»):
> `fire()` @file 0x13045 делает `xor ax,ax; push ax; call play_sound`.
> Длинный SFX 7 (79 байт, нисходящий свип + треск, ≈1.26 с) к выстрелу
> отношения не имеет — его играет другое действие Карнова
> (near-вызовы из file 0x10C10 / 0x10DB1 / 0x1114C). Раньше порт озвучивал
> выстрел УКОРОЧЕННЫМ SFX 7 и не имел SFX 0 вовсе.
>
> Частота тика: PIT divisor 0x1530 → ISR 219.98 Гц, ISR чередует каналы по
> чётности → на канал 109.99 Гц.

Документ описывает звуковую подсистему оригинального KARNOV.EXE
(Electronic Arts / Data East, 1987, Turbo C compact-model)
и её перенос в `karnov_cpp/src/audio.cpp` через SDL2.

## 1. Архитектура оригинала

Звук в KARNOV.EXE — PC speaker, программируемый через PIT канал 2
(порты `0x42`/`0x43`, gate `0x61`). Два потока:

- **Канал B (SFX)** — короткие эффекты, 10 скриптов, таблица `DS:0x147E`.
- **Канал A (музыка)** — фоновая музыка с pitch-LUT, 7 треков `DS:0x04CE..`.

В каждый момент времени PC speaker звучит одной частотой. Микшер
оригинала — приоритетный: SFX перебивает музыку, после окончания SFX
канал A возвращает управление.

### Адреса в KARNOV.EXE

```
cs:0xCB9:0x0DD   play_sound_internal(off, seg, dur)     ; file off 0xE46D
cs:0xCB9:0x3CC   timer_isr (INT 08h hook)               ; file off 0xE75C
cs:0xCB9:0x298   music_tick (channel A advance)         ; file off 0xE628
cs:0xCB9:0x07D   play_music(off, seg, prio, loop)       ; file off 0xE40D
cs:0xCEA:0x098   channel-A script dispatcher            ; file off 0xE6A4
cs:0xD4B:0x1124  play_sound(idx)  wrapper               ; file off 0xFDD4
```

### Тайминги PIT

- PIT канал 0 перепрограммирован на divisor `0x1530` = 5424 →
  частота тика ≈ `1193182 / 5424` ≈ **220 Гц**.
- `music_tick` вызывается каждый второй PIT-тик → **110 Гц** для музыки.
- Длительности WAIT/note считаются в этих 110-Гц «муз.тиках».

### Формат скрипта SFX (канал B, 5 опкодов)

```
00              END
01 lo hi        SET_FREQ:   PIT divisor (audible freq = 1193182 / divisor)
02              (зарезервирован, в 10 имеющихся SFX не встречается)
03 lo hi        SET_DELTA:  signed 16-bit, прибавляется к divisor каждый муз.тик
04 dur          WAIT:       продолжать <dur> муз.тиков с текущим состоянием
```

10 скриптов SFX расположены в `DS:0x0B17..0x0C2E`.

### Формат скрипта музыки (канал A)

Музыка использует pitch-LUT (chromatic scale, A4=440 Гц на ноте `0x3A`)
по адресу `DS:0x126A`, 112 16-битных divisor'ов.

Опкоды:

```
FF reg lo hi    LOAD-DUR-LUT/state register
note dur        play_note: PIT divisor = LUT[note], держать <dur> муз.тиков
00              END (если loop_flag — перезапуск с начала)
```

7 музыкальных треков расположены в `DS:0x04CE..0x0B17`.

## 2. Идентификация 7 музыкальных треков

Все 7 треков были идентифицированы пользователем итеративно через
переменную окружения `KARNOV_MUSIC=N` (см. ниже):

| idx | DS-offset | назначение                              | режим            |
| --- | --------- | --------------------------------------- | ---------------- |
| 0   | 0x04CE    | фоновая музыка игры                     | loop             |
| 1   | 0x0663    | time-out warning (финальный уровень)    | loop             |
| 2   | 0x06FC    | конец уровня — jingle                   | once             |
| 3   | 0x0791    | end-of-game credits                     | once, prio 0x1000|
| 4   | 0x08CC    | high-score                              | once, prio 0x1000|
| 5   | 0x09D9    | смерть Karnov                           | once, prio 0x64  |
| 6   | 0x0A6A    | **LOADER** — подгрузка уровней          | **loop**         |

Контекст использования трека **6 (LOADER)**:
функция `cs:0x40E` (file off `0x5854`) циклически перебирает
`current_level=(0,1,2)`, вызывает STOP music, короткая задержка,
PLAY track 6 с LOOP — классический паттерн фоновой загрузки.

## 3. Сигнатура `play_music`

Изначально вызовы выглядели как 3-аргументные, но обращение
`MOV AX, [BP+0xE]` показывает 4-й аргумент `loop_flag`:

```c
void play_music(uint16_t off,    /* [BP+8]   ofs скрипта в DS */
                uint16_t seg,    /* [BP+0xA] сегмент (всегда DS) */
                uint16_t prio,   /* [BP+0xC] приоритет (0x64, 0x1000, ...) */
                uint16_t loop);  /* [BP+0xE] 0 = once, !=0 = loop */
```

Приоритет используется при «затирании» уже играющей музыки: новый трек
заменяет текущий только если его prio ≥ prio текущего.

## 4. Реализация на SDL2 (karnov_cpp)

### Файлы

```
include/audio.h    — публичное API
src/audio.cpp      — реализация (~600 строк):
                     • встроенный pitch-LUT (112 divisors)
                     • встроенные данные всех 7 треков + 10 SFX
                     • эмулятор PC speaker через фазовый аккумулятор
                     • интерпретатор опкодов канала A и B
                     • SDL2-объявления вручную (без SDL2-devel)
```

### Эмуляция PC speaker

Фазовый аккумулятор: при текущем PIT-divisor целевая частота
`f = 1193182 / divisor`, шаг фазы `dphi = f / sample_rate`. На каждом
сэмпле фаза инкрементируется, выход — sign(phase) → `±amp` (square wave).

Sample rate — 22050 Hz, формат `AUDIO_S16SYS`, mono.

### Очередь и приоритеты

Состояние — atomic+SDL_LockAudio для синхронизации потоков:

```
g_initialized          - инициализован ли SDL_OpenAudio
g_chA / g_chB          - текущие позиции в скрипте каждого канала
g_chA_prio             - приоритет занявшего канал A трека
g_chA_loop_start       - офсет начала, если loop=true
```

В callback'е SDL: для каждого сэмпла — продвинуть оба канала на
`1/22050 c`, если пришёл WAIT-конец — прочесть следующий опкод;
смикшировать сэмпл (приоритет B > A).

### Публичное API

```cpp
int  audio_init();                          // 0 = ok
void audio_play_sound(int idx);             // SFX 0..9
void audio_queue_intro();                   // запустить loader-музыку
void audio_play_music(int idx, bool loop);  // idx 0..6
bool audio_busy();
void audio_wait_idle();
void audio_stop();
void audio_close();
```

### `audio_queue_intro` (выбор трека)

```cpp
void audio_queue_intro() {
    if (!g_initialized.load()) return;
    int idx = 6;  /* LOADER (DS:0x0A6A) — по умолчанию */
    if (const char* env = std::getenv("KARNOV_MUSIC")) {
        char* endp = nullptr;
        long v = std::strtol(env, &endp, 10);
        if (endp != env && v >= 0 && v < 7) idx = (int)v;
    }
    std::printf("audio: track %d — %s\n", idx, g_tracks[idx].label);
    audio_play_music(idx, false);
}
```

Переменная окружения `KARNOV_MUSIC=0..6` переопределяет дефолтный
трек — использовалась для итеративной идентификации.

### Сборка

`Makefile` обновлён:

```
SRC      += src/audio.cpp
LDLIBS    = -lSDL2 -lpthread
```

SDL2-devel не требуется — все нужные структуры и прототипы объявлены
прямо в `audio.cpp`, линковка идёт с библиотекой `libSDL2.so`.

## 5. Сопутствующие исправления (вне аудио)

В ходе работы были исправлены попутные баги в C++-реконструкции:

### 5.1 "Insert data disk" hang

`src/file_io.cpp` — добавлен `karnov_fopen()`, который пробует имя как
есть, затем в UPPER, затем в lower case. Это нужно для DOS-имён файлов
(`LEVEL0.BIN`) на чувствительной к регистру FS Linux.

`src/level.cpp:prompt_insert_data_disk()`:
- добавлен параметр `const char* missing_file` — печатается имя
  ненайденного файла;
- цикл `while ((c = getchar()) == EOF) {}` заменён на одиночный
  `getchar()` + EOF-выход (иначе бесконечный цикл при отсутствии stdin).

### 5.2 Бесконечный спам "level load failed for N"

`src/game.cpp`:
- `start_level()` стал возвращать `int` (LL_OK или код ошибки);
- `run_game()` считает успешно загруженные уровни за attract-pass;
- если за полный круг (10 уровней) не загрузилось ни одного —
  печатается сообщение и игра завершается.

### 5.3 Неверный формат карты

`src/level.cpp`: убран лишний `tile_size_bytes = tile_size_words << 1`.
Правильно: читаем `tile_size_bytes` напрямую из файла, и потом
вычисляем `tile_size_words = tile_size_bytes >> 1`.

Проверено через `karnov_decode.py`: `num_unique_tiles * <третье_слово>`
совпадает с фактическим размером блока пикселей в `LEVELn.BIN`.

`FORMATS.md` обновлён согласно правильной семантике.

### 5.4 Тесты

`test/test_io.cpp`: добавлен Test 5 — end-to-end `load_level()` для
уровней 0,1,2,3,9. Все 35 тестов проходят.

## 6. Полезные инструменты диагностики

### Дамп трека в WAV (без SDL)

`/tmp/wavdump.cpp` — заглушки SDL2 (`SDL_Init/SDL_OpenAudio/SDL_Delay`
возвращают вызов callback'а из main thread). Использование:

```
g++ -I include /tmp/wavdump.cpp src/audio.cpp -o /tmp/wavdump
/tmp/wavdump 6 > track6.wav
```

### Standalone-тест аудиомодуля

`/tmp/audtest.cpp` — `audio_init()` + `audio_queue_intro()` +
busy-poll до окончания. Линкуется с `src/audio.cpp` и `-lSDL2`.

## 7. История идентификации треков

Треки были опознаны итеративно по слуху пользователя:

| итерация | KARNOV_MUSIC | вердикт                                  |
| -------- | ------------ | ---------------------------------------- |
| 1        | 4 (default)  | «доска рекордов» → high-score            |
| 2        | 0            | «фоновая музыка из игры» → game BG       |
| 3        | 3            | «после полного прохождения» → end-credit |
| 4        | 1            | «когда время заканчивается» → time-out   |
| 5        | 2            | «окончание уровня» → level-complete      |
| 6        | 5            | «гибель Karnov» → death                  |
| 7        | 6            | по исключению → **LOADER** ✓             |

Итог: `int idx = 6` в `audio_queue_intro()`, при запуске выводится
`audio: track 6 — DS:0x0A6A — LOADER (level loading)`.
