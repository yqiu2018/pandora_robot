#include "laser_receiver.h"

#define DBG_SECTION_NAME  "laser_receiver"
#define DBG_LEVEL         DBG_LOG
#include <rtdbg.h>

// ADC
#define RAW_DATA_THRESHOLDS     405

#define REFER_VOLTAGE 330       /* 参 考 电 压 3.3V,数 据 精 度 乘 以100保 留2位 小 数*/
#define CONVERT_BITS (1 << 12)  /* 转 换 位 数为12位 */
#define ADC_RAWDATA_TO_VOLTAGE(rawdata) (rawdata * REFER_VOLTAGE / CONVERT_BITS)
#define LASER_RECEIVER_ADC_DEV_NAME             "adc1"
#define LASER_RECEIVER_ADC_CHANNEL     9
rt_adc_device_t laser_adc_dev; 

// THREAD
#define THREAD_DELAY_TIME           50

#define THREAD_PRIORITY             10
#define THREAD_STACK_SIZE          512
#define THREAD_TIMESLICE             5
static rt_thread_t tid_laser_receiver = RT_NULL;

void laser_receiver_thread(void *param)
{
    // TODO
    rt_uint32_t raw_data;

    while(1)
    {
        rt_thread_mdelay(THREAD_DELAY_TIME);
        // Check
        raw_data = rt_adc_read(laser_adc_dev, LASER_RECEIVER_ADC_CHANNEL);
        if (raw_data > RAW_DATA_THRESHOLDS)
        {
            rt_kprintf("Be Shot!\n");
        }
    }
}

void laser_receiver_init()
{
    laser_adc_dev = (rt_adc_device_t)rt_device_find(LASER_RECEIVER_ADC_DEV_NAME);
    if (laser_adc_dev == RT_NULL)
    {
        LOG_E("Can't find device on %s", LASER_RECEIVER_ADC_DEV_NAME);
        return;
    }
    rt_adc_enable(laser_adc_dev, LASER_RECEIVER_ADC_CHANNEL);

    // thread
    tid_laser_receiver = rt_thread_create("laser",
                              laser_receiver_thread, RT_NULL,
                              THREAD_STACK_SIZE,
                              THREAD_PRIORITY, THREAD_TIMESLICE);

    if (tid_laser_receiver != RT_NULL)
    {
        rt_thread_startup(tid_laser_receiver);
    }
}
