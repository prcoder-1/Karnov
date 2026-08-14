/* audio.c — PC-speaker эмулятор (SDL2 callback), script VM на 7 треков.
 *
 * Порт ../karnov_cpp/src/audio.cpp в C99. Музыкальный формат:
 *   FF reg lo hi   — записать в dur_lut[reg] = (lo=tone, hi=rest)
 *   note dur       — играть PITCH_LUT[note] на dur_lut[dur].lo тиков,
 *                    затем silence на dur_lut[dur].hi тиков
 *   0x00           — END (loop если play_music(loop=true))
 *   Note 0x6E      — silence (rest)
 *
 * Тик ~110.22 Гц (PIT ch0 div=5424 / 2). Square-wave фазовый аккумулятор.
 */
#include "audio.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define SAMPLE_RATE   22050
/* Частота SFX/музыкального тика: PIT divisor 0x1530 (file 0xE403) даёт
 * ISR 1193182/5424 = 219.98 Гц, а ISR чередует каналы по чётности тика
 * (file 0xE75C) → на канал приходится половина. */
#define TICK_RATE_HZ  (1193182.0 / 5424.0 / 2.0)
#define PIT_FREQ      1193182.0
#define NUM_TRACKS    7

/* Внешние таблицы из audio_data.c. */
extern const u16 PITCH_LUT[0x70];
extern const u8  MUSIC_04CE[]; extern const size_t MUSIC_04CE_LEN;
extern const u8  MUSIC_0663[]; extern const size_t MUSIC_0663_LEN;
extern const u8  MUSIC_06FC[]; extern const size_t MUSIC_06FC_LEN;
extern const u8  MUSIC_0791[]; extern const size_t MUSIC_0791_LEN;
extern const u8  MUSIC_08CC[]; extern const size_t MUSIC_08CC_LEN;
extern const u8  MUSIC_09D9[]; extern const size_t MUSIC_09D9_LEN;
extern const u8  MUSIC_0A6A[]; extern const size_t MUSIC_0A6A_LEN;

typedef struct {
    const u8 *data;
    size_t    size;
    const char *label;
} track_info_t;

static const track_info_t g_tracks[NUM_TRACKS] = {
    {NULL, 0, "in-game background"},      /* индексы заполним в audio_init */
    {NULL, 0, "time-out warning"},
    {NULL, 0, "level completion"},
    {NULL, 0, "end-of-game"},
    {NULL, 0, "high-score"},
    {NULL, 0, "Karnov death"},
    {NULL, 0, "LOADER"},
};

/* Чтобы избежать инициализаторов non-const, заполняем at runtime. */
static track_info_t g_tracks_rw[NUM_TRACKS];

/* ─── SFX (канал B) — декомпилировано из KARNOV.EXE ──────────────────────
 * play_sound(idx) @ cs:0xD4B:0x1124: far-ptr table DS:0x147E + приоритет
 * DS:0x146A; стример канала B @ cs:0xCB9:0x2D4. Формат — КОМАНДНЫЙ поток
 * (не music-скрипт):
 *   0x01 lo hi  — set PIT divisor (частота)
 *   0x03 lo hi  — set per-tick divisor delta (слайд высоты, s16)
 *   0x04 n      — играть текущий divisor n тиков
 *   0x02 b      — параметр (игнорируем)
 *   0x00        — конец
 * Перед обработкой каждого нот-цикла delta сбрасывается в 0 ([0x1228]=0).
 * SFX имеет ПРИОРИТЕТ над музыкой на едином голосе (как PC-speaker). */
