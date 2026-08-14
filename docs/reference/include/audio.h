/* audio.h — звуковая подсистема.
 *
 * Воспроизводит музыку KARNOV.EXE через SDL2-аудио, эмулируя PC speaker
 * (квадратная волна, управляемая PIT-divisor'ом, как в оригинале).
 *
 * Реверс-инжиниринг (адреса в KARNOV.EXE):
 *   cs:0xCB9:0xDD    play_sound_internal(off, seg, dur)   ; file 0xE46D
 *   cs:0xCB9:0x3CC   timer_isr                            ; file 0xE75C
 *   cs:0xCB9:0x298   music_tick                           ; file 0xE628
 *   cs:0xCEA:0x098   script dispatcher / opcode table     ; file 0xE6A4
 *   cs:0xD4B:0x1124  play_sound(idx)                      ; file 0xFDD4
 *
 * Формат скрипта (5 опкодов 00..04):
 *   01 lo hi   SET_FREQ:  PIT-divisor (audible freq = 1193182/divisor)
 *   03 lo hi   SET_DELTA: signed 16-bit, прибавляется к divisor'у каждый тик
 *   04 dur     WAIT:      продолжать <dur> муз.тиков с текущим состоянием
 *   00         END
 *   (опкод 02 — не используется в имеющихся 10 звуках)
 *
 * Тики музыки идут на ~110 Hz (PIT перепрограммирован на ~220 Hz,
 * music_tick вызывается каждый 2-й кадр).
 */
#ifndef KARNOV_AUDIO_H
#define KARNOV_AUDIO_H

#include "karnov.h"

namespace karnov {

/* Инициализация SDL2-аудио. Возвращает 0 при успехе, иначе ≠0 (тогда
 * остальные функции — no-op, игра продолжает без звука). */
int audio_init();

/* Поставить в очередь воспроизведение звука idx (0..9). Не блокирует. */
void audio_play_sound(int idx);

/* Поставить в очередь все 10 звуков подряд (как «вступительная музыка»).
 * Не блокирует — возврат сразу. */
void audio_queue_intro();

/* Запустить один из 7 музыкальных треков (channel A), идентифицированных
 * по play_music-вызовам в KARNOV.EXE и по слуху пользователем.
 * Индексы 0..6 соответствуют:
 *   0: DS:0x04CE — фоновая музыка игры (loop)
 *   1: DS:0x0663 — time-out warning на финальном уровне (loop)
 *   2: DS:0x06FC — конец уровня jingle (once)
 *   3: DS:0x0791 — end-of-game credits (once, priority 0x1000)
 *   4: DS:0x08CC — high-score (once, priority 0x1000)
 *   5: DS:0x09D9 — смерть Karnov (once, priority 0x64)
 *   6: DS:0x0A6A — LOADER, подгрузка уровня (loop) ← intro по умолчанию
 * loop=true → перезапуск с начала при достижении 00; false → играть один раз.
 * Если track_idx вне диапазона — no-op. */
void audio_play_music(int track_idx, bool loop);

/* Истинно, если ещё что-то играет. */
bool audio_busy();

/* Количество завершённых циклов loop-музыки с момента последнего
 * audio_play_music(). Для непетлевых треков всегда 0. */
unsigned audio_cycle_count();

/* Блокирующее ожидание окончания текущей очереди. Возвращает сразу,
 * если очередь пуста. */
void audio_wait_idle();

/* Остановить воспроизведение и очистить очередь. */
void audio_stop();

/* Завершение SDL2-аудио. */
void audio_close();

} /* namespace karnov */

#endif /* KARNOV_AUDIO_H */
