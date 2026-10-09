# karnov_c — Karnov (Data East, 1987) на C99 + SDL2

Две сборки (`make`):

- **`karnov`** — перевод KARNOV.EXE на Си функция в функцию (`src/orig`);
- **`karnov_old`** — прежний порт-реконструкция (`src/old`), см. ниже.

## karnov — перевод KARNOV.EXE

```bash
make karnov
cd ..               # каталог с LEVEL?.BIN, LEVEL?.MOV, SPRITES.EGA/CGA
./karnov_c/karnov             # EGA, как в оригинале без Shift
./karnov_c/karnov --menu      # меню видеорежима: CGA, EGA, Tandy, Hercules
./karnov_c/karnov --no-audio
./karnov_c/karnov --fps 0        # без ограничения: как оригинал на быстрой машине
```

Темп постоянный: по умолчанию около 16 кадров/с (14 тиков таймера
на кадр — в среднем столько, сколько оригинал тратит на IBM PC/AT);
`--fps N` задаёт другой, 0 — минимум оригинала, 8 тиков (≈27 кадров/с).

Загрузка уровня мгновенная, поэтому музыка загрузчика доигрывает свой
цикл (≈14 с); любая клавиша его прерывает.

Опции для автоматических прогонов: `--headless` (без окна и звука),
`--fast` (без выдержки реального времени), `--frames N` (выход после N
кадров главного цикла).

Управление — как в оригинале: направление «залипает» (цифровой блок,
5 — стоп; стрелки работают как цифры, отпускание всех стрелок — 5),
пробел или X — огонь, Z — предмет, Enter — начать игру, Esc — пауза,
Q — выход, 1–4 — два игрока, джойстик, музыка, эффекты.

Устройство:

- `src/orig/sSSSS_*.c` — модули оригинала по сегментам; перед каждой
  функцией тег `@SEG:OFF file:FFFFF` — её адрес в KARNOV.EXE
  (`include/orig/fn.h` — прототипы);
- `include/orig/ds.h`, `src/orig/ds.c` — глобалы сегмента данных с
  начальными значениями (в комментариях — адреса DS);
- `src/orig/hw*.c` — то, что оригинал делает портами и BIOS: EGA,
  CGA, Tandy, Hercules и текст BIOS (`hw_video.c`), PIT и динамик
  (`hw_sound.c`), клавиатура и джойстик (`hw.c`), детект машины;
- `src/orig/main.c` — опции и события SDL.

Где оригинал читает память за пределами массивов (мусор, зависящий от
DOS и порядка загрузки), перевод использует определённые правила:
клетки вне карты — пустой тайл (ниже предела камеры — пол), пиксель →
клетка считается формулой, у объекта без анимации 0 кадров, нота за
таблицей частот — неслышимый делитель.

## karnov_old — прежний порт-реконструкция

C99 порт DOS-игры Karnov на базе реверс-инжиниринга оригинального
KARNOV.EXE. Использует:

- **SDL2** для окна и аудио
- **Оригинальные спрайты** из SPRITES.EGA / SPRITES.CGA (через pre-extracted
  кэш `data/karnov_sprites_{ega,cga}.bin`)
- **Оригинальную loader-music** (track 6 = DS:0x0A6A), извлечённую байт-в-байт
  из KARNOV.EXE и портированную в `src/audio_data.c`

Референс декомпиляции (пословные реконструкции THINKCYCLE и всех 57
AI-обработчиков) — в `../docs/reference/`; сводная документация по
KARNOV.EXE и форматам файлов — `../KARNOV.md`.

### Сборка и запуск

```bash
make karnov_old                       # → ./karnov_old
./karnov_old                          # splash LEVEL0 в SDL2-окне (с аудио)
./karnov_old --no-audio               # без звука
./karnov_old --cga                    # CGA-палитра вместо EGA
./karnov_old --level 1                # загрузить LEVEL1.BIN
./karnov_old --scale 4                # окно 320*4 × 168*4
./karnov_old --ppm out.ppm --frame 50 # headless рендер 50-го кадра в PPM
./karnov_old --music-test             # ждать один цикл loader-music и выйти
```

