#ifndef __GIMBAL_H__
#define __GIMBAL_H__

#include <rtthread.h>
#include <servo.h>

typedef struct gimbal *gimbal_t;

struct gimbal
{
    servo_t servo_x;    // control phi angle
    servo_t servo_z;    // control theta angle
};

gimbal_t gimbal_create(servo_t servo_x, servo_t servo_z);
rt_err_t gimbal_destroy(gimbal_t gimbal);
rt_err_t gimbal_enable(gimbal_t gimbal);
rt_err_t gimbal_disable(gimbal_t gimbal);
rt_err_t gimbal_set_angle(gimbal_t gimbal, float theta, float phi);

#endif // __GIMBAL_H__

