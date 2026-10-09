/* Модуль 0x0365 (file 0x4E50) — уровень: буферы, загрузка LEVELn.BIN,
 * триггеры появления врагов (CHECKTRIGGERS), смена уровня. */
#include <stdio.h>
#include <string.h>
#include "orig/fn.h"
#include "orig/hw.h"

/* @0365:00ED file:04F3D spawn_active_object */
ActiveObject *spawn_active_object(EnemySpawn *rec)
{
    ActiveObject *o;
    o = new_object(rec->kind & 1);
    if (o == NULL)
        return NULL;
    o->obj_type = rec->kind;
    o->hit_points = rec->hp;
    o->x_world = rec->wx;
    o->y_world = rec->wy;
    o->num_bboxes = 1;
    o->box[0].hitflag = 1;
    o->box[0].dx = 0;
    o->box[0].dy = 0;
    init_active_object(o, ai_handlers[rec->type], rec->init_arg);
    rec->spawned = 1;
    return o;
}

/* @0365:01C1 file:05011 CHECKTRIGGERS — фаза 4: появление врагов, чья
 * зона накрывает игрока. Окно по x — от левого края экрана до игрок+0xA0. */
void CHECKTRIGGERS(void)
{
    s16 px = player_ptr->x_world, py = player_ptr->y_world;
    s16 first, last, si, d, key;
    EnemySpawn *rec;
    ActiveObject *o;

    trig_lo = 0;
    trig_hi = spawn_count - 1;
    key = (s16)((u16)cam_rect[0] << tile_shift);
    do {
        si = (s16)(trig_lo + trig_hi) >> 1;
        if (spawns[si].x < key)
            trig_lo = si;
        else
            trig_hi = si;
    } while (trig_hi - trig_lo > 1);
    first = trig_lo;
    trig_lo = 0;
    trig_hi = spawn_count - 1;
    key = px + 0xA0;
    do {
        si = (s16)(trig_lo + trig_hi) >> 1;
        if (spawns[si].x < key)
            trig_lo = si;
        else
            trig_hi = si;
    } while (trig_hi - trig_lo > 1);
    last = trig_hi;

    for (si = first, rec = &spawns[first]; si <= last; si++, rec++) {
        if (rec->spawned != 0)
            continue;
        d = px - rec->x;
        if (d <= 0)
            d = -d;
        if (d >= rec->rx)
            continue;
        d = py - rec->y;
        if (d <= 0)
            d = -d;
        if (d >= rec->ry)
            continue;
        if (rec->kind == 1 || rec->kind == 7)
            kill_sky_chain();
        o = spawn_active_object(rec);
        if (o == NULL)
            continue;
        if (plr.current_level == 9) {
            if (rec->type == 0x38)
                goal_idx = o->self_idx;
        } else if (spawn_count - 1 == si) {
            goal_idx = o->self_idx;
        }
    }
}

/* @0365:0366 file:051B6 read_object_regions */
s16 read_object_regions(FILE *fp)
{
    if (bin_fread((u8 *)&region_count, 2, 1, fp) != 1 ||
        bin_fread((u8 *)regions, 0xC, (u16)region_count, fp) != (u16)region_count)
        return 1;
    player_spawn.wx = regions[0].spawn_x;
    player_spawn.wy = regions[0].spawn_y;
    return 0;
}

/* @0365:03C6 file:05216 read_enemies — сортировка выбором по x */
s16 read_enemies(FILE *fp)
{
    EnemySpawn tmp;
    s16 i, j, min, minx;
    if (bin_fread((u8 *)&spawn_count, 2, 1, fp) != 1 ||
        bin_fread((u8 *)spawns, 0x14, (u16)spawn_count, fp) != (u16)spawn_count)
        return 1;
    for (i = 0; i < spawn_count - 1; i++) {
        min = i;
        minx = spawns[i].x;
        for (j = i + 1; j < spawn_count; j++)
            if (spawns[j].x < minx) {
                min = j;
                minx = spawns[j].x;
            }
        if (min != i) {
            memcpy(&tmp, &spawns[min], 0x14);
            memcpy(&spawns[min], &spawns[i], 0x14);
            memcpy(&spawns[i], &tmp, 0x14);
        }
    }
    return 0;
}