Клавиатура в SDL2-окне: стрелки = R/L/D/U, **X или Space = FIRE, Z = OPTION**
(как в оригинале: биты 0x10 и 0x20), ESC = выход. Движение влево/вправо
также листает выбранный предмет OPTIONS (каждые 16 px пути).

### Что работает

| Функция | Статус |
|---------|--------|
| Загрузчик LEVEL%d.BIN (RLE 0x5A) | ✅ Все 10 уровней парсятся |
| Парсер LEVEL%d.MOV (attract-demo) | ✅ |
| Рендер тайл-карты (16×10 px @ 2bpp) | ✅ |
| EGA-палитра + attr-driven sub-палитры | ✅ (титул в правильных цветах) |
| Sprite-cache (KS v4, 334 EGA / 338 CGA) | ✅ после банк-фикса директории |
| Karnov (sprite #98-#123, 8 анимаций) | ✅ |
| Анимация раскладной лесенки (#24) | ✅ за 60 кадров |
| Физика Karnov (gravity/jump/walk/crouch/fire) | ✅ |
| Тикер MOV-демо → input_state | ✅ |
| HUD (frame, X, INPUT, REC) | ✅ |
| Loader-music (track 6, один цикл) | ✅ ~14 сек |
| Все 7 музыкальных треков извлечены | ✅ (в audio_data.c) |
| Y-tracker AI (тип 13, зелёный тролль) | ✅ |
| SDL2 keyboard input | ✅ |
| Горизонтальный/вертикальный скролл | ✅ (для LEVEL1+) |
| Семантика спауна (subtype→obj_type, init_arg, HP=trg>>8, X×2) | ✅ |
| Type 8 sky-spawner (PRNG-летуны у игрока) | ✅ |
| Все 57 AI типов | ✅ обработчики по INIT-константам декомпиляции; TICK там, где не декомпилирован — по семантике (chaser/patrol/worm), помечено TODO |
| Multi-part враги (змея 20, цепи 21/33/34/39/50, Рю 36) | ✅ |
| Общий PRNG (Turbo-C LCG) — детерминизм демо | ✅ |
| Заставка LEVEL0 data-driven (без hardcode) | ✅ |
| Жизни, level-end секвенция (босс → свиток) | ✅ |
| Гэймплей: очки, THINKCYCLE pair-loop 1:1 | ⚠️ частично (урон по subtype) |

### Структура

```
karnov_c/
├── Makefile               # gcc -std=c99 -pedantic -Wall -Wextra
├── data/                  # симлинки на ../../{LEVEL*,SPRITES*}, кэш спрайтов
├── include/
│   ├── karnov.h           # типы (u8/u16/u32/u64), константы окна/тайла
│   ├── file_io.h          # bin_fopen/fread (RLE 0x5A)
│   ├── palette.h          # EGA-16 + CGA-4 + attr-sub-палитры
│   ├── sprite.h           # KS v3/v4 cache loader + манифест ai_type
│   ├── level.h            # парсер LEVEL%d.BIN
│   ├── demo.h             # парсер LEVEL%d.MOV + tick
│   ├── audio.h            # API плеера (init, play_music, cycle_count)
│   ├── input.h            # SDL2 → input_state bits
│   ├── physics.h          # gravity/jump/walk Karnov
│   ├── ai.h               # function-pointer table AI
│   ├── render.h           # SDL2 окно + framebuffer + блиттеры
│   └── game.h             # splash_run
└── src/                   # реализации всех модулей
```

## Источники

- `../KARNOV.md` — устройство KARNOV.EXE и форматы файлов (консолидированный)
- `../AUDIO.md` — звуковая подсистема (PC-speaker)
- `../docs/DECOMPILE_NOTES.md` — карта адресов KARNOV.EXE
- `../docs/ENEMY_GUIDE.md` — каталог всех 57 AI-типов
- `../docs/reference/` — C++-реконструкция (ai_scripts.cpp, think.cpp и др.)
