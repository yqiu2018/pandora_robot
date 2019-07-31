#include <rtthread.h>
#include <chassis.h>
#include <command.h>
#include <ps2.h>
#include <ano.h>
#include <dual_pwm_motor.h>
#include <ab_phase_encoder.h>
#include <inc_pid_controller.h>

#define DBG_SECTION_NAME  "car"
#define DBG_LEVEL         DBG_LOG
#include <rtdbg.h>

// MOTOR
#define LEFT_FORWARD_PWM            "pwm4"
#define LEFT_FORWARD_PWM_CHANNEL    3
#define LEFT_BACKWARD_PWM           "pwm4"
#define LEFT_BACKWARD_PWM_CHANNEL   4

#define RIGHT_FORWARD_PWM           "pwm2"
#define RIGHT_FORWARD_PWM_CHANNEL   3
#define RIGHT_BACKWARD_PWM          "pwm2"
#define RIGHT_BACKWARD_PWM_CHANNEL  4

// ENCODER
#define LEFT_ENCODER_A_PHASE_PIN    31      // GET_PIN(B, 15)
#define LEFT_ENCODER_B_PHASE_PIN    34      // GET_PIN(C, 2)
#define RIGHT_ENCODER_A_PHASE_PIN   38      // GET_PIN(C, 6)
#define RIGHT_ENCODER_B_PHASE_PIN   39      // GET_PIN(C, 7)
#define PULSE_PER_REVOL           2000      // Real value 2000
#define SAMPLE_TIME                 50

// CONTROLLER PID
#define PID_SAMPLE_TIME             50
#define PID_PARAM_KP                6.6
#define PID_PARAM_KI                6.5
#define PID_PARAM_KD                7.6

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

static rt_err_t car_forward(rt_int8_t cmd, void *param);
static rt_err_t car_backward(rt_int8_t cmd, void *param);
static rt_err_t car_turnleft(rt_int8_t cmd, void *param);
static rt_err_t car_turnright(rt_int8_t cmd, void *param);
static rt_err_t car_stop(rt_int8_t cmd, void *param);

static rt_thread_t tid_car = RT_NULL;

void car_thread(void *param)
{
    // TODO

    struct velocity target_velocity;

    target_velocity.linear_x = 0.00f;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = 0;
    chassis_set_velocity(chas, target_velocity);

    // Open control
    // auto_control_disable(chas->c_wheels[0]->w_control);
    // auto_control_disable(chas->c_wheels[1]->w_control);

    while (1)
    {
        rt_thread_mdelay(50);
        chassis_update(chas);
        // ano_send_senser(chas->c_wheels[0]->rpm, chas->c_wheels[0]->w_controller->target, chas->c_wheels[1]->rpm, chas->c_wheels[1]->w_controller->target,0,0,0,0,0,0);
    }

//    chassis_destroy(chas);
}

