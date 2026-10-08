/* main.c — точка входа порта (не из оригинала): платформа, реестр DS,
 * затем main() оригинала (@0330:01C2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "orig/fn.h"
#include "orig/port.h"

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) {
            int bad = ds_check();
            printf("ds_check: %s, переменных %u, функций %u\n",
                   bad ? "ОШИБКИ" : "ok", ds_nvars, fn_nreg);
            return bad;
        }
    }
    if (ds_check())
        return 1;
    ds_init();
    return 0;
}
