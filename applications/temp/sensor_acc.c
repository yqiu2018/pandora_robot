//椭球校准.cpp
//最小二乘的椭球拟合
//((x-x0)/A)^2 + ((y-y0)/B)^2 + ((z-z0)/C)^2 = 1 的空间任意椭球方程式
//x^2 + a*y^2 + b*z^2 + c*x + d*y + e*z + f = 0  简化后的方程
//问题转换为由a,b,c,d,e,f,来求解x0，y0，z0 以及 A,B,C
//作者：摩天轮1111
//知乎ID：摩天轮1111  转载请注明出处 尊重劳动者成果

// #include "stdafx.h"
// #include "stdio.h"
// #include "string.h"
// #include "math.h"

#include <rtthread.h>

#include <rtdevice.h>
#include <stdio.h>
#include "mpu6xxx.h"

/* Default configuration, please change according to the actual situation, support i2c and spi device name */
#define MPU6XXX_DEVICE_NAME "i2c3"

#define ACC_A_PARAM 0.798170f
#define ACC_B_PARAM -2.147398f
#define ACC_C_PARAM 101.378105f
#define ACC_D_PARAM 12.727443f
#define ACC_E_PARAM 2944.900116f
#define ACC_F_PARAM -767533.946368f

static struct mpu6xxx_device *mpu_dev;

/* Test function */
static int mpu6xxx_test()
{

    // struct mpu6xxx_3axes accel, gyro;

    /* Initialize mpu6xxx, The parameter is RT_NULL, means auto probing for i2c*/
    mpu_dev = mpu6xxx_init(MPU6XXX_DEVICE_NAME, RT_NULL);

    if (mpu_dev == RT_NULL)
    {
        rt_kprintf("mpu6xxx init failed\n");
        return -1;
    }
    rt_kprintf("mpu6xxx init succeed\n");

    // mpu6xxx_get_accel(mpu_dev, &accel);
    // mpu6xxx_get_gyro(mpu_dev, &gyro);

    // rt_kprintf("accel.x = %3d, accel.y = %3d, accel.z = %3d ", accel.x, accel.y, accel.z);
    // rt_kprintf("gyro.x = %3d gyro.y = %3d, gyro.z = %3d\n", gyro.x, gyro.y, gyro.z);

    return 0;
}

#define CALIBRATE_SAMPLE_COUNT 512
#define AVERAGE_COUNT          16
#define WINDOW_SIZE            1
#define MOVEMENT_END_COUNT     6

int32_t calibration_x = 0;
int32_t calibration_y = 0;
int32_t acceleration_x[2] = {0, 0};
int32_t acceleration_y[2] = {0, 0};
int32_t velocity_x[2] = {0, 0};
int32_t velocity_y[2] = {0, 0};
int32_t position_x[2] = {0, 0};
int32_t position_y[2] = {0, 0};
uint32_t acc_zero_x_count = 0;
uint32_t acc_zero_y_count = 0;

static void acc_param_clear()
{
    acceleration_x[1]   = 0;
    acceleration_y[1]   = 0;
    velocity_x[1]       = 0;
    velocity_y[1]       = 0;
    position_x[1]       = 0;
    position_y[1]       = 0;
    acceleration_x[0]   = 0;
    acceleration_y[0]   = 0;
    velocity_x[0]       = 0;
    velocity_y[0]       = 0;
    position_x[0]       = 0;
    position_y[0]       = 0;
    acc_zero_x_count    = 0;
    acc_zero_y_count    = 0;
}

static void acc_calibrate()
{
    struct mpu6xxx_3axes accel;
    int32_t ax = 0, ay = 0;
    for (int32_t i = 0; i < CALIBRATE_SAMPLE_COUNT; i++)
    {
        mpu6xxx_get_accel(mpu_dev, &accel);
        ax += accel.x;
        ay += accel.y;
    }
    calibration_x = (ax / CALIBRATE_SAMPLE_COUNT) + 0.5f;
    calibration_y = (ay / CALIBRATE_SAMPLE_COUNT) + 0.5f;

    rt_kprintf("\ncalibration-x-y:%d %d\n\n", calibration_x, calibration_y);
    calibration_x = -calibration_x;
    calibration_y = -calibration_y;
}

