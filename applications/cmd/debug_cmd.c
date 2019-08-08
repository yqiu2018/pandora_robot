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
}
MSH_CMD_EXPORT(debug, debug command list);
