/* orig/port.h — служебное порта, не из оригинала. */
#ifndef ORIG_PORT_H
#define ORIG_PORT_H
#include "orig/ds.h"

typedef struct {
    int headless;           /* без окна и звука */
    int fast;               /* без выдержки реального времени */
    int no_audio;
    int frames;             /* >0: выйти после N кадров главного цикла */
} PortOpt;
extern PortOpt port_opt;

/* Начало кадра главного цикла. */
void port_frame_hook(void);
/* События SDL (клавиатура, закрытие окна). */
void port_pump_events(void);

#endif
