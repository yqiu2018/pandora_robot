#include "imu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rtthread.h>

static void imu_update_sensitivity(imu_t *imux)
{
    float32_t scale[5];

    mpu92_get_sensitivity(&imux->mpu, scale);

    /* Set gyroscope sensitivity (dps/LSB) */
    imux->data.gyrFactor[0] = scale[0];
    imux->data.gyrFactor[1] = scale[0];
    imux->data.gyrFactor[2] = scale[0];

    /* Set accelerometer sensitivity (g/LSB) */
    imux->data.accFactor[0] = scale[1];
    imux->data.accFactor[1] = scale[1];
    imux->data.accFactor[2] = scale[1];

    /* Set magnetometer sensitivity (uT/LSB) */
    imux->data.magFactor[0] = scale[2];
    imux->data.magFactor[1] = scale[2];
    imux->data.magFactor[2] = scale[2];

    /* Set ictemperature sensitivity (degC/LSB) */
    imux->data.ictempScale = scale[3];
    imux->data.ictempOffset = scale[4];
}

int imu_init(imu_t *imux)
{
    int status;

    status = mpu92_init(&imux->mpu);
    if (status != RT_EOK) {
        rt_kprintf("\nfailed to init MPU92\n");
        return status;
    }
    rt_kprintf("\nsuccessfuly init MPU92\n");

    imu_update_sensitivity(imux);

    return RT_EOK;
}

static int imu_get_raw_data(imu_t *imux)
{
    int status;
    int16_t data16[10];

    status = mpu92_get_raw_data(data16);
    imux->data.ictempRaw = data16[0]; /* ICTemp */
    imux->data.gyrRaw[0] = data16[1]; /* Gyr.X */
    imux->data.gyrRaw[1] = data16[2]; /* Gyr.Y */
    imux->data.gyrRaw[2] = data16[3]; /* Gyr.Z */
    imux->data.accRaw[0] = data16[4]; /* Acc.X */
    imux->data.accRaw[1] = data16[5]; /* Acc.Y */
    imux->data.accRaw[2] = data16[6]; /* Acc.Z */

#if defined(__USE_MAGNETOMETER)
    if (status == 1)
    {
        imux->data.magRaw[0] = data16[7]; /* Mag.X */
        imux->data.magRaw[1] = data16[8]; /* Mag.Y */
        imux->data.magRaw[2] = data16[9]; /* Mag.Z */
    }
    else{
        imux->data.magRaw[0] = 0; /* Mag.X */
        imux->data.magRaw[1] = 0; /* Mag.Y */
        imux->data.magRaw[2] = 0; /* Mag.Z */
    }
#endif

    return status;
}

void imu_update(imu_t *imux)
{
    imu_get_raw_data(imux);

#if defined(__USE_GYROSCOPE)
    imux->data.gyrData[0] = imux->data.gyrRaw[0] * imux->data.gyrFactor[0]; /* Gyr.X */
    imux->data.gyrData[1] = imux->data.gyrRaw[1] * imux->data.gyrFactor[1]; /* Gyr.Y */
    imux->data.gyrData[2] = imux->data.gyrRaw[2] * imux->data.gyrFactor[2]; /* Gyr.Z */
#endif

#if defined(__USE_ACCELEROMETER)
    imux->data.accData[0] = imux->data.accRaw[0] * imux->data.accFactor[0]; /* Acc.X */
    imux->data.accData[1] = imux->data.accRaw[1] * imux->data.accFactor[1]; /* Acc.Y */
    imux->data.accData[2] = imux->data.accRaw[2] * imux->data.accFactor[2]; /* Acc.Z */
#endif

#if defined(__USE_MAGNETOMETER)
    imux->data.magData[0] = imux->data.magRaw[0] * imux->data.magFactor[0]; /* Mag.X */
    imux->data.magData[1] = imux->data.magRaw[1] * imux->data.magFactor[1]; /* Mag.Y */
    imux->data.magData[2] = imux->data.magRaw[2] * imux->data.magFactor[2]; /* Mag.Z */
#endif
}