static const u8 SFX1[] = {0x01,0x16,0xD2,0x04,0x02,0x01,0xC9,0x33,0x04,0x02,0x01,0x5C,0x9B,0x04,0x02,0x01,0x95,0x42,0x04,0x02,0x01,0x37,0x5D,0x04,0x02,0x01,0x16,0xD2,0x04,0x02,0x00}; /* enemy killed */
static const u8 SFX2[] = {0x01,0xAE,0x4D,0x04,0x02,0x01,0xC9,0x33,0x04,0x02,0x01,0x0B,0xE9,0x04,0x02,0x01,0xC9,0x33,0x04,0x02,0x01,0x37,0x5D,0x04,0x02,0x01,0xAE,0x4D,0x04,0x02,0x00}; /* player hurt */
static const u8 SFX3[] = {0x01,0xD3,0x48,0x03,0x80,0xFF,0x04,0x07,0x00};
static const u8 SFX4[] = {0x01,0x19,0x02,0x03,0xDD,0xFF,0x04,0x02,0x01,0x00,0x02,0x03,0xDD,0xFF,0x04,0x02,0x01,0xE9,0x01,0x03,0xDD,0xFF,0x04,0x02,0x00};
static const u8 SFX5[] = {0x01,0x2F,0x15,0x03,0xDD,0xFF,0x04,0x02,0x01,0x1F,0x0E,0x03,0xDD,0xFF,0x04,0x02,0x01,0x97,0x0A,0x03,0xDD,0xFF,0x04,0x02,0x00}; /* item pickup */
static const u8 SFX6[] = {0x01,0x2F,0x15,0x04,0x07,0x01,0x1F,0x0E,0x04,0x07,0x01,0x97,0x0A,0x04,0x07,0x01,0x4C,0x06,0x04,0x0F,0x01,0x97,0x0A,0x04,0x09,0x01,0x4C,0x06,0x04,0x0F,0x00};
/* SFX7 — ПОЛНЫЕ 79 байт из EXE (DS:0x0BCE): нисходящий свип 2.5 кГц→143 Гц
 * тремя сегментами + низкочастотный «треск» (0.22 c), всего ≈1.26 c.
 * Раньше здесь лежал обрезок в 9 байт: его сделали, когда считали SFX7
 * звуком выстрела и хотели короткий щелчок. Это была ошибка — выстрел
 * играет SFX0 (см. ниже), а SFX7 принадлежит другому действию Карнова
 * (near-вызовы из file 0x10C10 / 0x10DB1 / 0x1114C). */
static const u8 SFX7[] = {
    0x01,0x8D,0x01,0x03,0x50,0x00,0x04,0x32,0x03,0x40,0x00,0x04,0x32,
    0x03,0x30,0x00,0x04,0x0F,
    0x01,0x16,0xD2,0x04,0x02,0x01,0xC9,0x33,0x04,0x02,0x01,0x5C,0x9B,0x04,0x02,
    0x01,0x95,0x42,0x04,0x02,0x01,0x37,0x5D,0x04,0x02,0x01,0x16,0xD2,0x04,0x02,
    0x01,0x16,0xD2,0x04,0x02,0x01,0xC9,0x33,0x04,0x02,0x01,0x5C,0x9B,0x04,0x02,
    0x01,0x95,0x42,0x04,0x02,0x01,0x37,0x5D,0x04,0x02,0x01,0x16,0xD2,0x04,0x02,
    0x00};
static const u8 SFX8[] = {0x01,0x9B,0x2E,0x03,0x0C,0xFE,0x04,0x0C,0x00}; /* rock bounce */
static const u8 SFX9[] = {0x01,0x1E,0x02,0x03,0xFE,0xFF,0x04,0x07,0x00}; /* level-end */

/* SFX0 (DS:0x0B20) — ЗВУК ВЫСТРЕЛА ИГРОКА: шесть нот по одному тику,
 * ≈55 мс, короткий «бип». Именно его играет fire() (file 0x13045:
 * `xor ax,ax; push ax; call play_sound`). В порту этой записи не было
 * вовсе, а выстрел ошибочно озвучивался обрезанным SFX7. */
static const u8 SFX0[] = {
    0x01,0x5F,0x2A,0x04,0x01, 0x01,0x87,0x18,0x04,0x01,
    0x01,0xDA,0x23,0x04,0x01, 0x01,0x6A,0x1B,0x04,0x01,
    0x01,0x12,0x1F,0x04,0x01, 0x01,0x5F,0x2A,0x04,0x01, 0x00};

