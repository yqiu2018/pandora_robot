#include <laser_launcher.h>
#include <laser_receiver.h>
#include <stdlib.h>

static void laser_test(int argc, void *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "init_all")))
    {
        laser_launcher_init();
        laser_receiver_init();
    }
    if (!(rt_strcmp(argv[1], "init_launcher")))
    {
        laser_receiver_init();
    }
    if (!(rt_strcmp(argv[1], "init_receiver")))
    {
        laser_receiver_init();
    }
    if (!(rt_strcmp(argv[1], "open_launcher")))
    {
        laser_launcher_open();
    }
    if (!(rt_strcmp(argv[1], "close_launcher")))
    {
        laser_launcher_close();
    }

    if (argc < 3)
    {
        return;
    }
    if (!(rt_strcmp(argv[1], "launcher_keep")))
    {
        laser_launcher_keep(atoi(argv[2]));
    }

}

MSH_CMD_EXPORT(laser_test, laser_test test);