static void acc_integration()
{
    struct mpu6xxx_3axes accel;
    // [filter] get average data
    for (int i = 0; i < AVERAGE_COUNT; i++)
    {
        mpu6xxx_get_accel(mpu_dev, &accel);
        acceleration_x[1] = accel.x + calibration_x; //filtering routine for noise attenuation
        acceleration_y[1] = accel.y + calibration_y;
    }
    acceleration_x[1] = acceleration_x[1] / AVERAGE_COUNT;
    acceleration_y[1] = acceleration_y[1] / AVERAGE_COUNT;

    rt_kprintf("[filter] acc-x-y: %d %d\n", acceleration_x[1], acceleration_y[1]);

    // window
    if (acceleration_x[1] < WINDOW_SIZE && acceleration_x[1] > -WINDOW_SIZE)
    {
        acceleration_x[1] = 0;
    }
    if (acceleration_y[1] < WINDOW_SIZE && acceleration_y[1] > -WINDOW_SIZE)
    {
        acceleration_y[1] = 0;
    }

    // movement-end check
    if (acceleration_x[1] == 0)
    {
        acc_zero_x_count++;
        if (acc_zero_x_count >= MOVEMENT_END_COUNT)
        {
            velocity_x[0] = 0;
            velocity_x[1] = 0;
            acc_zero_x_count = 0;
        }
    }
    else
    {
        acc_zero_x_count = 0;
    }
    if (acceleration_y[1] == 0)
    {
        acc_zero_y_count++;
        if (acc_zero_y_count >= MOVEMENT_END_COUNT)
        {
            velocity_y[0] = 0;
            velocity_y[1] = 0;
            acc_zero_y_count = 0;
        }
    }
    else
    {
        acc_zero_y_count = 0;
    }

    //first integration
    velocity_x[1] = velocity_x[0] + acceleration_x[0] + ((acceleration_x[1] - acceleration_x[0]) >> 1);
    velocity_y[1] = velocity_y[0] + acceleration_y[0] + ((acceleration_y[1] - acceleration_y[0]) >> 1);
    //second integration
    position_x[1] = position_x[0] + velocity_x[0] + ((velocity_x[1] - velocity_x[0]) >> 1);
    position_y[1] = position_y[0] + velocity_y[0] + ((velocity_y[1] - velocity_y[0]) >> 1);

    acceleration_x[0] = acceleration_x[1];
    acceleration_y[0] = acceleration_y[1];
    velocity_x[0] = velocity_x[1];
    velocity_y[0] = velocity_y[1];
    position_x[0] = position_x[1];
    position_y[0] = position_y[1];

    rt_kprintf("velocity-x-y:%d %d\n", velocity_x[1], velocity_y[1]);
    rt_kprintf("position-x-y:%d %d\n", position_x[1], position_y[1]);
}
static void sensor_acc(int argc, char *argv[])
{
    static rt_int32_t acc_delay_time = 10;
    if (argc < 2)
    {
        return;
    }
    if (!rt_strcmp("init", argv[1]))
    {
        mpu6xxx_test();
    }

    if (argc < 3)
    {
        return;
    }
    if (!rt_strcmp("read_average", argv[1]))
    {
        struct mpu6xxx_3axes accel;
        int32_t total = atoi(argv[2]);
        int32_t ax = 0, ay = 0, az = 0, g;
        for (int32_t i = 0; i < total; i++)
        {
            mpu6xxx_get_accel(mpu_dev, &accel);
            ax += accel.x;
            ay += accel.y;
            az += accel.z;
            // accel.x = accel.x * ACC_A_PARAM + ACC_D_PARAM;
            // accel.y = accel.y * ACC_B_PARAM + ACC_E_PARAM;
            // accel.z = accel.z * ACC_C_PARAM + ACC_F_PARAM;
            rt_kprintf("process:%d/%d data:%d %d %d\n", i, total, accel.x, accel.y, accel.z);
            rt_thread_mdelay(acc_delay_time);
        }
        rt_kprintf("acc-x-y-z: %d %d %d\n", accel.x, accel.y, accel.z);
        rt_kprintf("raw-x-y-z: %d %d %d\n", ax, ay, az);
        ax /= total;
        ay /= total;
        az /= total;
       
        rt_kprintf("total:%d average-x-y-z: %d %d %d G:%d.%d\n", total, ax, ay, az, g / 1000, g % 1000);
    }
    if (!rt_strcmp("settime", argv[1]))
    {
        acc_delay_time = atoi(argv[2]);
        rt_kprintf("now time is:%d\n", acc_delay_time);
    }
    if (!rt_strcmp("integration", argv[1]))
    {
        uint32_t count = atoi(argv[2]);
        acc_param_clear();
        acc_calibrate();
        for (uint32_t i=0; i<count; i++)
        {
            acc_integration();
        }
    }
}
MSH_CMD_EXPORT(sensor_acc, sensor_acc test);
