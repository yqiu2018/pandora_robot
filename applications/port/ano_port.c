#include <rtthread.h>
#include <ano.h>

static rt_device_t com1;

static rt_err_t _rx_ind(rt_device_t dev, rt_size_t size)
{
    ano_byte_ready_indicate();

    return RT_EOK;
}

static int _serial_init(void)
{
    com1 = rt_device_find("uart1");
    if (com1 == RT_NULL)
    {
        rt_kprintf("Can't find device on %s for ano\n", "uart1");
        return RT_ERROR;
    }
    rt_device_close(com1);
    rt_device_open(com1, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    rt_device_set_rx_indicate(com1, _rx_ind);

    rt_kprintf("control can use\n");

    return RT_EOK;
}

void ano_init_all(void)
{
    ano_init(0);
    _serial_init();
}

uint8_t ano_read_byte_port(void)
{
    uint8_t temp;
    rt_device_read(com1, 0, &temp, 1);
    return temp;
}

void ano_send_data_port(uint8_t *buffer, uint8_t length)
{
    if (com1 != RT_NULL)
    {
        rt_device_write(com1, 0, buffer, length);
    }
    else
    {
        rt_kprintf(">ano-send: len:%d\n", length);
    }
}

void ano_test(void)
{
    uint32_t cnt = 0;
    while (cnt++ < 60 * 1000)
    {
        rt_thread_mdelay(10);
        ano_send_status(cnt,1,1,1,1,1);
    }  
}

static void ano(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }

    if (!rt_strcmp(argv[1], "init"))
    {
        ano_init(0);
    }
    if (!rt_strcmp(argv[1], "initcom"))
    {
        _serial_init();
    }
    if (!rt_strcmp(argv[1], "test"))
    {
        ano_test();
    }
    if (!rt_strcmp(argv[1], "version"))
    {
        ano_send_version(1,1,1,1,1);        
    }
    if (!rt_strcmp(argv[1], "status"))
    {
        ano_send_status(1,1,1,1,1,1);
    }
    if (!rt_strcmp(argv[1], "send"))
    {
        ano_test();
    }
}
MSH_CMD_EXPORT(ano, test ano);