/* Приоритеты — таблица DS:0x146A оригинала. */
typedef struct { const u8 *data; u16 prio; } sfx_info_t;
static const sfx_info_t g_sfx_tbl[10] = {
    {SFX0,256}, {SFX1,144}, {SFX2,144}, {SFX3,112}, {SFX4,128},
    {SFX5,272}, {SFX6,288}, {SFX7,304}, {SFX8,320}, {SFX9,336},
};

typedef struct {
    const u8 *script;
    size_t    pc;
    bool      active;
    int       prio;
    int       divisor;      /* текущий PIT divisor (0..65535) */
    int       delta;        /* per-tick слайд (s16) */
    int       ticks_left;   /* оставшиеся тики текущей ноты */
    double    phase;
} sfx_state_t;
static sfx_state_t g_sfx;

typedef struct {
    /* Скрипт */
    const u8 *script;
    size_t    script_len;
    size_t    pc;
    bool      loop;
    bool      active;

    /* dur LUT (заполняется FF опкодами в скрипте). */
    struct { u8 lo, hi; } dur_lut[256];

    /* Состояние ноты */
    u16  divisor;
    int  tone_ticks_left;
    int  rest_ticks_left;

    /* Тики ↔ сэмплы */
    double samples_per_tick;
    double samples_acc;

    /* Фаза square-wave */
    double phase;
} player_state_t;

static player_state_t g_st;
static bool           g_initialized = false;
static volatile u32   g_cycle_count = 0;
static SDL_AudioDeviceID g_dev = 0;

/* ─── Единый PC-speaker (PIT ch2), как в KARNOV.EXE ──────────────────────
 * Таймерный ISR (PIT ch0 div=5424 → 220 Гц) на ЧЁТНЫХ тиках обслуживает SFX
 * (канал B, ISR @ 0x437), на НЕЧЁТНЫХ — музыку (канал A). Оба перепрограммируют
 * ОДИН делитель PIT ch2 → на едином голосе музыка и SFX МУЛЬТИПЛЕКСИРУЮТСЯ
 * (быстрое чередование), а не «SFX глушит музыку». Здесь фаза квадратной волны
 * непрерывна, а делитель g_pit_divisor меняется тем каналом, что тикнул. */
static double   g_isr_acc = 0.0;
static unsigned g_isr_parity = 0;
static int      g_pit_divisor = 0;
static double   g_spk_phase = 0.0;

static Sint16 spk_sample(void)
{
    if (g_pit_divisor < 64) return 0;
    double freq = PIT_FREQ / (double)g_pit_divisor;
    if (freq > 12000.0) return 0;
    g_spk_phase += freq / SAMPLE_RATE;
    if (g_spk_phase >= 1.0) g_spk_phase -= 1.0;
    return (g_spk_phase < 0.5) ? (Sint16)8000 : (Sint16)-8000;
}

static int fetch(void)
{
    if (!g_st.script || g_st.pc >= g_st.script_len) return -1;
    return g_st.script[g_st.pc++];
}

static void parse_until_note(void)
{
    while (g_st.active) {
        int op = fetch();
        if (op < 0 || op == 0x00) {
            if (op == 0x00 && g_st.loop) {
                g_st.pc = 0;
                g_cycle_count++;
                continue;
            }
            g_st.active = false;
            g_st.divisor = 0;
            return;
        }
        if (op == 0xFF) {
            int reg = fetch(); int lo = fetch(); int hi = fetch();
            if (reg < 0 || lo < 0 || hi < 0) { g_st.active = false; return; }
            g_st.dur_lut[reg & 0xFF].lo = (u8)lo;
            g_st.dur_lut[reg & 0xFF].hi = (u8)hi;
            continue;
        }
        int dur = fetch();
        if (dur < 0) { g_st.active = false; return; }
        u16 div = ((unsigned)op < (sizeof(PITCH_LUT)/sizeof(PITCH_LUT[0]))) ? PITCH_LUT[op] : 0;
        g_st.divisor         = div;
        g_st.tone_ticks_left = g_st.dur_lut[dur & 0xFF].lo;
        g_st.rest_ticks_left = g_st.dur_lut[dur & 0xFF].hi;
        if (g_st.tone_ticks_left == 0 && g_st.rest_ticks_left == 0) {
            g_st.tone_ticks_left = 1;
        }
        return;
    }
}

