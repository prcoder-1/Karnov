/* demo.cpp — запись и воспроизведение аттракт-демо.
 *
 * Пословный перевод из KARNOV.EXE:
 *   cs:0x6CB9 — is_demo_at_end       (curr_idx == num_records)
 *   cs:0x6D00 — demo_reset
 *   cs:0x6D13 — внутренний inc word [es:bx+2] & 0xff (post-record advance)
 *   cs:0x6D27 — demo_advance         (++curr_idx, mask 0xFF)
 *   cs:0x6D39 — внутренний sprintf (вспомогательный)
 *   cs:0x6D5D — demo_record_event
 *   cs:0x6DC0 — tick_demo
 *   cs:0x6E16 — load_demo
 *   cs:0x6FCC — save_demo
 *
 * Структура файла LEVEL%d.MOV:
 *   uint16_t  start_record_idx;       // всегда 0
 *   uint16_t  num_records;
 *   { uint8_t state; uint16_t frame_le; } records[num_records];
 */
#include "demo.h"
#include "globals.h"
#include "file_io.h"
#include "video.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace karnov {

/* KARNOV.EXE: cs:0x6CB9. */
int is_demo_at_end(DemoState* st) {
    if (!st) return 1;
    return (st->curr_idx == st->num_records) ? 1 : 0;
}

/* KARNOV.EXE: cs:0x6D00. */
void demo_reset(DemoState* st) {
    if (!st) return;
    st->curr_idx     = 0;
    st->num_records  = 0;
}

/* KARNOV.EXE: cs:0x6D27.
 * В оригинале mask 0xFF — в реальных демо всегда < 200 событий. */
void demo_advance(DemoState* st) {
    if (!st) return;
    st->curr_idx = (st->curr_idx + 1) & 0xFF;
}

/* KARNOV.EXE: cs:0x6D5D — demo_record_event.
 *
 * if (is_demo_at_end(st)) return 0;
 * records[num_records].input_state  = input;
 * records[num_records].frame_number = frame;
 * ++num_records;          // см. cs:0x6D13
 * return 1;
 *
 * Замечание: в оригинале num_records — это «следующий свободный слот»
 * (когда мы записываем). curr_idx используется для другого набора. */
int demo_record_event(DemoState* st, u8 input, u16 frame) {
    if (is_demo_at_end(st)) return 0;
    /* В оригинале запись использует поле [es:bx+2] (== num_records) как
     * текущий индекс записи. После записи это поле инкрементируется. */
    MovRecord* rec = &st->records[st->num_records];
    rec->input_state  = input;
    rec->frame_number = frame;
    st->num_records = (st->num_records + 1) & 0xFF;
    return 1;
}

/* KARNOV.EXE: cs:0x6DC0 — tick_demo.
 *
 *   if is_demo_at_end(st):
 *       return (current_level == FINAL_LEVEL) ? 0 : 1   ; "конец" только до 9-го
 *
 *   rec = &records[curr_idx]
 *   if rec->frame_number <= current_frame:
 *       input_state = rec->input_state           ; DS:0xCC9E
 *       demo_advance(st)
 *   return 0
 */
int tick_demo(DemoState* st) {
    if (is_demo_at_end(st)) {
        return (current_level == FINAL_LEVEL) ? 0 : 1;
    }
    MovRecord* rec = &st->records[st->curr_idx];
    if (rec->frame_number <= current_frame) {
        input_state = rec->input_state;
        demo_advance(st);
    }
    return 0;
}

/* KARNOV.EXE: cs:0x6E16 — load_demo.
 *
 *   sprintf(filename, "level%d.mov", current_level);  // DS:0xCE0
 *   loop:
 *       fp = fopen(filename, "rb")                    // DS:0xCEC
 *       if fp == NULL:
 *           clear_screen(); draw_text(10, "Please insert data disk");
 *           draw_text(12, "any key when ready");
 *           wait_for_key(); if key == 'Q': exit(0);
 *           goto loop;
 *
 *   fclose(fp)                                         // да, оригинал закрывает
 *   fp = fopen(filename, "rb")                         // и заново открывает
 *
 *   fread(&hdr_start, 2, 1, fp)                        // WORD1
 *   fread(&hdr_end,   2, 1, fp)                        // WORD2
 *   if hdr_start >= hdr_end: return error
 *
 *   for i in 0 .. (hdr_end - hdr_start) - 1:
 *       fread(&records[i].input_state,  1, 1, fp)
 *       fread(&records[i].frame_number, 2, 1, fp)
 *
 *   demo_play->curr_idx    = 0;
 *   demo_play->num_records = hdr_end;
 *   fclose(fp); return 0;
 */
