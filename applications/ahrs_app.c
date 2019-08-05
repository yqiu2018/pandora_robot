#include <rtthread.h>
#include <imu.h>
#include <ahrs.h>

// Ahrs Thread
#define THREAD_DELAY_TIME           5
#define THREAD_PRIORITY             11
#define THREAD_STACK_SIZE          1024
#define THREAD_TIMESLICE             5

static rt_thread_t tid_ahrs = RT_NULL;
float rpy_state[3];
float yaw_average;

static void ahrs_app_thread(void *param)
{
    static imu_t imu;
    float sum = 0.0f;
    int count = 0;

    imu.mpu.MPU_Acc_FullScale = MPU_AccFS_2g;
    imu.mpu.MPU_Acc_LowPassFilter = MPU_AccLPS_460Hz;
    imu.mpu.MPU_Gyr_FullScale = MPU_GyrFS_250dps;
    imu.mpu.MPU_Gyr_LowPassFilter = MPU_GyrLPS_250Hz;
    // imu.mpu.MPU_Mag_FullScale = MPU_MagFS_14b;
    imu_init(&imu);

    while(1)
    {
        rt_thread_mdelay(THREAD_DELAY_TIME);
        imu_update(&imu);
        ahrs_9dof_update(imu.data.gyrData[0], imu.data.gyrData[1], imu.data.gyrData[2],
                        imu.data.accData[0], imu.data.accData[1], imu.data.accData[2],
                        imu.data.magData[0], imu.data.magData[1], imu.data.magData[2], (float)THREAD_DELAY_TIME/1000, rpy_state);

        sum += rpy_state[2];

        if (count++ > 40)
        {
            count = 0;
            yaw_average = sum / 40;
            sum = 0.0f;
        }

        // rt_kprintf("rpy:%d %d %d\n", (int)rpy_state[0], (int)rpy_state[1], (int)rpy_state[2]);
        // ano_send_status(state[0], state[1], state[2], 0,0,0);
        // ano_send_senser(imu.data.accData[0], imu.data.accData[1], imu.data.accData[2],
        //     imu.data.gyrData[0], imu.data.gyrData[1], imu.data.gyrData[2],
        //     imu.data.magData[0], imu.data.magData[1], imu.data.magData[2], 0);
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


