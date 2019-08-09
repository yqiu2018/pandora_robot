#include <rtthread.h>
#include <chassis.h>
extern chassis_t chas;

static void debug(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!rt_strcmp("get_encoder_cnt", argv[1]))
    {
        rt_kprintf("the count is: %d %d\n", chas->c_wheels[0]->w_encoder->pulse_count, chas->c_wheels[1]->w_encoder->pulse_count);
    }
    if (argc < 3)
    {
        return;
    }
    if (!rt_strcmp("mem_alloc", argv[1]))
    {
        rt_size_t size = atoi(argv[2]);
        if (rt_malloc(size) != RT_NULL)
        {
            rt_kprintf("size:%d ok\n", size);
        }
    }
}
MSH_CMD_EXPORT(debug, debug command list);
