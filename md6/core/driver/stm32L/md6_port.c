#include "md6_port.h"
#include <rtthread.h>
#include <rtdevice.h>

static struct rt_i2c_bus_device *i2c_bus = RT_NULL;

static void _i2c_init(void *bus_name)
{
    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(bus_name);
    RT_ASSERT(i2c_bus != RT_NULL);
    rt_kprintf("md6 port find device\n");
}

static int _i2c_write(uint8_t addr, uint8_t reg, uint8_t const *data, uint8_t len)
{
    uint8_t buf[len + 1];
    struct rt_i2c_msg msgs;

    buf[0] = reg;
    if (len == 1)
        buf[1] = data[0];
    else
        rt_memcpy(&buf[1], data, len);

    msgs.addr = addr;
    msgs.flags = RT_I2C_WR;
    msgs.len = len + 1;
    msgs.buf = buf;

    if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
        return RT_EOK;
    else
        return RT_ERROR;
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
        return RT_EOK;
    else
        return RT_ERROR;
}

int Sensors_I2C_WriteRegister_swap(unsigned char slave_addr, unsigned char reg_addr, unsigned char length, unsigned char const *data)
{
    if (_i2c_write(slave_addr, reg_addr, data, length) != RT_EOK)
    {
        rt_kprintf("error:md6 i2c port write regs\n");
        return 1;
    }

    return 0;
}
int Sensors_I2C_ReadRegister_swap(unsigned char slave_addr, unsigned char reg_addr, unsigned char length, unsigned char *data)
{
    if (_i2c_read(slave_addr, reg_addr, data, length) != RT_EOK)
    {
        rt_kprintf("error:md6 i2c port read regs\n");
        return 1;
    }
    
    return 0;
}
void Delay(unsigned long ms)
{
    rt_thread_mdelay(ms);
}
void stm32l_get_clock_ms(unsigned long *count)
{
    *count = rt_tick_get();
}

void md6_port_init(void *param)
{
    _i2c_init(param);
}
