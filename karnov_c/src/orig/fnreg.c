/* Сгенерировано tools/gen_fn.py — не править. */
#include "orig/fn.h"

const FnReg fn_reg[] = {
    { 0x058B, 0x0006, (farfn)bin_fopen, "bin_fopen" },
    { 0x058B, 0x004C, (farfn)bin_fclose, "bin_fclose" },
    { 0x058B, 0x0066, (farfn)bin_reset, "bin_reset" },
    { 0x058B, 0x0077, (farfn)bin_fread, "bin_fread" },
    { 0x058B, 0x0135, (farfn)read_byte, "read_byte" },
    { 0x1BF7, 0x0203, (farfn)rtl_fopen, "rtl_fopen" },
};
const unsigned fn_nreg = 6;
