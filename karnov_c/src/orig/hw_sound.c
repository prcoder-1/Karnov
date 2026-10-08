/* hw_sound.c — PIT, динамик PC и векторы прерываний (порт).
 *
 * Тики PIT детерминированы: их выполняет главный поток в hw_wait_tick,
 * когда оригинал ждёт ISR. После каждого тика состояние динамика (делитель
 * ch2 или 0 = молчит) кладётся в кольцо; SDL-колбэк синтезирует меандр,
 * расходуя кольцо с частотой тиков (синтез — как spk_sample в src/old/audio.c). */
#include <SDL2/SDL.h>
#include "orig/hw.h"
#include "orig/port.h"

#define PIT_HZ      1193182.0
#define SAMPLE_RATE 44100
#define RING        4096

static u16 pit0_div;            /* 0 = 65536 */
static u16 pit2_div;
static int pit0_lohi, pit2_lohi; /* следующий байт — старший */
static u8  port61;
static farfn vec[256];

static u16 ring[RING];
static volatile unsigned ring_w, ring_r;
static SDL_AudioDeviceID dev;
static int audio_tried;
static double spk_phase, tick_acc;
static u16 cur_div;
static int present_div;

static void bios_int8(void) { }

int hw_sound_port_in(u16 port, u8 *v)
{
    if (port == 0x61) {
        *v = port61;
        return 1;
    }
    return 0;
}

int hw_sound_port_out(u16 port, u8 v)
{
    switch (port) {
    case 0x43:
        if ((v & 0xC0) == 0x00) pit0_lohi = 0;
        if ((v & 0xC0) == 0x80) pit2_lohi = 0;
        break;
    case 0x40:
        if (!pit0_lohi) pit0_div = (pit0_div & 0xFF00) | v;
        else pit0_div = (u16)((pit0_div & 0x00FF) | v << 8);
        pit0_lohi ^= 1;
        break;
    case 0x42:
        if (!pit2_lohi) pit2_div = (pit2_div & 0xFF00) | v;
        else pit2_div = (u16)((pit2_div & 0x00FF) | v << 8);
        pit2_lohi ^= 1;
        break;
    case 0x61:
        port61 = v;
        break;
    case 0x20:
        break;
    default:
        return 0;
    }
    return 1;
}

farfn hw_getvect(u8 n)
{
    if (n == 8 && !vec[8])
        return bios_int8;
    return vec[n];
}

void hw_setvect(u8 n, farfn f)
{
    vec[n] = f == bios_int8 ? NULL : f;
}

static double tick_hz(void)
{
    return PIT_HZ / (pit0_div ? pit0_div : 65536.0);
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int i, n = len / 2;
    double spt = SAMPLE_RATE / tick_hz();
    (void)ud;
    for (i = 0; i < n; i++) {
        tick_acc += 1.0;
        if (tick_acc >= spt) {
            tick_acc -= spt;
            if (ring_r != ring_w)
                cur_div = ring[ring_r++ % RING];
        }
        if (cur_div < 64) {
            out[i] = 0;
        } else {
            double f = PIT_HZ / cur_div;
            spk_phase += f / SAMPLE_RATE;
            if (spk_phase >= 1.0) spk_phase -= 1.0;
            out[i] = f > 12000.0 ? 0 : spk_phase < 0.5 ? 6000 : -6000;
        }
    }
}

static void audio_open(void)
{
    SDL_AudioSpec want, got;
    audio_tried = 1;
    if (port_opt.headless || port_opt.no_audio)
        return;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16LSB;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (dev)
        SDL_PauseAudioDevice(dev, 0);
}

void hw_wait_tick(void)
{
    static Uint64 next;
    if (!audio_tried)
        audio_open();
    if (!port_opt.headless && !port_opt.fast) {
        Uint64 f = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter();
        Uint64 period = (Uint64)(f / tick_hz());
        if (next == 0 || now > next + f / 4)
            next = now;
        while ((now = SDL_GetPerformanceCounter()) < next) {
            Uint64 left = (next - now) * 1000 / f;
            if (left > 1)
                SDL_Delay((Uint32)(left - 1));
        }
        next += period;
        port_pump_events();
    }
    if (vec[8])
        vec[8]();
    if (++present_div >= 4) {
        present_div = 0;
        hw_video_present();
    }
    if (dev && ring_w - ring_r < RING)
        ring[ring_w++ % RING] = (port61 & 3) == 3 ? pit2_div : 0;
}
