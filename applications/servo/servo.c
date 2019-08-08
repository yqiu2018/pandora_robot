#include "servo.h"

#define     DBG_SECTION_NAME    "servo"
#define     DBG_LEVEL           DBG_LOG
#include <rtdbg.h>

// 20ms; 1.5ms; +-1ms
#define SERVO_PERIOD          20000000
#define SERVO_MIDDLE_PULSE     1500000
#define SERVO_MAX_PULSE        2500000
#define SERVO_MIN_PULSE         500000

// note: 模拟舵机，数字舵机，转速可调，旋转角度范围，other？

servo_t servo_create(const char * pwm, int channel, float angle)
{
    // Malloc memory for new chassis
    servo_t new_servo = (servo_t) rt_malloc(sizeof(struct servo));
    if(new_servo == RT_NULL)
    {
        LOG_E("Falied to allocate memory for servo");
        return RT_NULL;
    }

    new_servo->pwmdev = (struct rt_device_pwm *)rt_device_find(pwm);
    new_servo->channel = channel;
    new_servo->angle_maximum = angle;
    if (new_servo -> pwmdev == RT_NULL)
    {
        LOG_E("Falied to find device on %s", pwm);
        servo_destroy(new_servo);
        return RT_NULL;
    }

    rt_pwm_set(new_servo->pwmdev, new_servo->channel, 0, 0);

    return new_servo;
}

rt_err_t servo_destroy(servo_t servo)
{
    rt_free(servo);

    return RT_EOK;
}

rt_err_t servo_enable(servo_t servo)
{
    if (servo == NULL)
        return RT_ERROR;

    rt_pwm_enable(servo->pwmdev, servo->channel);

    return RT_EOK;
}

rt_err_t servo_disable(servo_t servo)
{
    if (servo == NULL)
        return RT_ERROR;

    rt_pwm_disable(servo->pwmdev, servo->channel);

    return RT_EOK;
}

rt_err_t servo_set_angle(servo_t servo, float angle)
{
    rt_uint32_t set_point;

    if (servo == NULL)
        return RT_ERROR;

    set_point = SERVO_MIN_PULSE + (SERVO_MAX_PULSE - SERVO_MIN_PULSE) * angle / servo->angle_maximum;

    // if (angle >= 0)
    // {
    //     set_point = SERVO_MIDDLE_PULSE + (SERVO_MAX_PULSE - SERVO_MIDDLE_PULSE) * angle / servo->half_angle;
    // }
    // else
    // {
    //     set_point = SERVO_MIDDLE_PULSE - (SERVO_MIDDLE_PULSE - SERVO_MIN_PULSE) * (-angle) / servo->half_angle;
    // }
    
    rt_pwm_set(servo->pwmdev, servo->channel, SERVO_PERIOD, set_point);

    return RT_EOK;
}


servo_t l_servo = RT_NULL;
extern float stof(const char *s);

static void servo(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "enable")))
    {
        rt_kprintf("enable servo\n");
        servo_enable(l_servo);
    }
    if (!(rt_strcmp(argv[1], "disable")))
    {
        rt_kprintf("disable servo\n");
        servo_disable(l_servo);
    }

    if (argc < 3)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "angle")))
    {
        servo_set_angle(l_servo, stof(argv[2]));
        rt_kprintf("set-angle:%d\n", (int)stof(argv[2]));
    }

    if (argc < 5)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "create")))
    {
        l_servo = servo_create(argv[2], atoi(argv[3]), stof(argv[4]));
        rt_kprintf("dev:%s ch:%d angle:%d\n", argv[2], atoi(argv[3]), (int)stof(argv[4]));
    }
}
MSH_CMD_EXPORT(servo, servo test);

