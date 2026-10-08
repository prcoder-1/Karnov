/* orig/port.h — служебное порта, не из оригинала. */
#ifndef ORIG_PORT_H
#define ORIG_PORT_H
#include "orig/ds.h"

typedef struct {
    int verbose;
    int headless;
    int fast;               /* без выдержки реального времени */
    int no_audio;
    int frames;             /* >0: выйти после N кадров главного цикла */
    const char *dsdump;     /* файл покадрового дампа DS */
} PortOpt;
extern PortOpt port_opt;

int ds_check(void);
/* Непереведённая функция оригинала: печать и аварийный выход. */
void port_todo(const char *what);
/* То же без остановки: печать при первом вызове. */
void port_todo_once(const char *what);
/* Начало кадра главного цикла (место хука DSDUMP.EXE): дамп DS. */
void port_frame_hook(void);
/* События SDL (клавиатура, закрытие окна). */
void port_pump_events(void);

#endif
