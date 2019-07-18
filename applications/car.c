#include <rtthread.h>
#include <chassis.h>

#define DBG_SECTION_NAME  "car"
#define DBG_LEVEL         DBG_LOG
#include <rtdbg.h>

// Motor
extern int left_motor_init(void);
extern int left_motor_enable(void);
extern int left_motor_disable(void);
extern int left_motor_set_speed(rt_int8_t percentage);
extern int right_motor_init(void);
extern int right_motor_enable(void);
extern int right_motor_disable(void);
extern int right_motor_set_speed(rt_int8_t percentage);

// ENCODER
#define LEFT_ENCODER_PIN            62     // GET_PIN(D, 14)
#define RIGHT_ENCODER_PIN           61     // GET_PIN(D, 13)
#define PULSE_PER_REVOL             20     // Real value 20
#define SAMPLE_TIME                1000

// PID
#define PID_SAMPLE_TIME             50

// WHEEL
#define WHEEL_RADIUS             0.066
#define GEAR_RATIO                   1

// CAR
chassis_t chas;

#define WHEEL_DIST_X                 0
#define WHEEL_DIST_Y              0.13

// Car Thread
#define THREAD_PRIORITY             10
#define THREAD_STACK_SIZE          512
#define THREAD_TIMESLICE             5

// static rt_thread_t tid_car = RT_NULL;

// void car_thread(void *param)
// {
//     // TODO

//     while (1)
//     {
//         rt_thread_mdelay(50);
//     }

// //    chassis_destroy(chas);
// }

void car_init(void *parameter)
{
    // 1. Initialize two wheels - left and right
    wheel_t* c_wheels = (wheel_t*) rt_malloc(sizeof(wheel_t) * 2);
    if (c_wheels == RT_NULL)
    {
        LOG_D("Failed to malloc memory for wheels");
    }

    // 1.1 Create two motors
    motor_t left_motor  = motor_create(left_motor_init,  left_motor_enable,  left_motor_disable,  left_motor_set_speed,  DC_MOTOR);
    motor_t right_motor = motor_create(right_motor_init, right_motor_enable, right_motor_disable, right_motor_set_speed, DC_MOTOR);

    // 1.2 Create two encoders
    encoder_t left_encoder  = encoder_create(LEFT_ENCODER_PIN, PULSE_PER_REVOL);
    encoder_t right_encoder = encoder_create(RIGHT_ENCODER_PIN, PULSE_PER_REVOL);

    // 1.3 Create two pid contollers
    pid_control_t left_pid  = pid_create();
    pid_control_t right_pid = pid_create();

    // 1.4 Add two wheels
    c_wheels[0] = wheel_create(left_motor,  left_encoder,  left_pid,  WHEEL_RADIUS, GEAR_RATIO);
    c_wheels[1] = wheel_create(right_motor, right_encoder, right_pid, WHEEL_RADIUS, GEAR_RATIO);

    // 2. Iinialize Kinematics - Two Wheel Differential Drive
    kinematics_t c_kinematics = kinematics_create(TWO_WD, WHEEL_DIST_X, WHEEL_DIST_Y, WHEEL_RADIUS);

    // 3. Initialize Chassis
    chas = chassis_create(c_wheels, c_kinematics);

    // 4. Enable Chassis
    chassis_enable(chas);

   	// Set Sample time
    encoder_set_sample_time(chas->c_wheels[0]->w_encoder, SAMPLE_TIME);
    encoder_set_sample_time(chas->c_wheels[1]->w_encoder, SAMPLE_TIME);
    pid_set_sample_time(chas->c_wheels[0]->w_pid, PID_SAMPLE_TIME);
    pid_set_sample_time(chas->c_wheels[1]->w_pid, PID_SAMPLE_TIME);

    // tid_car = rt_thread_create("tcar",
    //                           car_thread, RT_NULL,
    //                           THREAD_STACK_SIZE,
    //                           THREAD_PRIORITY, THREAD_TIMESLICE);

    // if (tid_car != RT_NULL)
    // {
    //     rt_thread_startup(tid_car);
    // }
}


