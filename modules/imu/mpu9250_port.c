#include <rtdevice.h>
#include "mpu9250_port.h"

#define mpu9250_I2C_BUS_NAME "i2c2"   /* 传 感器 连 接 的I2C总 线 设 备 名 称 */
#define mpu9250_ADDR MPU6500_I2C_ADDR /* 从 机地 址 */

static struct rt_i2c_bus_device *i2c_bus = RT_NULL; /* I2C总 线 设 备 句 柄 */

static void _i2c_init(void)
{
    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(mpu9250_I2C_BUS_NAME);
    RT_ASSERT(i2c_bus != RT_NULL);
}

int mpu9250_port_init(void)
{
    _i2c_init();

    return 0;
}

static int _i2c_write(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t buf[len+1];
    struct rt_i2c_msg msgs;

    buf[0] = reg;
    if (len == 1)
        buf[1] = data[0];
    else
        rt_memcpy(&buf[1], data, len);

    msgs.addr = addr;
    msgs.flags = RT_I2C_WR;
    msgs.len = len+1;
    msgs.buf = buf;

    if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
        return 0;
    else
        return -1;  
}
static int _i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t buf;
    struct rt_i2c_msg msgs[2];

    buf = reg;
    msgs[0].addr = addr;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].len = 1;
    msgs[0].buf = &buf;

    msgs[1].addr = addr;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].len = len;
    msgs[1].buf = data;

    if (rt_i2c_transfer(i2c_bus, msgs, 2) == 2)
        return 0;
    else
        return 1;
}

void mpu92_write_reg( uint8_t writeAddr, uint8_t writeData )
{
    _i2c_write(MPU6500_I2C_ADDR, writeAddr, &writeData, 1);
}
void mpu92_write_regs( uint8_t writeAddr, uint8_t *writeData, uint8_t lens )
{
    _i2c_write(MPU6500_I2C_ADDR, writeAddr, writeData, lens);
}
uint8_t mpu92_read_reg( uint8_t readAddr )
{
    uint8_t readData;
    _i2c_read(MPU6500_I2C_ADDR, readAddr, &readData, 1);
    return readData;
}
void mpu92_read_regs( uint8_t readAddr, uint8_t *readData, uint8_t lens )
{
    _i2c_read(MPU6500_I2C_ADDR, readAddr, readData, lens);
}

void mpu92_mag_write_reg( uint8_t writeAddr, uint8_t writeData )
{
    _i2c_write(AK8963_I2C_ADDR, writeAddr, &writeData, 1);
}
void mpu92_mag_write_regs( uint8_t writeAddr, uint8_t *writeData, uint8_t lens )
{
    _i2c_write(AK8963_I2C_ADDR, writeAddr, writeData, lens);
}
uint8_t mpu92_mag_read_reg( uint8_t readAddr )
{
    uint8_t readData;
    _i2c_read(AK8963_I2C_ADDR, readAddr, &readData, 1);
    return readData;
}
void mpu92_mag_read_regs( uint8_t readAddr, uint8_t *readData, uint8_t lens )
{
    _i2c_read(AK8963_I2C_ADDR, readAddr, readData, lens);
}
