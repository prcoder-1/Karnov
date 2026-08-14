/* test_io.cpp — проверка совместимости RLE-декомпрессора и парсеров с
 * реальными файлами LEVEL%d.BIN и LEVEL%d.MOV.
 *
 * Запускается из родительской директории игры:
 *   ./test_io  →  читает ../LEVEL0.BIN, ../LEVEL1.MOV, ...
 */
#include "karnov.h"
#include "globals.h"
#include "file_io.h"
#include "level.h"
#include "demo.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>

using namespace karnov;

static int g_passed = 0, g_failed = 0;

#define CHECK(cond, fmt, ...) do { \
    if (cond) { ++g_passed; std::printf("  [OK] " fmt "\n", ##__VA_ARGS__); } \
    else      { ++g_failed; std::printf("  [FAIL] " fmt "\n", ##__VA_ARGS__); } \
} while (0)

/* ─── Тест 1: RLE-декомпрессор побайтово ────────────────────────────── */
static void test_rle_unit() {
    std::printf("=== Test 1: RLE decompressor unit cases ===\n");

    /* Создаём временные файлы с известным содержимым и читаем их через
     * bin_fread. */
    auto write_file = [](const char* name, const std::vector<u8>& bytes) {
        FILE* f = std::fopen(name, "wb");
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    };

    auto read_via_bin = [](const char* name, std::size_t n) -> std::vector<u8> {
        std::vector<u8> out(n);
        FILE* f = bin_fopen(name, "rb");
        bin_fread(out.data(), 1, n, f);
        bin_fclose(f);
        return out;
    };

    /* (a) Литерал 0x5A: bytes 5A 5A  → выход 5A. */
    write_file("/tmp/t1.bin", {0x5A, 0x5A, 0x01, 0x02});
    auto a = read_via_bin("/tmp/t1.bin", 3);
    CHECK(a[0] == 0x5A && a[1] == 0x01 && a[2] == 0x02,
          "5A 5A 01 02 -> 5A 01 02 (got %02X %02X %02X)", a[0], a[1], a[2]);

    /* (b) Простой повтор: 5A 04 AA → 4 байта AA. */
    write_file("/tmp/t2.bin", {0x5A, 0x04, 0xAA});
    auto b = read_via_bin("/tmp/t2.bin", 4);
    CHECK(b[0]==0xAA && b[1]==0xAA && b[2]==0xAA && b[3]==0xAA,
          "5A 04 AA -> AAAAAAAA (got %02X %02X %02X %02X)", b[0],b[1],b[2],b[3]);

    /* (c) N=0 → 256 повторов. */
    write_file("/tmp/t3.bin", {0x5A, 0x00, 0x77});
    auto c = read_via_bin("/tmp/t3.bin", 256);
    bool all_77 = true;
    for (int i = 0; i < 256; ++i) if (c[i] != 0x77) { all_77 = false; break; }
    CHECK(all_77, "5A 00 77 -> 256x 0x77");

    /* (d) Смешанное чтение через границу запроса (1+1+1 байтов). */
    write_file("/tmp/t4.bin", {0x01, 0x5A, 0x03, 0xFF, 0x02});
    FILE* f = bin_fopen("/tmp/t4.bin", "rb");
    u8 by;
    std::vector<u8> d;
    while (bin_fread(&by, 1, 1, f) == 1) d.push_back(by);
    bin_fclose(f);
    CHECK(d.size() == 5 && d[0]==0x01 && d[1]==0xFF && d[2]==0xFF && d[3]==0xFF && d[4]==0x02,
          "01 5A 03 FF 02 -> 01 FF FF FF 02 (got %zu bytes)", d.size());
}

/* ─── Тест 2: чтение реальных MOV файлов ────────────────────────────── */
static void test_mov_files() {
    std::printf("=== Test 2: real MOV files ===\n");

    /* Выделить буфер демо. */
    static u8 buf[4 + 256*sizeof(MovRecord)];
    std::memset(buf, 0, sizeof(buf));
    demo_play = reinterpret_cast<DemoState*>(buf);
    current_level = 0;

    /* Ожидаемые значения из FORMATS.md (проверены при анализе). */
    struct { int level; u16 expected_records; u16 expected_last_frame; } cases[] = {
        {0,  43,  514},
        {1, 167,  933},
        {2, 165, 1229},
    };
    for (auto& c : cases) {
        current_level = static_cast<u16>(c.level);
        char filename[64];
        std::snprintf(filename, sizeof(filename), "../LEVEL%d.MOV", c.level);
        FILE* f = std::fopen(filename, "rb");
        if (!f) { std::printf("  [SKIP] %s not found\n", filename); continue; }
        /* Парсим напрямую (load_demo использует sprintf+cwd). */
        u16 hdr_start, hdr_end;
        std::fread(&hdr_start, 2, 1, f);
        std::fread(&hdr_end,   2, 1, f);
        demo_play->curr_idx = 0;
        demo_play->num_records = hdr_end;
        for (int i = 0; i < hdr_end; ++i) {
            std::fread(&demo_play->records[i].input_state,  1, 1, f);
            std::fread(&demo_play->records[i].frame_number, 2, 1, f);
        }
        std::fclose(f);

        CHECK(demo_play->num_records == c.expected_records,
              "LEVEL%d.MOV num_records=%u (expected %u)",
              c.level, demo_play->num_records, c.expected_records);
        CHECK(demo_play->records[c.expected_records-1].frame_number == c.expected_last_frame,
              "LEVEL%d.MOV last frame=%u (expected %u)",
              c.level,
              demo_play->records[c.expected_records-1].frame_number,
              c.expected_last_frame);
    }
}

/* ─── Тест 3: RLE-распаковка LEVEL%d.BIN, проверка ожидаемой длины ──── */
static void test_bin_rle_size() {
    std::printf("=== Test 3: real BIN files (RLE decompressed size) ===\n");

    /* Ожидаемые размеры после распаковки (полученные при анализе). */
    struct { int level; std::size_t expected_decoded; } cases[] = {
        {0, 17950},
        {1, 26991},
        {2, 26586},
        {3, 26634},
        {9, 36437},
    };
    for (auto& c : cases) {
        char filename[64];
        std::snprintf(filename, sizeof(filename), "../LEVEL%d.BIN", c.level);
        FILE* f = bin_fopen(filename, "rb");
        if (!f) { std::printf("  [SKIP] %s\n", filename); continue; }
        std::size_t total = 0;
        u8 byte;
        while (bin_fread(&byte, 1, 1, f) == 1) ++total;
        bin_fclose(f);
        CHECK(total == c.expected_decoded,
              "LEVEL%d.BIN decoded=%zu (expected %zu)",
              c.level, total, c.expected_decoded);
    }
}

/* ─── Тест 4: распаковка LEVEL0.BIN, проверка структуры заголовка ──── */
static void test_bin_header_parse() {
    std::printf("=== Test 4: LEVEL0.BIN parse ===\n");

    FILE* f = bin_fopen("../LEVEL0.BIN", "rb");
    if (!f) { std::printf("  [SKIP]\n"); return; }

    u16 n_obj = 0;
    bin_fread(&n_obj, 2, 1, f);
    CHECK(n_obj == 1, "objects count = %u (expected 1)", n_obj);

    ObjectRegion obj;
    bin_fread(&obj, sizeof(obj), 1, f);
    CHECK(obj.x0 == 0 && obj.y0 == 0 && obj.x1 == 9999 && obj.y1 == 9999
          && obj.vx == 56 && obj.vy == 70,
          "obj[0] = (%u,%u,%u,%u,%u,%u) (expected 0,0,9999,9999,56,70)",
          obj.x0, obj.y0, obj.x1, obj.y1, obj.vx, obj.vy);

    u16 n_en = 0;
    bin_fread(&n_en, 2, 1, f);
    CHECK(n_en == 6, "enemies count = %u (expected 6)", n_en);

    EnemySpawn en;
    bin_fread(&en, sizeof(en), 1, f);    /* first */
    CHECK(en.type == 2 && en.subtype == 0,
          "enemy[0] type=%u subtype=%u (expected 2, 0)", en.type, en.subtype);
    bin_fread(&en, sizeof(en), 1, f);    /* second */
    CHECK(en.type == 6 && en.subtype == 5 && en.cur_x == 8 && en.cur_y == 100,
          "enemy[1] = (type=%u sub=%u cx=%u cy=%u) (expected 6,5,8,100)",
          en.type, en.subtype, en.cur_x, en.cur_y);

    bin_fclose(f);
}

/* ─── Тест 5: end-to-end load_level() для каждого уровня ─────────────── */
static void test_load_level_e2e() {
    std::printf("=== Test 5: load_level() end-to-end ===\n");

    /* load_level использует sprintf+cwd, поэтому уходим на уровень выше. */
    if (chdir("..") != 0) {
        std::printf("  [SKIP] chdir(..) failed\n");
        return;
    }

    /* Выделить статические буферы (как в main.cpp). */
    static ObjectRegion level_objects_buf[16];
    static EnemySpawn   level_enemies_buf[256];
    static u8           map_buffer_buf  [256 * MAP_ROW_EXT_BYTES];
    static u8           tile_pixels_buf [256 * 512];
    level_objects = level_objects_buf;
    level_enemies = level_enemies_buf;
    map_buffer    = map_buffer_buf;
    tile_pixels   = tile_pixels_buf;

    /* Ожидаемые заголовки карты — выведены из распакованных файлов. */
    struct { int level; u16 rows; u16 ntiles; u16 tile_size_bytes; } cases[] = {
        {0, 144, 12, 20},
        {1, 151, 31, 225},
        {2,  88, 56, 242},
        {3,  94, 64, 208},
        {9, 147, 63, 247},
    };
    for (auto& c : cases) {
        int rc = load_level(static_cast<u16>(c.level));
        CHECK(rc == LL_OK, "load_level(%d) rc=%d (expected %d)",
              c.level, rc, LL_OK);
        if (rc == LL_OK) {
            CHECK(tile_size_bytes == c.tile_size_bytes,
                  "  L%d tile_size_bytes=%u (expected %u)",
                  c.level, tile_size_bytes, c.tile_size_bytes);
            CHECK(num_unique_tiles == c.ntiles,
                  "  L%d num_unique_tiles=%u (expected %u)",
                  c.level, num_unique_tiles, c.ntiles);
        }
    }

    if (chdir("karnov_cpp") != 0) { /* вернуться, чтобы остальные относительные пути работали */ }
}

int main() {
    test_rle_unit();
    test_mov_files();
    test_bin_rle_size();
    test_bin_header_parse();
    test_load_level_e2e();
    std::printf("\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
