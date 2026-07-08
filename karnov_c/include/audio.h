/* audio.h — PC-speaker эмулятор (SDL2 callback). */
#ifndef KARNOV_AUDIO_H
#define KARNOV_AUDIO_H

#include "karnov.h"

int  audio_init(void);
void audio_close(void);
void audio_play_music(int track_idx, bool loop);
/* Проиграть звуковой эффект по индексу play_sound(id) из KARNOV.EXE:
 * 1=враг убит, 2=игрок ранен, 5=подбор предмета, 7=выстрел, 8=отскок камня,
 * 9=конец уровня. SFX перекрывает музыку на едином голосе (по приоритету). */
void audio_play_sfx(int id);
void audio_stop(void);
u32  audio_cycle_count(void);
/* true, если проигрывание не-зацикленного трека завершилось (или плеер не
 * инициализирован). Используется для тайминга level-end jingle. */
bool audio_music_finished(void);

#endif