int load_demo(int level_number) {
    char filename[64];
    std::sprintf(filename, "level%d.mov", level_number);

    if (!demo_play) {
        /* В оригинале буфер выделяется заранее; здесь делаем подстраховку. */
        std::fprintf(stderr, "demo_play not allocated\n");
        return 1;
    }
    demo_play->curr_idx = 0;
    demo_play->num_records = 0;

    /* Цикл «Please insert data disk» при отсутствии файла. */
    FILE* fp = nullptr;
    for (;;) {
        fp = karnov_fopen(filename, "rb");
        if (fp) break;
        clear_screen();
        char line[128];
        std::snprintf(line, sizeof(line),
                      "Please insert data disk (file not found: %s)", filename);
        draw_text(10, line);
        draw_text(12, "any key when ready (Q to quit)");
        int c = std::getchar();
        if (c == 'q' || c == 'Q' || c == EOF) {
            clear_screen();
            std::exit(c == EOF ? 1 : 0);
        }
        clear_screen();
    }

    /* Оригинал на этом месте делает fclose+fopen — возможно, чтобы сбросить
     * буферизацию DOS. Семантически это no-op; повторяем для верности. */
    std::fclose(fp);
    fp = karnov_fopen(filename, "rb");
    if (!fp) return 1;

    u16 hdr_start = 0, hdr_end = 0;
    if (std::fread(&hdr_start, 2, 1, fp) != 1) { std::fclose(fp); return 1; }
    if (std::fread(&hdr_end,   2, 1, fp) != 1) { std::fclose(fp); return 1; }
    if (hdr_start >= hdr_end) { std::fclose(fp); return 1; }

    int n = hdr_end - hdr_start;
    for (int i = 0; i < n; ++i) {
        if (std::fread(&demo_play->records[i].input_state,  1, 1, fp) != 1) {
            std::fclose(fp); return 1;
        }
        if (std::fread(&demo_play->records[i].frame_number, 2, 1, fp) != 1) {
            std::fclose(fp); return 1;
        }
    }

    demo_play->curr_idx    = 0;
    demo_play->num_records = (u16)hdr_end;
    std::fclose(fp);
    return 0;
}

/* KARNOV.EXE: cs:0x6FCC — save_demo.
 *
 *   tmp = *demo_record                  ; копия заголовка на стек
 *   sprintf(filename, "level%d.mov", current_level)
 *   fp = fopen(filename, "wb")          ; mode "wb" по DS:0xCFE
 *   if fp:
 *       if tmp.curr_idx < tmp.num_records:
 *           for r in records[curr_idx .. num_records-1]:
 *               fwrite(...)
 *       fclose(fp)
 */
int save_demo(int level_number) {
    if (!demo_record) return 1;

    DemoState header = { demo_record->curr_idx, demo_record->num_records, {} };
    char filename[64];
    std::sprintf(filename, "level%d.mov", level_number);

    FILE* fp = std::fopen(filename, "wb");
    if (!fp) return 1;

    /* Заголовок: 2 + 2 байта. */
    std::fwrite(&header.curr_idx,    2, 1, fp);
    std::fwrite(&header.num_records, 2, 1, fp);

    for (u16 i = header.curr_idx; i < header.num_records; ++i) {
        std::fwrite(&demo_record->records[i].input_state,  1, 1, fp);
        std::fwrite(&demo_record->records[i].frame_number, 2, 1, fp);
    }
    std::fclose(fp);
    return 0;
}

} /* namespace karnov */
