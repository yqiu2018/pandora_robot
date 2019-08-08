#include "laser_launcher.h"

#define LASER_PIN       63  // GET_PIN(C,4)

void laser_launcher_init()
{
    rt_pin_mode(LASER_PIN, PIN_MODE_OUTPUT);
}

void laser_launcher_open()
{
    rt_pin_write(LASER_PIN, PIN_HIGH);
}

void laser_launcher_close()
{
    rt_pin_write(LASER_PIN, PIN_LOW);
}

void laser_launcher_keep(rt_int32_t ms)
{
    laser_launcher_open();
    rt_thread_mdelay(ms);
    laser_launcher_close();
}