static void tick_music(void)
{
    if (!g_st.active) return;
    if (g_st.tone_ticks_left > 0) {
        --g_st.tone_ticks_left;
        if (g_st.tone_ticks_left == 0) {
            if (g_st.rest_ticks_left > 0) g_st.divisor = 0;
            else parse_until_note();
        }
        return;
    }
    if (g_st.rest_ticks_left > 0) {
        --g_st.rest_ticks_left;
        if (g_st.rest_ticks_left == 0) parse_until_note();
        return;
    }
    parse_until_note();
}

/* ─── SFX (канал B): командный стример (cs:0xCB9:0x2D4) ──────────────────
 * Порядок как в ISR @ 0x437-0x44E: сначала шаг стримера (dec countdown или
 * разбор новых команд), затем `[0x121b] += [0x1228]` (слайд) КАЖДЫЙ SFX-тик. */
static void sfx_tick(void)
{
    if (!g_sfx.active) return;
    if (g_sfx.ticks_left > 0) --g_sfx.ticks_left;
    if (g_sfx.ticks_left == 0) {           /* [0x1227]==0 → разобрать команды */
        g_sfx.delta = 0;                   /* [0x1228]=0 в начале нот-цикла */
        for (;;) {
            u8 cmd = g_sfx.script[g_sfx.pc++];
            if (cmd == 1) {                /* set divisor */
                g_sfx.divisor = g_sfx.script[g_sfx.pc] | (g_sfx.script[g_sfx.pc+1] << 8);
                g_sfx.pc += 2;
            } else if (cmd == 3) {         /* set per-tick delta (s16) */
                int v = g_sfx.script[g_sfx.pc] | (g_sfx.script[g_sfx.pc+1] << 8);
                g_sfx.delta = (v >= 0x8000) ? (v - 0x10000) : v;
                g_sfx.pc += 2;
            } else if (cmd == 4) {         /* play n ticks */
                g_sfx.ticks_left = g_sfx.script[g_sfx.pc++];
                break;
            } else if (cmd == 2) {         /* игнорируемый параметр */
                g_sfx.pc += 1;
            } else {                       /* 0x00 (или мусор) — конец */
                g_sfx.active = false;
                g_sfx.prio = 0;
                return;
            }
        }
    }
    g_sfx.divisor = (g_sfx.divisor + g_sfx.delta) & 0xFFFF;   /* слайд высоты */
}

static void audio_callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    Sint16 *out = (Sint16 *)stream;
    int n = len / 2;
    double isr_samples = g_st.samples_per_tick * 0.5;   /* ISR @ 2× tick-rate = 220 Гц */
    for (int i = 0; i < n; ++i) {
        g_isr_acc += 1.0;
        if (g_isr_acc >= isr_samples) {
            g_isr_acc -= isr_samples;
            if (g_isr_parity & 1u) {
                /* Нечётный ISR-тик — музыка (канал A). Если музыка не активна —
                 * спикер НЕ трогаем (HOLD, как ветка [0x120a]==0 → 0x479). */
                tick_music();
                if (g_st.active) g_pit_divisor = g_st.divisor;
            } else {
                /* Чётный ISR-тик — SFX (канал B): перепрограммирует спикер
                 * поверх музыки (мультиплекс на едином голосе). Нет SFX — HOLD. */
                if (g_sfx.active) {
                    sfx_tick();
                    if (g_sfx.active) g_pit_divisor = g_sfx.divisor;
                }
            }
            /* Ничего не играет → тишина (иначе спикер «завис» бы на последней ноте). */
            if (!g_st.active && !g_sfx.active) g_pit_divisor = 0;
            g_isr_parity++;
        }
        out[i] = spk_sample();
    }
}

/* ─── Public API ─────────────────────────────────────────────────────── */

