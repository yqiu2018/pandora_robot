#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <chassis.h>
#include <stdio.h>
#include <stdlib.h>

#define _ABS(val)  (val > 0 ? val : -val)
#define PULSE_PER_REVOL 2496

#define TURN_SPEED      11

struct velocity target_velocity;

extern chassis_t chas;
extern float rpy_state[3];
extern float yaw_average;

extern float stof(const char *s);

static void turn(float z)
{
    target_velocity.linear_x = 0;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = z;
    chassis_set_velocity(chas, target_velocity);
}

static void forward(float x)
{
    target_velocity.linear_x = x;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = 0;
    chassis_set_velocity(chas, target_velocity);
}

static void stop(void)
{
    target_velocity.linear_x = 0;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = 0;
    chassis_set_velocity(chas, target_velocity);
}

static void pos_test(int argc, char *argv[])
{
    if (argc < 2)
    {
        return;
    }

    if (argc < 4)
    {
        return;
    }
    if (!rt_strcmp("set", argv[1]))
    {
        double pos = stof(argv[3]);
        double wheel_pos = PI * chas->c_kinematics->wheel_radius;
        double left_pos;
        double right_pos;
        float init_yaw = yaw_average;
        float diff_yaw;
        float speed = stof(argv[2]);

        encoder_reset(chas->c_wheels[0]->w_encoder);
        encoder_reset(chas->c_wheels[1]->w_encoder);
        if (encoder_read(chas->c_wheels[0]->w_encoder) != 0 || encoder_read(chas->c_wheels[1]->w_encoder) != 0)
        {
            rt_kprintf("reset error: %d %d\n", chas->c_wheels[0]->w_encoder->pulse_count, chas->c_wheels[1]->w_encoder->pulse_count);
        }

        if (pos < 0)
        {
            if (speed > 0)
                speed = -speed;
        }
        forward(speed);

        // target_velocity.linear_y = 0;
        // target_velocity.angular_z = 0;
        // chassis_set_velocity(chas, target_velocity);

        while (1)
        {
            rt_thread_mdelay(20);
            diff_yaw = yaw_average - init_yaw;
            if ((diff_yaw > 1.6) || (diff_yaw < -1.6))
            {
                rt_kprintf("diff_yaw:%d\n", (int)diff_yaw);
                if (diff_yaw > 0)
                {
                    // if (chas->c_velocity.angular_z != TURN_SPEED)
                    //     turn(TURN_SPEED);
                }
                else
                {
                    // if (chas->c_velocity.angular_z != -TURN_SPEED)
                    //     turn(-TURN_SPEED);
                }   
            }
            else
            {
                if (chas->c_velocity.linear_x != speed)
                {
                    // forward(speed);
                }
            }
            
            left_pos = wheel_pos * encoder_read(chas->c_wheels[0]->w_encoder) / PULSE_PER_REVOL;
            right_pos = wheel_pos * encoder_read(chas->c_wheels[1]->w_encoder) / PULSE_PER_REVOL;
            if (pos > 0)
            {
                if (left_pos >= pos || right_pos >= pos)
                {
                    stop();
                    rt_kprintf("pos-l-r:%d.%d %d.%d\n", (int)left_pos, (int)(left_pos*100)%100, (int)right_pos, (int)(right_pos*100)%100);
                    break;
                }
            }
            else
            {
                if (left_pos <= pos || right_pos <= pos)
                {
                    stop();
                    rt_kprintf("pos-l-r:%d.%d %d.%d\n", (int)left_pos, (int)(left_pos*100)%100, (int)right_pos, (int)(right_pos*100)%100);
                    break;
                }
            }
        }
    }
}
MSH_CMD_EXPORT(pos_test, pos_test test);
