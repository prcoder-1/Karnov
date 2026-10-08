/* Сгенерировано tools/gen_fn.py — не править. */
#ifndef ORIG_FN_H
#define ORIG_FN_H
#include <stdio.h>
#include "orig/ds.h"

FILE *bin_fopen(const char *name, const char *mode); /* @058B:0006 */
s16 bin_fclose(FILE *fp);                            /* @058B:004C */
void bin_reset(void);                                /* @058B:0066 */
u16 bin_fread(u8 *buf, u16 size, u16 count, FILE *fp); /* @058B:0077 */
s16 read_byte(u8 *dst, FILE *fp);                    /* @058B:0135 */
FILE *rtl_fopen(const char *name, const char *mode); /* @1BF7:0203 */

#endif
