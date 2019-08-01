#include <rtthread.h>
#include <ahrs.h>

#include <stdlib.h>
#include "mpu6xxx.h"

#define MPU6XXX_DEVICE_NAME "i2c3"
#define SAMPLE_TIME         0.001

static struct mpu6xxx_device *mpu_dev;

/* Test function */
static int mpu6xxx_find()
{
    /* Initialize mpu6xxx, The parameter is RT_NULL, means auto probing for i2c*/
    mpu_dev = mpu6xxx_init(MPU6XXX_DEVICE_NAME, RT_NULL);

    if (mpu_dev == RT_NULL)
    {
        rt_kprintf("mpu6xxx init failed\n");
        return -1;
    }
    rt_kprintf("mpu6xxx init succeed\n");
    return 0;
}

static void ahrs_test(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!rt_strcmp("init", argv[1]))
    {
        mpu6xxx_find();
    }

    if (argc < 3)
    {
        return;
    }
    
    if (!rt_strcmp("update", argv[1]))
    {
        struct mpu6xxx_3axes raw_acc, raw_gyro;
        float state[3];
        uint32_t total = atoi(argv[2]);
        for (uint32_t i=0; i<total; i++)
        {
            mpu6xxx_get_accel(mpu_dev, &raw_acc);
            mpu6xxx_get_gyro(mpu_dev, &raw_gyro);
            ahrs_9dof_update(raw_gyro.x, raw_gyro.y, raw_gyro.z, raw_acc.x, raw_acc.y, raw_acc.z, 0,0,0, SAMPLE_TIME, state);
            rt_kprintf("ahrs-r-p-y: %d.%d %d.%d %d.%d\n", (int)(state[0]), (int)state[0]*100%100, (int)(state[1]), (int)state[1]*100%100,(int)(state[2]), (int)state[2]*100%100);
            rt_thread_mdelay(1);
        }
    }
    if (!rt_strcmp("update2", argv[1]))
    {
        struct mpu6xxx_3axes raw_acc, raw_gyro;
        float state[3];
        uint32_t total = atoi(argv[2]);
        for (uint32_t i=0; i<total; i++)
        {
            mpu6xxx_get_accel(mpu_dev, &raw_acc);
            mpu6xxx_get_gyro(mpu_dev, &raw_gyro);
            MadgwickAHRSupdate(raw_gyro.x, raw_gyro.y, raw_gyro.z, raw_acc.x, raw_acc.y, raw_acc.z, 0.0f,0.0f,0.0f, SAMPLE_TIME,  state);
            rt_kprintf("ahrs-r-p-y: %d.%d %d.%d %d.%d\n", (int)(state[0]), (int)state[0]*100%100, (int)(state[1]), (int)state[1]*100%100,(int)(state[2]), (int)state[2]*100%100);
            rt_thread_mdelay(1);
        }
    }
}
MSH_CMD_EXPORT(ahrs_test, ahrs test);