void car_init(void *parameter)
{
    // 1. Initialize two wheels - left and right
    wheel_t* c_wheels = (wheel_t*) rt_malloc(sizeof(wheel_t) * 2);
    if (c_wheels == RT_NULL)
    {
        LOG_D("Failed to malloc memory for wheels");
    }

    // 1.1 Create two motors
    dual_pwm_motor_t left_motor   = dual_pwm_motor_create(LEFT_FORWARD_PWM, LEFT_FORWARD_PWM_CHANNEL, LEFT_BACKWARD_PWM, LEFT_BACKWARD_PWM_CHANNEL);
    dual_pwm_motor_t right_motor  = dual_pwm_motor_create(RIGHT_FORWARD_PWM, RIGHT_FORWARD_PWM_CHANNEL, RIGHT_BACKWARD_PWM, RIGHT_BACKWARD_PWM_CHANNEL);

    // 1.2 Create two encoders
    ab_phase_encoder_t left_encoder  = ab_phase_encoder_create(LEFT_ENCODER_A_PHASE_PIN, LEFT_ENCODER_B_PHASE_PIN, PULSE_PER_REVOL);
    ab_phase_encoder_t right_encoder = ab_phase_encoder_create(RIGHT_ENCODER_A_PHASE_PIN, RIGHT_ENCODER_B_PHASE_PIN, PULSE_PER_REVOL);

    // 1.3 Create two pid contollers
    inc_pid_controller_t left_pid  = inc_pid_controller_create(PID_PARAM_KP, PID_PARAM_KI, PID_PARAM_KD);
    inc_pid_controller_t right_pid = inc_pid_controller_create(PID_PARAM_KP, PID_PARAM_KI, PID_PARAM_KD);

    // 1.4 Add two wheels
    c_wheels[0] = wheel_create((motor_t)left_motor,  (encoder_t)left_encoder,  (controller_t)left_pid,  WHEEL_RADIUS, GEAR_RATIO);
    c_wheels[1] = wheel_create((motor_t)right_motor, (encoder_t)right_encoder, (controller_t)right_pid, WHEEL_RADIUS, GEAR_RATIO);

    // 2. Iinialize Kinematics - Two Wheel Differential Drive
    kinematics_t c_kinematics = kinematics_create(TWO_WD, WHEEL_DIST_X, WHEEL_DIST_Y, WHEEL_RADIUS);

    // 3. Initialize Chassis
    chas = chassis_create(c_wheels, c_kinematics);

    // Set Sample time
    encoder_set_sample_time(chas->c_wheels[0]->w_encoder, SAMPLE_TIME);
    encoder_set_sample_time(chas->c_wheels[1]->w_encoder, SAMPLE_TIME);
    controller_set_sample_time(chas->c_wheels[0]->w_controller, PID_SAMPLE_TIME);
    controller_set_sample_time(chas->c_wheels[1]->w_controller, PID_SAMPLE_TIME);

    // 4. Enable Chassis
    chassis_enable(chas);

    // Register command
    command_register(COMMAND_CAR_STOP     , car_stop);
    command_register(COMMAND_CAR_FORWARD  , car_forward);
    command_register(COMMAND_CAR_BACKWARD , car_backward);
    command_register(COMMAND_CAR_TURNLEFT , car_turnleft);
    command_register(COMMAND_CAR_TURNRIGHT, car_turnright);

    rt_kprintf("car command register complete\n");

    // Controller
    ps2_init(28, 29, 4, 36);

    // thread
    tid_car = rt_thread_create("tcar",
                              car_thread, RT_NULL,
                              THREAD_STACK_SIZE,
                              THREAD_PRIORITY, THREAD_TIMESLICE);

    if (tid_car != RT_NULL)
    {
        rt_thread_startup(tid_car);
    }
}

static rt_err_t car_forward(rt_int8_t cmd, void *param)
{
    struct velocity target_velocity;

    target_velocity.linear_x = 0.05f;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = 0;
    chassis_set_velocity(chas, target_velocity);

    LOG_D("forward cmd");

    return RT_EOK;
}

static rt_err_t car_backward(rt_int8_t cmd, void *param)
{
    struct velocity target_velocity;

    target_velocity.linear_x = -0.05f;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = 0;
    chassis_set_velocity(chas, target_velocity);

    LOG_D("backward cmd");

    return RT_EOK;
}

static rt_err_t car_turnleft(rt_int8_t cmd, void *param)
{
    struct velocity target_velocity;

    target_velocity.linear_x = 0.00f;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = 0.5;
    chassis_set_velocity(chas, target_velocity);

    LOG_D("turnleft cmd");

    return RT_EOK;
}

static rt_err_t car_turnright(rt_int8_t cmd, void *param)
{
    struct velocity target_velocity;

    target_velocity.linear_x = 0.00f;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = -0.5;
    chassis_set_velocity(chas, target_velocity);

    LOG_D("turnright cmd");

    return RT_EOK;
}

static rt_err_t car_stop(rt_int8_t cmd, void *param)
{
    struct velocity target_velocity;

    target_velocity.linear_x = 0.00f;
    target_velocity.linear_y = 0;
    target_velocity.angular_z = 0;
    chassis_set_velocity(chas, target_velocity);

    LOG_D("stop cmd");

    return RT_EOK;
}




