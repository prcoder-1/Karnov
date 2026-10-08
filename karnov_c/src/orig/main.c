/* main.c — точка входа порта (не из оригинала): разбор опций, реестр DS,
 * затем main() оригинала (@0330:01C2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "orig/fn.h"
#include "orig/port.h"
#include "orig/hw.h"
#include <SDL2/SDL.h>

PortOpt port_opt;

void port_todo(const char *what)
{
    fprintf(stderr, "не переведено: %s\n", what);
    exit(2);
}

void port_todo_once(const char *what)
{
    static const char *seen[64];
    unsigned i;
    for (i = 0; i < 64 && seen[i]; i++)
        if (seen[i] == what)
            return;
    if (i < 64)
        seen[i] = what;
    fprintf(stderr, "не переведено (пропуск): %s\n", what);
}

void port_frame_hook(void)
{
    static FILE *fp;
    static u8 img[0x10000], mask[0x10000];
    static int frames;
    if (port_opt.dsdump) {
        ds_snapshot(img, mask);
        if (fp == NULL) {
            char mpath[512];
            FILE *mf;
            fp = fopen(port_opt.dsdump, "wb");
            snprintf(mpath, sizeof mpath, "%s.mask", port_opt.dsdump);
            mf = fopen(mpath, "wb");
            if (mf) {
                fwrite(mask, 1, sizeof mask, mf);
                fclose(mf);
            }
        }
        if (fp)
            fwrite(img, 1, sizeof img, fp);
        if (frames < 150) {             /* как HEAP.BIN хука DOSBox */
            static FILE *hp;
            char hpath[512];
            if (hp == NULL) {
                snprintf(hpath, sizeof hpath, "%s.heap", port_opt.dsdump);
                hp = fopen(hpath, "wb");
            }
            if (hp)
                fwrite(&hw_conv[0x2DE30], 1, 0x50000, hp);
        }
    }
    {
        static int fno;
        int k;
        char spath[64];
        for (k = 0; k < port_opt.nshot; k++)
            if (port_opt.shot[k] == fno) {
                snprintf(spath, sizeof spath, "shot_%05d.ppm", fno);
                hw_video_save_ppm(spath);
            }
        fno++;
    }
    if (port_opt.frames && ++frames >= port_opt.frames) {
        if (fp)
            fclose(fp);
        exit(0);
    }
}

/* SDL → коды BIOS (скан-код << 8 | ASCII). Стрелки — клавиши цифрового
 * блока при включённом NumLock (его включает read_key); отпускание всех
 * стрелок порт превращает в «5» — в оригинале стоп надо нажимать. */
static u16 bios_code(const SDL_KeyboardEvent *k)
{
    int ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
    SDL_Keycode c = k->keysym.sym;
    switch (k->keysym.scancode) {
    case SDL_SCANCODE_UP:    case SDL_SCANCODE_KP_8: return 0x4838;
    case SDL_SCANCODE_DOWN:  case SDL_SCANCODE_KP_2: return 0x5032;
    case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_KP_4: return 0x4B34;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4D36;
    case SDL_SCANCODE_KP_7: return 0x4737;
    case SDL_SCANCODE_KP_9: return 0x4939;
    case SDL_SCANCODE_KP_1: return 0x4F31;
    case SDL_SCANCODE_KP_3: return 0x5133;
    case SDL_SCANCODE_KP_5: return 0x4C35;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x1C0D;
    case SDL_SCANCODE_ESCAPE: return 0x011B;
    case SDL_SCANCODE_SPACE: return 0x3920;
    default: break;
    }
    if (c >= 'a' && c <= 'z') {
        static const u8 scan[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,
            0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
        u8 sc = scan[c - 'a'];
        if (ctrl)
            return (u16)(sc << 8 | (c - 'a' + 1));
        return (u16)(sc << 8 | (c - 'a' + 'A'));
    }
    if (c >= '0' && c <= '9')
        return (u16)((c == '0' ? 0x0B : c - '0' + 1) << 8 | c);
    return 0;
}

void port_pump_events(void)
{
    static int arrows;
    SDL_Event e;
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            exit(0);
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            SDL_Scancode sc = e.key.keysym.scancode;
            int arrow = sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN ||
                        sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT;
            if (e.type == SDL_KEYDOWN) {
                u16 code = bios_code(&e.key);
                if (arrow && !e.key.repeat)
                    arrows++;
                if (code)
                    hw_kbd_push(code);
            } else if (arrow && --arrows <= 0) {
                arrows = 0;
                hw_kbd_push(0x4C35);
            }
        }
    }
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) {
            int bad = ds_check();
            printf("ds_check: %s, переменных %u, функций %u\n",
                   bad ? "ОШИБКИ" : "ok", ds_nvars, fn_nreg);
            return bad;
        } else if (!strcmp(argv[i], "-v")) {
            port_opt.verbose = 1;
        } else if (!strcmp(argv[i], "--fast")) {
            port_opt.fast = 1;
        } else if (!strcmp(argv[i], "--no-audio")) {
            port_opt.no_audio = 1;
        } else if (!strcmp(argv[i], "--headless")) {
            port_opt.headless = 1;
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc && port_opt.nshot < 16) {
            port_opt.shot[port_opt.nshot++] = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            port_opt.frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dsdump") && i + 1 < argc) {
            port_opt.dsdump = argv[++i];
        }
    }
    if (ds_check())
        return 1;
    ds_init();
    rtl_startup_heap();
    return game_main();
}
