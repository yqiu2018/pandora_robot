#include <rtthread.h>
#include <rtdevice.h>

#define ADC_DEVICE_NAME   "adc1"
#define ADC_CHANNEL   9
#define REFER_VOLTAGE 330 /* 参 考 电 压 3.3V,数 据 精 度 乘 以100保 留2位 小 数*/
#define CONVERT_BITS (1 << 12) /* 转 换 位 数为12位 */
rt_adc_device_t adc_dev; /* ADC 设 备 句 柄 */

static rt_uint32_t get_voltage(rt_adc_device_t dev, rt_uint32_t channel)
{
    rt_uint32_t val;
    int vol;
    /* 读 取 采 样 值 */
    val = rt_adc_read(dev, channel);
    /* 转 换 为 对 应 电 压 值 */
    vol = val * REFER_VOLTAGE / CONVERT_BITS;
    rt_kprintf("voltage:%d.%02d source:%d\n", vol / 100, vol % 100, val);

    return vol;
}

static void adc_test(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "init")))
    {
        adc_dev = (rt_adc_device_t)rt_device_find(ADC_DEVICE_NAME);
    }
    if (!(rt_strcmp(argv[1], "enable")))
    {
        rt_adc_enable(adc_dev, ADC_CHANNEL);
    }
    if (!(rt_strcmp(argv[1], "read")))
    {
        get_voltage(adc_dev, ADC_CHANNEL);
    }
    if (argc < 3)
    {
        return;
    }
    // if (!(rt_strcmp(argv[1], "enable")))
    // {
    //     rt_pwm_enable(adc_dev, atoi(argv[2]));
    // }
    // if (!(rt_strcmp(argv[1], "probe")))
    // {
    //     adc_dev = (rt_adc_device_t)rt_device_find(argv[2]);
    // }
   
}
MSH_CMD_EXPORT(adc_test, adc_test test);
