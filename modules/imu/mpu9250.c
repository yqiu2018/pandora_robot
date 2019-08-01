#include "mpu9250.h"
#include <rtthread.h>

#define _delay_ms(__ms)    rt_thread_mdelay(__ms)


int mpu92_init(mpu_config_t *mpux)
{
    uint8_t status;

    mpu9250_port_init();
    _delay_ms(20);
    
    status = mpu92_device_check();
    if (status != RT_EOK)
        return RT_ERROR;

    mpu92_write_reg(MPU6500_PWR_MGMT_1, 0x80);         /* [0]  Reset Device                  */
    mpu92_write_reg(MPU6500_PWR_MGMT_1, 0x04);         /* [1]  Clock Source                  */
    mpu92_write_reg(MPU6500_INT_PIN_CFG, 0x10);        /* [2]  Set INT_ANYRD_2CLEAR          */
    mpu92_write_reg(MPU6500_INT_ENABLE, 0x01);         /* [3]  Set RAW_RDY_EN                */
    mpu92_write_reg(MPU6500_PWR_MGMT_2, 0x00);         /* [4]  Enable Acc & Gyro             */
    mpu92_write_reg(MPU6500_SMPLRT_DIV, 0x00);         /* [5]  Sample Rate Divider           */
    mpu92_write_reg(MPU6500_GYRO_CONFIG, mpux->MPU_Gyr_FullScale    );        /* [6]  default : +-250dps            */
    mpu92_write_reg(MPU6500_ACCEL_CONFIG, mpux->MPU_Acc_FullScale    );       /* [7]  default : +-2G                */
    mpu92_write_reg(MPU6500_CONFIG, mpux->MPU_Gyr_LowPassFilter);             /* [8]  default : GyrLPS_250Hz        */
    mpu92_write_reg(MPU6500_ACCEL_CONFIG_2, mpux->MPU_Acc_LowPassFilter);     /* [9]  default : AccLPS_460Hz        */
//  mpu92_write_reg(MPU6500_USER_CTRL     , 0x30);     /* [10] Set I2C_MST_EN, I2C_IF_DIS    */

#if defined(__USE_MAGNETOMETER)
    mpu92_write_reg(MPU6500_INT_PIN_CFG, 0x10 | 0x02);
    rt_thread_delay(20);
    mpu92_mag_write_reg(AK8963_CNTL2, 0x01);      /* [0]  Reset Device                  */
    mpu92_mag_write_reg(AK8963_CNTL1, 0x00);      /* [1]  Power-down mode               */
    mpu92_mag_write_reg(AK8963_CNTL1, 0x0F);      /* [2]  Fuse ROM access mode          */
    /*      Read sensitivity adjustment   */
    mpu92_mag_write_reg(AK8963_CNTL1, 0x00);      /* [3]  Power-down mode               */
    mpu92_mag_write_reg(AK8963_CNTL1, 0x06);      /* [4]  Continuous measurement mode 2 */

    /* config mpu9250 i2c */
    _delay_ms(2);
    mpu92_write_reg(MPU6500_I2C_MST_CTRL, 0x5D);
    _delay_ms(2);
    mpu92_write_reg(MPU6500_I2C_SLV0_ADDR, AK8963_I2C_ADDR | 0x80);
    _delay_ms(2);
    mpu92_write_reg(MPU6500_I2C_SLV0_REG, AK8963_ST1);
    _delay_ms(2);
    mpu92_write_reg(MPU6500_I2C_SLV0_CTRL, MPU6500_I2C_SLVx_EN | 8);
    _delay_ms(2);
    mpu92_write_reg(MPU6500_I2C_SLV4_CTRL, 0x09);
    _delay_ms(2);
    mpu92_write_reg(MPU6500_I2C_MST_DELAY_CTRL, 0x81);

    mpu92_write_reg(MPU6500_USER_CTRL, 0x20);
#endif

    _delay_ms(100);

    return RT_EOK;
}

int mpu92_device_check(void)
{
    int trycnt=3;

    do {
        uint8_t deviceID = mpu92_read_reg(MPU6500_WHO_AM_I);
        if (deviceID == MPU6500_DeviceID || deviceID == 0x73) 
        {
            break;
        }
        else
        {
            rt_kprintf("error MPU6500ID:0x%x", deviceID);
        }
    }while (--trycnt);
    if (trycnt == 0)
    {
        return RT_ERROR;
    }


#if defined(__USE_MAGNETOMETER)
    // mpu92_write_reg(MPU6500_INT_PIN_CFG, 0x02);    //enable bypass
    // _delay_ms(200);
    // deviceID = mpu92_Mag_read_reg(AK8963_WIA);
    // if (deviceID != AK8963_DeviceID) {
    //     rt_kprintf("AK8963ID:0x%x", deviceID);
    //     return ERROR;
    // }
#endif

    return RT_EOK;
}

