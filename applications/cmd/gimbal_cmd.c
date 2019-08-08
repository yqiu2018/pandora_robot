#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdio.h>
#include <stdlib.h>
#include <gimbal.h>

static gimbal_t gimbal;

extern float stof(const char *s);

static void gimbal_init(void)
{
    servo_t servo_x = servo_create("pwm3", 1, 210);
    servo_t servo_z = servo_create("pwm3", 2, 210);
    gimbal = gimbal_create(servo_x, servo_z);
    gimbal_enable(gimbal);
    rt_kprintf("gimbal enable\n");
}

static void print_help()
{
    rt_kprintf("Usage: mobile_robot [x] [y] [w] [duration]\n");
}

static void gimbal_test(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "init")))
    {
        gimbal_init();
    }

    if (argc < 4)
    {
        print_help();
        return;
    }
    if (!(rt_strcmp(argv[1], "angle")))
    {
        gimbal_set_angle(gimbal, stof(argv[2]), stof(argv[3]));
        rt_kprintf("angle:%d %d\n", (int)stof(argv[2]), (int)stof(argv[3]));
    }
}
MSH_CMD_EXPORT(gimbal_test, gimbal test);
