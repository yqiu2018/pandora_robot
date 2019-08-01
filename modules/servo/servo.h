#ifndef __SERVO_H__
#define __SERVO_H__

#include <rtthread.h>
#include <rtdevice.h>

typedef struct servo *servo_t;

struct servo
{
    struct rt_device_pwm    *pwmdev;
    int                     channel;
    float                   angle_maximum;
};

servo_t  servo_create(const char * pwm, int channel, float angle);
rt_err_t servo_destroy(servo_t servo);
rt_err_t servo_enable(servo_t servo);
rt_err_t servo_disable(servo_t servo);
rt_err_t servo_set_angle(servo_t servo, float angle);

#endif //  __SERVO_H__

