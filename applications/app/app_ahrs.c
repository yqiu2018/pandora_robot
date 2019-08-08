#include <rtthread.h>
#include <ahrs.h>
#include <mpu6xxx.h>
#include <ano.h>

// Ahrs Thread
#define THREAD_DELAY_TIME           5
#define THREAD_PRIORITY             11
#define THREAD_STACK_SIZE          1024
#define THREAD_TIMESLICE             5

#define MPU6XXX_DEVICE_NAME  "i2c2"

struct axes_3f
{
    float x;
    float y;
    float z;
};


static rt_thread_t tid_ahrs = RT_NULL;
static struct mpu6xxx_device *dev;
static struct mpu6xxx_3axes accel, gyro, mag;

static void ahrs_app_thread(void *param)
{
    float rpy_state[3];
    struct axes_3f f_acc;
    struct axes_3f f_gyro;
    struct axes_3f f_mag;

    dev = mpu6xxx_init(MPU6XXX_DEVICE_NAME, RT_NULL);

    if (dev == RT_NULL)
    {
        rt_kprintf("ahrs imu init failed\n");
    }
    rt_kprintf("ahrs imu init succeed\n");

    while(1)
    {
        rt_thread_mdelay(THREAD_DELAY_TIME);
        mpu6xxx_get_accel(dev, &accel);
        mpu6xxx_get_gyro(dev, &gyro);
        mpu6xxx_get_mag(dev, &mag);

        f_acc.x = accel.x * 2 / 32768;
        f_acc.y = accel.y * 2 / 32768;
        f_acc.z = accel.z * 2 / 32768;

        f_gyro.x = gyro.x * 250 / 32768;
        f_gyro.y = gyro.y * 250 / 32768;
        f_gyro.z = gyro.z * 250 / 32768;

        f_mag.x = mag.x * 0.15;
        f_mag.y = mag.y * 0.15;
        f_mag.z = mag.z * 0.15;

        ahrs_9dof_update(f_gyro.x, f_gyro.y, f_gyro.z,
                        f_acc.x, f_acc.y, f_acc.z,
                        f_mag.x, f_mag.y, f_mag.z, (float)THREAD_DELAY_TIME/1000, rpy_state);


        // ahrs_9dof_update(gyro.x, gyro.y, gyro.z,
        //                 accel.x, accel.y, accel.z,
        //                 mag.x, mag.y, mag.z, (float)THREAD_DELAY_TIME/1000, rpy_state);

        ano_send_status(rpy_state[0], rpy_state[1], rpy_state[2], 0,0,0);
        ano_send_senser(accel.x, accel.y, accel.z,
            gyro.x, gyro.y, gyro.z,
            mag.x, mag.y, mag.z, 0);

        // rt_kprintf("rpy:%d %d %d\n", (int)(rpy_state[0]*10), (int)(rpy_state[1]*10), (int)(rpy_state[2]*10));
        // rt_kprintf("accel.x = %3d, accel.y = %3d, accel.z = %3d ", accel.x, accel.y, accel.z);
        // rt_kprintf("gyro.x = %3d gyro.y = %3d, gyro.z = %3d ", gyro.x, gyro.y, gyro.z);
        // rt_kprintf("mag.x = %3d mag.y = %3d, mag.z = %3d\n", mag.x, mag.y, mag.z);
    }
}

void ahrs_app_init(void)
{
    // thread
    tid_ahrs = rt_thread_create("tahrs",
                              ahrs_app_thread, RT_NULL,
                              THREAD_STACK_SIZE,
                              THREAD_PRIORITY, THREAD_TIMESLICE);

    if (tid_ahrs != RT_NULL)
    {
        rt_thread_startup(tid_ahrs);
    }
}
