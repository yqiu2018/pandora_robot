#ifndef __LASER_LAUCHER_H__
#define __LASER_LAUCHER_H__

#include <rtthread.h>
#include <rtdevice.h>

void laser_launcher_init(void);
void laser_launcher_open(void);
void laser_launcher_close(void);
void laser_launcher_keep(rt_int32_t ms);

#endif // __LASER_LAUCHER_H__
