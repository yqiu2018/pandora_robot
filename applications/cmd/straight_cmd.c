#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <chassis.h>
#include <stdio.h>
#include <stdlib.h>
#include <pos_pid_controller.h>
#include <ano.h>

#define SAMPLE_TIME          10

#define LEFT_MOTOR_OBJ       chas->c_wheels[0]->w_motor
#define RIGHT_MOTOR_OBJ      chas->c_wheels[1]->w_motor

// Thread
#define THREAD_DELAY_TIME               10
#define THREAD_PRIORITY                 12
#define THREAD_STACK_SIZE               1024
#define THREAD_TIMESLICE                5

static rt_thread_t tid_straight = RT_NULL;
static pos_pid_controller_t pos_controller;
static float target_yaw;
static int active_control = RT_FALSE;
static int run_speed = 0;

extern float stof(const char *s);

extern float inv_yaw_state;
extern chassis_t chas;

extern void ano_init_all(void);

static void print_help()
{
    rt_kprintf("Usage: mobile_robot [x] [y] [w] [duration]\n");
}

static void reset_controller(void)
{
    pos_controller->integral = 0.0f;
}

void straight_thread(void *param)
{
    while(1)
    {
        rt_thread_mdelay(THREAD_DELAY_TIME);
        if (active_control)
        {
            pos_pid_controller_update(pos_controller, inv_yaw_state);

            motor_run(LEFT_MOTOR_OBJ, run_speed - pos_controller->last_out);
            motor_run(RIGHT_MOTOR_OBJ, run_speed + pos_controller->last_out);
        }
        else
        {
            motor_run(LEFT_MOTOR_OBJ, 0);
            motor_run(RIGHT_MOTOR_OBJ, 0);
            reset_controller();
        }
        ano_send_user_data(1, inv_yaw_state, target_yaw, pos_controller->p_error, pos_controller->i_error, pos_controller->d_error, (int16_t)pos_controller->last_out, run_speed, 0, pos_controller->integral);
    }
}

void straight_init(void)
{
    // thread
    tid_straight = rt_thread_create("tStraight",
                              straight_thread, RT_NULL,
                              THREAD_STACK_SIZE,
                              THREAD_PRIORITY, THREAD_TIMESLICE);

    if (tid_straight != RT_NULL)
    {
        rt_thread_startup(tid_straight);
    }
}

static void st(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }
    if (!rt_strcmp("init", argv[1]))
    {
        pos_controller = pos_pid_controller_create(60.0f,1.0f,70.0f);
        target_yaw = inv_yaw_state;
        // set controller target
        pos_controller->controller.target = target_yaw;

        // set controller sample time
        pos_controller->controller.sample_time = SAMPLE_TIME;

        // set i output limit
        pos_controller->anti_windup_value = pos_controller->maximum * 0.618;

        // set output limit
        pos_controller->maximum = 800;
        pos_controller->minimum = -800;

        // start thread
        straight_init();
    }

    if (!rt_strcmp("init-all", argv[1]))
    {
        pos_controller = pos_pid_controller_create(60.0f,1.0f,70.0f);
        target_yaw = inv_yaw_state;
        // set controller target
        pos_controller->controller.target = target_yaw;

        // set controller sample time
        pos_controller->controller.sample_time = SAMPLE_TIME;

        // start thread
        straight_init();

        ano_init_all();
    }
    if (!rt_strcmp("reset-target", argv[1]))
    {
        target_yaw = inv_yaw_state;
        // set controller target
        pos_controller->controller.target = target_yaw;
        reset_controller();
        rt_kprintf("target:%d current:%d\n", (int)target_yaw, (int)inv_yaw_state);
    }
    if (!rt_strcmp("read", argv[1]))
    {
        rt_kprintf("target:%d current:%d\n", (int)target_yaw, (int)inv_yaw_state);
        rt_kprintf("kpid: %d %d %d\n", (int)(pos_controller->kp), (int)(pos_controller->ki), (int)(pos_controller->kd));
    }

    if (!rt_strcmp("start", argv[1]))
    {
        active_control = RT_TRUE;
    }
    if (!rt_strcmp("stop", argv[1]))
    {
        active_control = RT_FALSE;
    }

    if (argc < 3)
    {
        return;
    }
    if (!rt_strcmp("set-target", argv[1]))
    {
        target_yaw = stof(argv[2]);
        // set controller target
        pos_controller->controller.target = target_yaw;
        reset_controller();
        rt_kprintf("target:%d current:%d\n", (int)target_yaw, (int)inv_yaw_state);
    }

    if (!rt_strcmp("set-speed", argv[1]))
    {
        run_speed = atoi(argv[2]);
    }

    if (argc < 4)
    {
        return;
    }

    if (!rt_strcmp("run", argv[1]))
    {
        long duration = atoi(argv[2]);
        run_speed = atoi(argv[3]);

        rt_kprintf("start\n");
        active_control = RT_TRUE;
        rt_thread_mdelay(duration);
        active_control = RT_FALSE;
        rt_kprintf("end\n");
        rt_kprintf("target:%d current:%d\n", (int)target_yaw, (int)inv_yaw_state);
        run_speed = 0;
    }

    if (!rt_strcmp("keep-speed", argv[1]))
    {
        long duration = atoi(argv[2]);
        
        rt_kprintf("start\n");
        run_speed = atoi(argv[3]);
        rt_thread_mdelay(duration);
        run_speed = 0;
        rt_kprintf("end\n");
    }

    if (argc < 5)
    {
        return;
    }
    if (!rt_strcmp("set-kpid", argv[1]))
    {
        pos_controller->kp = stof(argv[2]);
        pos_controller->ki = stof(argv[3]);
        pos_controller->kd = stof(argv[4]);
    }
}
MSH_CMD_EXPORT(st, straight_test);