int audio_init(void)
{
    if (g_initialized) return 0;

    /* Late-bind таблицы треков (data — extern в audio_data.c). */
    g_tracks_rw[0].data = MUSIC_04CE; g_tracks_rw[0].size = MUSIC_04CE_LEN;
    g_tracks_rw[1].data = MUSIC_0663; g_tracks_rw[1].size = MUSIC_0663_LEN;
    g_tracks_rw[2].data = MUSIC_06FC; g_tracks_rw[2].size = MUSIC_06FC_LEN;
    g_tracks_rw[3].data = MUSIC_0791; g_tracks_rw[3].size = MUSIC_0791_LEN;
    g_tracks_rw[4].data = MUSIC_08CC; g_tracks_rw[4].size = MUSIC_08CC_LEN;
    g_tracks_rw[5].data = MUSIC_09D9; g_tracks_rw[5].size = MUSIC_09D9_LEN;
    g_tracks_rw[6].data = MUSIC_0A6A; g_tracks_rw[6].size = MUSIC_0A6A_LEN;
    for (int i = 0; i < NUM_TRACKS; ++i) g_tracks_rw[i].label = g_tracks[i].label;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "audio_init: SDL_InitSubSystem AUDIO: %s\n", SDL_GetError());
        return 1;
    }

    SDL_AudioSpec want = {0}, got = {0};
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16LSB;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = audio_callback;

    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (g_dev == 0) {
        fprintf(stderr, "audio_init: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return 2;
    }

    memset(&g_st, 0, sizeof(g_st));
    g_st.samples_per_tick = (double)got.freq / TICK_RATE_HZ;
    SDL_PauseAudioDevice(g_dev, 0);
    g_initialized = true;
    return 0;
}

void audio_play_music(int track_idx, bool loop)
{
    if (!g_initialized) return;
    if (track_idx < 0 || track_idx >= NUM_TRACKS) return;

    SDL_LockAudioDevice(g_dev);
    g_st.script           = g_tracks_rw[track_idx].data;
    g_st.script_len       = g_tracks_rw[track_idx].size;
    g_st.pc               = 0;
    g_st.loop             = loop;
    g_st.divisor          = 0;
    g_st.tone_ticks_left  = 0;
    g_st.rest_ticks_left  = 0;
    g_st.active           = true;
    g_st.samples_acc      = 0.0;
    g_st.phase            = 0.0;
    memset(g_st.dur_lut, 0, sizeof(g_st.dur_lut));
    g_cycle_count = 0;
    SDL_UnlockAudioDevice(g_dev);
}

void audio_stop(void)
{
    if (!g_initialized) return;
    SDL_LockAudioDevice(g_dev);
    g_st.active = false;
    g_st.script = NULL;
    g_st.script_len = 0;
    g_st.pc = 0;
    g_st.divisor = 0;
    SDL_UnlockAudioDevice(g_dev);
}

u32 audio_cycle_count(void)
{
    return g_cycle_count;
}

void audio_play_sfx(int id)
{
    if (!g_initialized) return;
    if (id > 9 || !g_sfx_tbl[id].data) return;   /* id 0 — звук выстрела */
    SDL_LockAudioDevice(g_dev);
    /* Приоритет: новый SFX перебивает текущий только если prio >= (как
     * [0x121F]-арбитр в KARNOV.EXE). Одинаковый prio → перезапуск (rapid fire). */
    if (!g_sfx.active || (int)g_sfx_tbl[id].prio >= g_sfx.prio) {
        g_sfx.script     = g_sfx_tbl[id].data;
        g_sfx.pc         = 0;
        g_sfx.active     = true;
        g_sfx.prio       = (int)g_sfx_tbl[id].prio;
        g_sfx.divisor    = 0;
        g_sfx.delta      = 0;
        g_sfx.ticks_left = 0;
        g_sfx.phase      = 0.0;
    }
    SDL_UnlockAudioDevice(g_dev);
}

bool audio_music_finished(void)
{
    /* Не-зацикленный трек ставит g_st.active=false в parse_until_note при
     * достижении терминатора 0x00. Чтение bool из audio-потока — атомарно
     * достаточно для тайминга. Если плеер не инициализирован — «завершён». */
    if (!g_initialized) return true;
    return !g_st.active;
}

void audio_close(void)
{
    if (!g_initialized) return;
    SDL_PauseAudioDevice(g_dev, 1);
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_initialized = false;
}
