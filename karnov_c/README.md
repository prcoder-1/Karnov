# karnov_c — чистый C99 порт Karnov (Data East, 1987)

C99 порт DOS-игры Karnov на базе реверс-инжиниринга оригинального
KARNOV.EXE. Использует:

- **SDL2** для окна и аудио
- **Оригинальные спрайты** из SPRITES.EGA / SPRITES.CGA (через pre-extracted
  кэш `data/karnov_sprites_{ega,cga}.bin`)
- **Оригинальную loader-music** (track 6 = DS:0x0A6A), извлечённую байт-в-байт
  из KARNOV.EXE и портированную в `src/audio_data.c`

Существует параллельный C++ порт `../karnov_cpp/` — он содержит более полную
декомпиляцию AI и используется как референс. Этот же каталог — компактный
C99 порт того же набора подсистем.

## Сборка и запуск

```bash
make                              # → ./karnov
./karnov                          # splash LEVEL0 в SDL2-окне (с аудио)
./karnov --no-audio               # без звука
./karnov --cga                    # CGA-палитра вместо EGA
./karnov --level 1                # загрузить LEVEL1.BIN
./karnov --scale 4                # окно 320*4 × 168*4
./karnov --ppm out.ppm --frame 50 # headless рендер 50-го кадра в PPM
./karnov --music-test             # ждать один цикл loader-music и выйти
```

Клавиатура в SDL2-окне: стрелки = R/L/D/U, Z или Space = FIRE, X = OPTION,
ESC = выход.

## Что работает

| Функция | Статус |
|---------|--------|
| Загрузчик LEVEL%d.BIN (RLE 0x5A) | ✅ Все 10 уровней парсятся |
| Парсер LEVEL%d.MOV (attract-demo) | ✅ |
| Рендер тайл-карты (16×10 px @ 2bpp) | ✅ |
| EGA-палитра + attr-driven sub-палитры | ✅ (титул в правильных цветах) |
| Sprite-cache (KS v3, 209 EGA / 228 CGA) | ✅ |
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
| Все 57 AI типов | ⚠️ только 3 типа реальные; остальные = stub |
| Гэймплей: триггеры, очки, жизни | ❌ — будущая работа |

## Структура

```
karnov_c/
├── Makefile               # gcc -std=c99 -pedantic -Wall -Wextra
├── data/                  # симлинки на ../../{LEVEL*,SPRITES*}, кэш спрайтов
├── include/
│   ├── karnov.h           # типы (u8/u16/u32/u64), константы окна/тайла
│   ├── file_io.h          # bin_fopen/fread (RLE 0x5A)
│   ├── palette.h          # EGA-16 + CGA-4 + attr-sub-палитры
│   ├── sprite.h           # KS v3 cache loader
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

- `../karnov_cpp/` — параллельный C++ порт с более полной декомпиляцией
- `../FORMATS.md` — описание форматов файлов
- `../karnov_cpp/DECOMPILE_NOTES.md` — карта адресов KARNOV.EXE → C++ имена
- `../karnov_cpp/AUDIO.md` — описание музыкальной подсистемы
- `../karnov_cpp/ENEMY_GUIDE.md` — каталог AI-типов
