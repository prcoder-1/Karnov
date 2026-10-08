/* orig/ds_vars.h — реестр глобалов KARNOV.EXE: тип, имя, адрес в DS.
 * Включается из ds.h (объявления) и ds.c (определения + реестр).
 * Порядок — по возрастанию адреса; пересечения запрещены (ds_check).
 *
 *  DSV(тип, имя, адрес)         скаляр
 *  DSA(тип, имя, адрес, n)      массив
 *  DSS(тип, имя, адрес, n)      массив структур с раскладкой тип_layout
 *  DSP(тип, имя, адрес)         far-указатель на данные
 *  DSPA(тип, имя, адрес, n)     массив far-указателей на данные
 *  DSF(имя, адрес)              far-указатель на код
 *  DSFA(имя, адрес, n)          массив far-указателей на код
 *  DSX(тип, имя, адрес, n)      есть в DS, но в сверке не участвует
 */

/* --- объекты (модуль 0x006E) */
DSV(s16, objloop_chain,  0x258C)    /* индекс parity-цепочки во внешних циклах */
DSV(s16, objloop_k,      0x258E)    /* позиция в active_index (шаг 2) */
DSV(s16, objloop_end,    0x2590)
DSA(s16, active_index,   0x2594, 128)
DSA(s16, active_count,   0x2694, 2)
DSS(ActiveObject, all_objects, 0x2824, 128)

/* --- RLE-ввод (модуль 0x058B) */
DSV(s16, io_buf_avail,   0xD566)
DSV(s16, rle_in_run,     0xD568)
DSA(u8,  io_buffer,      0xD56A, 0x200)
DSV(u8,  rle_run_byte,   0xD76A)
DSV(s16, io_buf_pos,     0xD76B)
DSV(u8,  rle_run_left,   0xD76D)