// /**
//   * @brief  mpu92_GetSensitivity
//   * @param  sensitivity: point to float32_t
//             sensitivity[0] - gyro
//             sensitivity[1] - accel
//             sensitivity[2] - magnetic
//             sensitivity[3] - temperature scale
//             sensitivity[4] - temperature offset
//   */
void mpu92_get_sensitivity( mpu_config_t *mpux, float32_t *sensitivity )
{
    float64_t scale;

    /* Set gyroscope sensitivity (dps/LSB) */
#if defined(__USE_GYROSCOPE)
    switch (mpux->MPU_Gyr_FullScale) {
    case MPU_GyrFS_250dps:
        scale = 250.0;
        break;
    case MPU_GyrFS_500dps:
        scale = 500.0;
        break;
    case MPU_GyrFS_1000dps:
        scale = 1000.0;
        break;
    case MPU_GyrFS_2000dps:
        scale = 2000.0;
        break;
    default:
        scale = 0.0;
        break;
    }
    sensitivity[0] = scale / 32768;
#else
    sensitivity[0] = 0.0f;
#endif

    /* Set accelerometer sensitivity (g/LSB) */
#if defined(__USE_ACCELEROMETER)
    switch (mpux->MPU_Acc_FullScale) {
    case MPU_AccFS_2g:
        scale = 2.0;
        break;
    case MPU_AccFS_4g:
        scale = 4.0;
        break;
    case MPU_AccFS_8g:
        scale = 8.0;
        break;
    case MPU_AccFS_16g:
        scale = 16.0;
        break;
    default:
        scale = 0.0;
        break;
    }
    sensitivity[1] = scale / 32768;
#else
    sensitivity[1] = 0.0f;
#endif

    /* Set magnetometer sensitivity (uT/LSB) */
#if defined(__USE_MAGNETOMETER)
    sensitivity[2] = 0.6;   /* +-4800uT */
#else
    sensitivity[2] = 0.0f;
#endif

    /* Set ictemperature sensitivity (degC/LSB) */
#if defined(__USE_ICTEMPERATURE)
    sensitivity[3] = 1.0 / 333.87;
    sensitivity[4] = 21.0;
#else
    sensitivity[3] = 0.0f;
    sensitivity[4] = 0.0f;
#endif
}

/**
  * @brief  mpu92_GetRawData
  * @param  data: point to int16_t
  * @retval return 1 : AK8963 data update
            return 0 : AK8963 data not update
  */
int mpu92_get_raw_data(int16_t *data)
{
#if defined(__USE_MAGNETOMETER)
    uint8_t readBuf[22] = {0};
    mpu92_read_regs(MPU6500_ACCEL_XOUT_H, readBuf, 22);    /* Gyr, Acc, Mag, Temp */
#else
    uint8_t readBuf[14] = {0};
    mpu92_read_regs(MPU6500_ACCEL_XOUT_H, readBuf, 14);    /* Gyr, Acc, Temp */
#endif

    data[0] = (int16_t)(readBuf[6]  << 8) | readBuf[7];   /* ICTemp */
    data[1] = (int16_t)(readBuf[8]  << 8) | readBuf[9];   /* Gyr.X */
    data[2] = (int16_t)(readBuf[10] << 8) | readBuf[11];  /* Gyr.Y */
    data[3] = (int16_t)(readBuf[12] << 8) | readBuf[13];  /* Gyr.Z */
    data[4] = (int16_t)(readBuf[0]  << 8) | readBuf[1];   /* Acc.X */
    data[5] = (int16_t)(readBuf[2]  << 8) | readBuf[3];   /* Acc.Y */
    data[6] = (int16_t)(readBuf[4]  << 8) | readBuf[5];   /* Acc.Z */

#if defined(__USE_MAGNETOMETER)
    if (!(!(readBuf[14] & AK8963_STATUS_DRDY) || (readBuf[14] & AK8963_STATUS_DOR) || (readBuf[21] & AK8963_STATUS_HOFL))) {
        data[7] = (int16_t)(readBuf[16] << 8) | readBuf[15];  /* Mag.X */
        data[8] = (int16_t)(readBuf[18] << 8) | readBuf[17];  /* Mag.Y */
        data[9] = (int16_t)(readBuf[20] << 8) | readBuf[19];  /* Mag.Z */
        return 1;
    }
#endif

    return 0;
}
