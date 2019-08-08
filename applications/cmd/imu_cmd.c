#include <rtthread.h>
#include <imu.h>
#include <stdlib.h>
#include <ahrs.h>
#include <ano.h>

static imu_t imu;

extern void ano_init_all(void);

static void imu_test(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "init")))
    {
        imu.mpu.MPU_Acc_FullScale = MPU_AccFS_2g;
        imu.mpu.MPU_Acc_LowPassFilter = MPU_AccLPS_460Hz;
        imu.mpu.MPU_Gyr_FullScale = MPU_GyrFS_250dps;
        imu.mpu.MPU_Gyr_LowPassFilter = MPU_GyrLPS_250Hz;
        // imu.mpu.MPU_Mag_FullScale = MPU_MagFS_14b;
        imu_init(&imu);
    }
    if (argc < 3)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "read")))
    {
        int cnt = atoi(argv[2]);
        float state[3];

        ano_init_all();

        for (int i=0; i<cnt; i++)
        {
            imu_update(&imu);
            rt_thread_mdelay(1);
            // rt_kprintf("a-x-y-z:%d %d %d g-x-y-z:%d %d %d z-x-y-z:%d %d %d\n",
            //     (int)(imu.data.accData[0] * 1000), (int)(1000 * imu.data.accData[1]), (int)(1000 * imu.data.accData[2]),
            //     (int)(imu.data.gyrData[0] * 1000), (int)(1000 * imu.data.gyrData[1]), (int)(1000 * imu.data.gyrData[2]),
            //     (int)(imu.data.magData[0]), (int)(imu.data.magData[1]), (int)(imu.data.magData[2]));
            MadgwickAHRSupdate(imu.data.gyrData[0], imu.data.gyrData[1], imu.data.gyrData[2],
                            imu.data.accData[0], imu.data.accData[1], imu.data.accData[2],
                            imu.data.magData[0], imu.data.magData[1], imu.data.magData[2], 0.01, state);
            ano_send_status(state[0], state[1], state[2], 0,0,0);
            //rt_kprintf("ahrs-r-p-y: %d.%d %d.%d %d.%d\n", (int)(state[0]), (int)state[0]*100%100, (int)(state[1]), (int)state[1]*100%100,(int)(state[2]), (int)state[2]*100%100);
        }
    }
    if (!(rt_strcmp(argv[1], "read2")))
    {
        int cnt = atoi(argv[2]);
        float state[3];

        ano_init_all();

        for (int i=0; i<cnt; i++)
        {
            imu_update(&imu);
            rt_thread_mdelay(1);
            // rt_kprintf("a-x-y-z:%d %d %d g-x-y-z:%d %d %d z-x-y-z:%d %d %d\n",
            //     (int)(imu.data.accData[0] * 1000), (int)(1000 * imu.data.accData[1]), (int)(1000 * imu.data.accData[2]),
            //     (int)(imu.data.gyrData[0] * 1000), (int)(1000 * imu.data.gyrData[1]), (int)(1000 * imu.data.gyrData[2]),
            //     (int)(imu.data.magData[0]), (int)(imu.data.magData[1]), (int)(imu.data.magData[2]));
            ahrs_9dof_update(imu.data.gyrData[0], imu.data.gyrData[1], imu.data.gyrData[2],
                            imu.data.accData[0], imu.data.accData[1], imu.data.accData[2],
                            imu.data.magData[0], imu.data.magData[1], imu.data.magData[2], 0.01, state);
            ano_send_status(state[0], state[1], state[2], 0,0,0);
            //rt_kprintf("ahrs-r-p-y: %d.%d %d.%d %d.%d\n", (int)(state[0]), (int)state[0]*100%100, (int)(state[1]), (int)state[1]*100%100,(int)(state[2]), (int)state[2]*100%100);
        }
    }
}
MSH_CMD_EXPORT(imu_test, imu_test test);


