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
#define TICK_RATE_HZ  110.225
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

/* ─── Square-wave генератор ──────────────────────────────────────────── */
static Sint16 next_sample(void)
{
    if (g_st.divisor < 64) return 0;
    double freq = PIT_FREQ / (double)g_st.divisor;
    if (freq > 12000.0) return 0;
    g_st.phase += freq / SAMPLE_RATE;
    if (g_st.phase >= 1.0) g_st.phase -= 1.0;
    return (g_st.phase < 0.5) ? (Sint16)8000 : (Sint16)-8000;
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

static void audio_callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    Sint16 *out = (Sint16 *)stream;
    int n = len / 2;
    for (int i = 0; i < n; ++i) {
        g_st.samples_acc += 1.0;
        if (g_st.samples_acc >= g_st.samples_per_tick) {
            g_st.samples_acc -= g_st.samples_per_tick;
            tick_music();
        }
        out[i] = g_st.active ? next_sample() : (Sint16)0;
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

void audio_close(void)
{
    if (!g_initialized) return;
    SDL_PauseAudioDevice(g_dev, 1);
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_initialized = false;
}
