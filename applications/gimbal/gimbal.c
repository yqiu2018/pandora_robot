#include "gimbal.h"

#define     DBG_SECTION_NAME    "gimbal"
#define     DBG_LEVEL           DBG_LOG
#include <rtdbg.h>

gimbal_t gimbal_create(servo_t servo_x, servo_t servo_z)
{
    // Malloc memory for new chassis
    gimbal_t new_gimbal = (gimbal_t) rt_malloc(sizeof(struct gimbal));
    if(new_gimbal == RT_NULL)
    {
        LOG_E("Falied to allocate memory for gimbal");
        return RT_NULL;
    }

    new_gimbal->servo_x = servo_x;
    new_gimbal->servo_z = servo_z;

    return new_gimbal;
}

rt_err_t gimbal_destroy(gimbal_t gimbal)
{
    servo_destroy(gimbal->servo_x);
    servo_destroy(gimbal->servo_z);
    rt_free(gimbal);
    
    return RT_EOK;
}

rt_err_t gimbal_enable(gimbal_t gimbal)
{
    servo_enable(gimbal->servo_x);
    servo_enable(gimbal->servo_z);

    return RT_EOK;
}

rt_err_t gimbal_disable(gimbal_t gimbal)
{
    servo_disable(gimbal->servo_x);
    servo_disable(gimbal->servo_z);

    return RT_EOK;
}

rt_err_t gimbal_set_angle(gimbal_t gimbal, float theta, float phi)
{
    servo_set_angle(gimbal->servo_x, theta);
    servo_set_angle(gimbal->servo_z, phi);

    return RT_EOK;
}