/* @0365:0500 file:05350 load_level — коды ошибок как в оригинале */
s16 load_level(s16 level)
{
    char name[0x14];
    FILE *fp = NULL;
    u8 *gfx;
    u8 cga_skip[0x28], ega[0x50];
    u16 ntiles;
    u8 idx;
    u8 attr;
    s16 si;
    u8 *src, *dst;
    s32 n;

    sprintf(name, "level%1d.bin", level);
    wait_for_file(name, level >= 7 ? "Please insert program disk" : "Please insert data disk");
    fp = bin_fopen(name, "rb");
    if (fp == NULL)
        return 1;
    if (read_object_regions(fp)) {
        bin_fclose(fp);
        return 0xB;
    }
    if (read_enemies(fp)) {
        bin_fclose(fp);
        return 0x15;
    }
    if (bin_fread((u8 *)&ntiles, 2, 1, fp) != 1) {
        bin_fclose(fp);
        return 0x1F;
    }
    if (bin_fread((u8 *)&map_rows, 2, 1, fp) != 1) {
        bin_fclose(fp);
        return 0x29;
    }
    if (bin_fread((u8 *)&map_cols, 2, 1, fp) != 1) {
        bin_fclose(fp);
        return 0x33;
    }
    map_row_bytes = map_cols * 2;

    gfx = tile_gfx;
    for (si = 0; si < (s16)ntiles; si++) {
        if ((si & 0xF) == 0)
            DOCONTROLS();
        if (bin_fread(&idx, 1, 1, fp) != 1) {
            bin_fclose(fp);
            return 0x3D;
        }
        if (bin_fread(&attr, 1, 1, fp) != 1) {
            bin_fclose(fp);
            return 0x47;
        }
        if (idx < 200)
            tile_attr[idx] = attr;
        if (level_mode == 0 || level_mode == 2) {
            gfx = tile_gfx + idx * 0x28;
            if (bin_fread(gfx, 0x28, 1, fp) != 1) {
                bin_fclose(fp);
                return 0x51;
            }
            if (bin_fread(ega, 0x50, 1, fp) != 1) {
                bin_fclose(fp);
                return 0x5B;
            }
            continue;
        }
        gfx = tile_gfx + idx * 0x50;
        if (bin_fread(cga_skip, 0x28, 1, fp) != 1)
            goto err1;
        if (level_mode == 3) {
            if (bin_fread(ega, 0x50, 1, fp) != 1)
                goto err1;
            tandy_convert(gfx, ega);
        } else if (bin_fread(gfx, 0x50, 1, fp) != 1) {
            goto err1;
        }
    }
    if (bin_fread(map_buf, (u16)(map_rows * map_cols), 1, fp) != 1) {
        bin_fclose(fp);
        return 0x65;
    }
    bin_fclose(fp);

    for (si = level_mode == 0 || level_mode == 2 ? 0x28 : 0x50; --si >= 0;)
        tile_gfx[si] = 0;

    /* Карта на диске — байт на клетку; разворачивается с конца в пары
     * (тайл, атрибут). */
    src = map_buf + (u16)(map_rows * map_cols) - 1;
    dst = map_buf + (u16)(map_rows * map_cols * 2) - 1;
    for (n = (s16)(map_rows * map_cols - 1); n >= 0; n--) {
        *dst-- = (u8)TILE_ATTR(*src);
        *dst-- = *src--;
    }
    return 0;
err1:
    bin_fclose(fp);
    return 1;
}

/* @0365:0A04 file:05854 enter_level — в демо уровни 0..2 по кругу
 * (смена каждые 4 захода); загрузка под музыку загрузчика */
void enter_level(void)
{
    s16 err, v;
    if (demo_mode != 0) {
        v = demo_buf.count == 0 ? 1 : 0;
        if (++demo_level_ctr > 3) {
            plr.current_level = (plr.current_level + 1) % 3;
            demo_level_ctr = 0;
            v++;
        }
        if (plr.current_level == 0)
            demo_level_ctr = 0x63;
        demo_set_attract(v);
    }
    if (plr.current_level == loaded_level)
        return;
    play_music(SND_STOP, 0x1000, 0);
    delay(0x37);
    play_music(music_loader, 0, 1);
    level_loading = 1;
    err = load_level(plr.current_level);
    if (err != 0) {
        sprintf(msg_buf, "MAP ERR %d", err);
        con_puts("\r\n");
        con_puts(msg_buf);
        delay(0x370);
        fatal(1, msg_buf);
    }
    err = sprite_loader_main();
    if (err != 0) {
        sprintf(msg_buf, "sprite bummer #%d", err);
        con_puts("\r\n");
        con_puts(msg_buf);
        delay(0x370);
        fatal(1, msg_buf);
    }
    /* порт: загрузка мгновенная — музыка загрузчика доигрывает цикл,
     * клавиша прерывает (и остаётся в буфере) */
    if (music_enabled) {
        u16 c = mus_cycles;
        while (mus_cycles == c && rtl_bioskey(1) == 0)
            hw_wait_tick();
    }
    loaded_level = plr.current_level;
    level_loading = 0;
    play_music(SND_STOP, 0x1000, 0);
    delay(0x37);
}
