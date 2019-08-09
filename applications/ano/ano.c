#include "ano.h"
#include <chassis.h>
#include <inc_pid_controller.h>

#define MQ_MSG_SIZE            50
#define MQ_MAX_MSGS            16

#define THREAD_STACK_SIZE      512
#define THREAD_PRIORITY        16
#define THREAD_TICK            10

//数据拆分宏定义，在发送大于1字节的数据类型时，比如int16、float等，需要把数据拆分成单独字节进行发送
#define BYTE0(dwTemp) (*((char *)(&dwTemp)))
#define BYTE1(dwTemp) (*((char *)(&dwTemp) + 1))
#define BYTE2(dwTemp) (*((char *)(&dwTemp) + 2))
#define BYTE3(dwTemp) (*((char *)(&dwTemp) + 3))

static rt_mutex_t ano_send_mutex = RT_NULL;
static rt_sem_t ano_ready_sem = RT_NULL;
static rt_thread_t ano_thread;

static void ano_parse_frame(uint8_t *buffer, uint8_t length);

extern uint8_t ano_read_byte_port(void);
extern void ano_send_data_port(uint8_t *buffer, uint8_t length);
extern chassis_t chas;

void _send_data(uint8_t *buffer, uint8_t length)
{
    if (ano_send_mutex != RT_NULL)
    {
        rt_mutex_take(ano_send_mutex, RT_WAITING_FOREVER);
        ano_send_data_port(buffer, length);
        rt_mutex_release(ano_send_mutex);
    }
}

// void _recv_data(uint8_t *buffer, uint8_t length)
// {
// //    if (ano_recv_mq != RT_NULL)
// //    {
// //        rt_mq_send(ano_recv_mq, buffer, length);
// //        rt_sem_release(ano_ready_sem);
// //    }
// }

static void ano_send_check(uint8_t head, uint8_t check_sum)
{
    uint8_t data_to_send[7];

    data_to_send[0] = 0xAA;
    data_to_send[1] = 0xAA;
    data_to_send[2] = 0xEF;
    data_to_send[3] = 2;
    data_to_send[4] = head;
    data_to_send[5] = check_sum;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < 6; i++)
        sum += data_to_send[i];
    data_to_send[6] = sum;

    _send_data(data_to_send, 7);
}

int ano_receive_byte(uint8_t data)
{    
    static uint8_t RxBuffer[50];
    static uint8_t _data_len = 0, _data_cnt = 0;
    static uint8_t state = 0;

    if (state == 0 && data == 0xAA)
    {
        state = 1;
        RxBuffer[0] = data;
    }
    else if (state == 1 && data == 0xAF)
    {
        state = 2;
        RxBuffer[1] = data;
    }
    else if (state == 2 && data < 0XF1)
    {
        state = 3;
        RxBuffer[2] = data;
    }
    else if (state == 3 && data < 50)
    {
        state = 4;
        RxBuffer[3] = data;
        _data_len = data;
        _data_cnt = 0;
    }
    else if (state == 4 && _data_len > 0)
    {
        _data_len--;
        RxBuffer[4 + _data_cnt++] = data;
        if (_data_len == 0)
            state = 5;
    }
    else if (state == 5)
    {
        state = 0;
        RxBuffer[4 + _data_cnt] = data;
        ano_parse_frame(RxBuffer, _data_cnt + 5);
        return 1;
    }
    else
        state = 0;
    
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
//Data_Receive_Anl函数是协议数据解析函数，函数参数是符合协议格式的一个数据帧，该函数会首先对协议数据进行校验
//校验通过后对数据进行解析，实现相应功能
//此函数可以不用用户自行调用，由函数Data_Receive_Prepare自动调用
static void ano_parse_frame(uint8_t *buffer, uint8_t length)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < (length - 1); i++)
        sum += *(buffer + i);
    if (!(sum == *(buffer + length - 1)))
        return; //判断sum
    if (!(*(buffer) == 0xAA && *(buffer + 1) == 0xAF))
        return; //判断帧头

    if (*(buffer + 2) == 0X01)
    {
        if (*(buffer + 4) == 0X01)
            // mpu6050.Acc_CALIBRATE = 1;
        if (*(buffer + 4) == 0X02)
            // mpu6050.Gyro_CALIBRATE = 1;
        if (*(buffer + 4) == 0X03)
        {
            // mpu6050.Acc_CALIBRATE = 1;
            // mpu6050.Gyro_CALIBRATE = 1;
        }
    }

    if (*(buffer + 2) == 0X02)
    {
        if (*(buffer + 4) == 0X01)
        {
            inc_pid_controller_t left = (inc_pid_controller_t)chas->c_wheels[0]->w_controller;
            // inc_pid_control_t right = (inc_pid_control_t)chas->c_wheels[1]->w_control;
            ano_send_pid(1, left->kp, left->ki, left->kd,0,0,0,0,0,0);
 
            // rt_kprintf("ano require pid param\n");
        }
        if (*(buffer + 4) == 0X02)
        {
        }
        if (*(buffer + 4) == 0XA0) //读取版本信息
        {
            // f.send_version = 1;
            rt_kprintf("ano read version info\n");
        }
        if (*(buffer + 4) == 0XA1) //恢复默认参数
        {
            // Para_ResetToFactorySetup();
            rt_kprintf("ano set default param\n");
        }
    }

    if (*(buffer + 2) == 0X10) //PID1
    {
        // ctrl_1.PID[PIDROLL].kp = 0.001 * ((volatile int16_t)(*(buffer + 4) << 8) | *(buffer + 5));
        // ctrl_1.PID[PIDROLL].ki = 0.001 * ((volatile int16_t)(*(buffer + 6) << 8) | *(buffer + 7));
        // ctrl_1.PID[PIDROLL].kd = 0.001 * ((volatile int16_t)(*(buffer + 8) << 8) | *(buffer + 9));
        // ctrl_1.PID[PIDPITCH].kp = 0.001 * ((volatile int16_t)(*(buffer + 10) << 8) | *(buffer + 11));
        // ctrl_1.PID[PIDPITCH].ki = 0.001 * ((volatile int16_t)(*(buffer + 12) << 8) | *(buffer + 13));
        // ctrl_1.PID[PIDPITCH].kd = 0.001 * ((volatile int16_t)(*(buffer + 14) << 8) | *(buffer + 15));
        // ctrl_1.PID[PIDYAW].kp = 0.001 * ((volatile int16_t)(*(buffer + 16) << 8) | *(buffer + 17));
        // ctrl_1.PID[PIDYAW].ki = 0.001 * ((volatile int16_t)(*(buffer + 18) << 8) | *(buffer + 19));
        // ctrl_1.PID[PIDYAW].kd = 0.001 * ((volatile int16_t)(*(buffer + 20) << 8) | *(buffer + 21));

        float kp,ki,kd;
        kp = 0.001 * ((int16_t)(*(buffer + 4) << 8) | *(buffer + 5));
        ki = 0.001 * ((int16_t)(*(buffer + 6) << 8) | *(buffer + 7));
        kd = 0.001 * ((int16_t)(*(buffer + 8) << 8) | *(buffer + 9));
        inc_pid_controller_set_kp((inc_pid_controller_t)chas->c_wheels[0]->w_controller, kp);
        inc_pid_controller_set_ki((inc_pid_controller_t)chas->c_wheels[0]->w_controller, ki);
        inc_pid_controller_set_kd((inc_pid_controller_t)chas->c_wheels[0]->w_controller, kd);

        inc_pid_controller_set_kp((inc_pid_controller_t)chas->c_wheels[1]->w_controller, kp);
        inc_pid_controller_set_ki((inc_pid_controller_t)chas->c_wheels[1]->w_controller, ki);
        inc_pid_controller_set_kd((inc_pid_controller_t)chas->c_wheels[1]->w_controller, kd);
        ano_send_check(*(buffer + 2), sum);
        // Param_SavePID();
    }
    if (*(buffer + 2) == 0X11) //PID2
    {
        // ctrl_1.PID[PID4].kp = 0.001 * ((volatile int16_t)(*(buffer + 4) << 8) | *(buffer + 5));
        // ctrl_1.PID[PID4].ki = 0.001 * ((volatile int16_t)(*(buffer + 6) << 8) | *(buffer + 7));
        // ctrl_1.PID[PID4].kd = 0.001 * ((volatile int16_t)(*(buffer + 8) << 8) | *(buffer + 9));
        // ctrl_1.PID[PID5].kp = 0.001 * ((volatile int16_t)(*(buffer + 10) << 8) | *(buffer + 11));
        // ctrl_1.PID[PID5].ki = 0.001 * ((volatile int16_t)(*(buffer + 12) << 8) | *(buffer + 13));
        // ctrl_1.PID[PID5].kd = 0.001 * ((volatile int16_t)(*(buffer + 14) << 8) | *(buffer + 15));
        // ctrl_1.PID[PID6].kp = 0.001 * ((volatile int16_t)(*(buffer + 16) << 8) | *(buffer + 17));
        // ctrl_1.PID[PID6].ki = 0.001 * ((volatile int16_t)(*(buffer + 18) << 8) | *(buffer + 19));
        // ctrl_1.PID[PID6].kd = 0.001 * ((volatile int16_t)(*(buffer + 20) << 8) | *(buffer + 21));
        ano_send_check(*(buffer + 2), sum);
        // Param_SavePID();
    }
    if (*(buffer + 2) == 0X12) //PID3
    {
        // ctrl_2.PID[PIDROLL].kp = 0.001 * ((volatile int16_t)(*(buffer + 4) << 8) | *(buffer + 5));
        // ctrl_2.PID[PIDROLL].ki = 0.001 * ((volatile int16_t)(*(buffer + 6) << 8) | *(buffer + 7));
        // ctrl_2.PID[PIDROLL].kd = 0.001 * ((volatile int16_t)(*(buffer + 8) << 8) | *(buffer + 9));
        // ctrl_2.PID[PIDPITCH].kp = 0.001 * ((volatile int16_t)(*(buffer + 10) << 8) | *(buffer + 11));
        // ctrl_2.PID[PIDPITCH].ki = 0.001 * ((volatile int16_t)(*(buffer + 12) << 8) | *(buffer + 13));
        // ctrl_2.PID[PIDPITCH].kd = 0.001 * ((volatile int16_t)(*(buffer + 14) << 8) | *(buffer + 15));
        // ctrl_2.PID[PIDYAW].kp = 0.001 * ((volatile int16_t)(*(buffer + 16) << 8) | *(buffer + 17));
        // ctrl_2.PID[PIDYAW].ki = 0.001 * ((volatile int16_t)(*(buffer + 18) << 8) | *(buffer + 19));
        // ctrl_2.PID[PIDYAW].kd = 0.001 * ((volatile int16_t)(*(buffer + 20) << 8) | *(buffer + 21));
        ano_send_check(*(buffer + 2), sum);
        // Param_SavePID();
    }
    if (*(buffer + 2) == 0X13) //PID4
    {
        ano_send_check(*(buffer + 2), sum);
    }
    if (*(buffer + 2) == 0X14) //PID5
    {
        ano_send_check(*(buffer + 2), sum);
    }
    if (*(buffer + 2) == 0X15) //PID6
    {
        ano_send_check(*(buffer + 2), sum);
    }
}

void ano_send_version(uint8_t hardware_type, uint16_t hardware_ver, uint16_t software_ver, uint16_t protocol_ver, uint16_t bootloader_ver)
{
    uint8_t data_to_send[14];
    uint8_t _cnt = 0;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0x00;
    data_to_send[_cnt++] = 0;

    data_to_send[_cnt++] = hardware_type;
    data_to_send[_cnt++] = BYTE1(hardware_ver);
    data_to_send[_cnt++] = BYTE0(hardware_ver);
    data_to_send[_cnt++] = BYTE1(software_ver);
    data_to_send[_cnt++] = BYTE0(software_ver);
    data_to_send[_cnt++] = BYTE1(protocol_ver);
    data_to_send[_cnt++] = BYTE0(protocol_ver);
    data_to_send[_cnt++] = BYTE1(bootloader_ver);
    data_to_send[_cnt++] = BYTE0(bootloader_ver);

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];
    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}
void ano_send_status(float angle_rol, float angle_pit, float angle_yaw, int32_t alt, uint8_t fly_model, uint8_t armed)
{
    uint8_t data_to_send[17];
    uint8_t _cnt = 0;
    volatile int16_t _temp;
    volatile int32_t _temp2 = alt;

    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0x01;
    data_to_send[_cnt++] = 0;

    _temp = (int)(angle_rol * 100);
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = (int)(angle_pit * 100);
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = (int)(angle_yaw * 100);
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);

    data_to_send[_cnt++] = BYTE3(_temp2);
    data_to_send[_cnt++] = BYTE2(_temp2);
    data_to_send[_cnt++] = BYTE1(_temp2);
    data_to_send[_cnt++] = BYTE0(_temp2);

    data_to_send[_cnt++] = fly_model;

    data_to_send[_cnt++] = armed;

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];
    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}
void ano_send_senser(int16_t a_x, int16_t a_y, int16_t a_z, int16_t g_x, int16_t g_y, int16_t g_z, int16_t m_x, int16_t m_y, int16_t m_z, int32_t bar)
{
    uint8_t data_to_send[23];
    uint8_t _cnt = 0;
    volatile int16_t _temp;

    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0x02;
    data_to_send[_cnt++] = 0;

    _temp = a_x;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = a_y;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = a_z;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);

    _temp = g_x;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = g_y;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = g_z;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);

    _temp = m_x;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = m_y;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = m_z;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];
    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}
void ano_send_rcdata(uint16_t thr, uint16_t yaw, uint16_t rol, uint16_t pit, uint16_t aux1, uint16_t aux2, uint16_t aux3, uint16_t aux4, uint16_t aux5, uint16_t aux6)
{
    uint8_t data_to_send[25];
    uint8_t _cnt = 0;

    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0x03;
    data_to_send[_cnt++] = 0;
    data_to_send[_cnt++] = BYTE1(thr);
    data_to_send[_cnt++] = BYTE0(thr);
    data_to_send[_cnt++] = BYTE1(yaw);
    data_to_send[_cnt++] = BYTE0(yaw);
    data_to_send[_cnt++] = BYTE1(rol);
    data_to_send[_cnt++] = BYTE0(rol);
    data_to_send[_cnt++] = BYTE1(pit);
    data_to_send[_cnt++] = BYTE0(pit);
    data_to_send[_cnt++] = BYTE1(aux1);
    data_to_send[_cnt++] = BYTE0(aux1);
    data_to_send[_cnt++] = BYTE1(aux2);
    data_to_send[_cnt++] = BYTE0(aux2);
    data_to_send[_cnt++] = BYTE1(aux3);
    data_to_send[_cnt++] = BYTE0(aux3);
    data_to_send[_cnt++] = BYTE1(aux4);
    data_to_send[_cnt++] = BYTE0(aux4);
    data_to_send[_cnt++] = BYTE1(aux5);
    data_to_send[_cnt++] = BYTE0(aux5);
    data_to_send[_cnt++] = BYTE1(aux6);
    data_to_send[_cnt++] = BYTE0(aux6);

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];

    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}
void ano_send_power(uint16_t votage, uint16_t current)
{
    uint8_t data_to_send[9];
    uint8_t _cnt = 0;
    uint16_t temp;

    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0x05;
    data_to_send[_cnt++] = 0;

    temp = votage;
    data_to_send[_cnt++] = BYTE1(temp);
    data_to_send[_cnt++] = BYTE0(temp);
    temp = current;
    data_to_send[_cnt++] = BYTE1(temp);
    data_to_send[_cnt++] = BYTE0(temp);

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];

    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}
void ano_send_motorpwm(uint16_t m_1, uint16_t m_2, uint16_t m_3, uint16_t m_4, uint16_t m_5, uint16_t m_6, uint16_t m_7, uint16_t m_8)
{
    uint8_t data_to_send[21];
    uint8_t _cnt = 0;

    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0x06;
    data_to_send[_cnt++] = 0;

    data_to_send[_cnt++] = BYTE1(m_1);
    data_to_send[_cnt++] = BYTE0(m_1);
    data_to_send[_cnt++] = BYTE1(m_2);
    data_to_send[_cnt++] = BYTE0(m_2);
    data_to_send[_cnt++] = BYTE1(m_3);
    data_to_send[_cnt++] = BYTE0(m_3);
    data_to_send[_cnt++] = BYTE1(m_4);
    data_to_send[_cnt++] = BYTE0(m_4);
    data_to_send[_cnt++] = BYTE1(m_5);
    data_to_send[_cnt++] = BYTE0(m_5);
    data_to_send[_cnt++] = BYTE1(m_6);
    data_to_send[_cnt++] = BYTE0(m_6);
    data_to_send[_cnt++] = BYTE1(m_7);
    data_to_send[_cnt++] = BYTE0(m_7);
    data_to_send[_cnt++] = BYTE1(m_8);
    data_to_send[_cnt++] = BYTE0(m_8);

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];

    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}
void ano_send_pid(uint8_t group, float p1_p, float p1_i, float p1_d, float p2_p, float p2_i, float p2_d, float p3_p, float p3_i, float p3_d)
{
    uint8_t data_to_send[23];
    uint8_t _cnt = 0;
    volatile int16_t _temp;

    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0x10 + group - 1;
    data_to_send[_cnt++] = 0;

    _temp = p1_p * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p1_i * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p1_d * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p2_p * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p2_i * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p2_d * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p3_p * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p3_i * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);
    _temp = p3_d * 1000;
    data_to_send[_cnt++] = BYTE1(_temp);
    data_to_send[_cnt++] = BYTE0(_temp);

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];

    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}

void ano_send_user_data(uint8_t number, float d0, float d1, float d2, float d3, float d4, int16_t d5, int16_t d6, int16_t d7, float d8)
{
    uint8_t data_to_send[23];
    uint8_t _cnt = 0;
    // volatile int16_t _temp;

    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xAA;
    data_to_send[_cnt++] = 0xF0 + number;
    data_to_send[_cnt++] = 0;

    data_to_send[_cnt++] = BYTE3(d0);
    data_to_send[_cnt++] = BYTE2(d0);
    data_to_send[_cnt++] = BYTE1(d0);
    data_to_send[_cnt++] = BYTE0(d0);

    data_to_send[_cnt++] = BYTE3(d1);
    data_to_send[_cnt++] = BYTE2(d1);
    data_to_send[_cnt++] = BYTE1(d1);
    data_to_send[_cnt++] = BYTE0(d1);

    data_to_send[_cnt++] = BYTE3(d2);
    data_to_send[_cnt++] = BYTE2(d2);
    data_to_send[_cnt++] = BYTE1(d2);
    data_to_send[_cnt++] = BYTE0(d2);

    data_to_send[_cnt++] = BYTE3(d3);
    data_to_send[_cnt++] = BYTE2(d3);
    data_to_send[_cnt++] = BYTE1(d3);
    data_to_send[_cnt++] = BYTE0(d3);

    data_to_send[_cnt++] = BYTE3(d4);
    data_to_send[_cnt++] = BYTE2(d4);
    data_to_send[_cnt++] = BYTE1(d4);
    data_to_send[_cnt++] = BYTE0(d4);

    data_to_send[_cnt++] = BYTE1(d5);
    data_to_send[_cnt++] = BYTE0(d5);
    data_to_send[_cnt++] = BYTE1(d6);
    data_to_send[_cnt++] = BYTE0(d6);
    data_to_send[_cnt++] = BYTE1(d7);
    data_to_send[_cnt++] = BYTE0(d7);

    data_to_send[_cnt++] = BYTE3(d8);
    data_to_send[_cnt++] = BYTE2(d8);
    data_to_send[_cnt++] = BYTE1(d8);
    data_to_send[_cnt++] = BYTE0(d8);

    data_to_send[3] = _cnt - 4;

    uint8_t sum = 0;
    for (uint8_t i = 0; i < _cnt; i++)
        sum += data_to_send[i];

    data_to_send[_cnt++] = sum;

    _send_data(data_to_send, _cnt);
}

void ano_byte_ready_indicate(void)
{
    rt_sem_release(ano_ready_sem);
}

// uint8_t ano_read_byte_port(void)
// {
//     ;
// }

// void ano_send_data_port(uint8_t *buffer, uint8_t length)
// {
//     ;
// }

void ano_thread_entry(void *param)
{
    while(1)
    {
        rt_sem_take(ano_ready_sem, RT_WAITING_FOREVER);
        ano_receive_byte(ano_read_byte_port());
    }
}

int ano_init(void *param)
{
    ano_send_mutex = rt_mutex_create("anoSend", RT_IPC_FLAG_FIFO);
    if (ano_send_mutex == RT_NULL)
    {
        rt_kprintf("Can't create mutex for ano\n");
    }
    ano_ready_sem = rt_sem_create("anoReady", 0, RT_IPC_FLAG_FIFO);
    if (ano_ready_sem == RT_NULL)
    {
        rt_kprintf("Can't create sem for ano\n");
        return RT_ERROR;
    }
    // ano_send_mq = rt_mq_create("anoSend", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    // if (ano_send_mq == RT_NULL)
    // {
    //     rt_kprintf("Can't create send_mq for ano\n");
    //     return RT_ERROR;
    // }
    // ano_recv_mq = rt_mq_create("anoRecv", MQ_MSG_SIZE, MQ_MAX_MSGS, RT_IPC_FLAG_FIFO);
    // if (ano_recv_mq == RT_NULL)
    // {
    //     rt_kprintf("Can't create recv_mq for ano\n");
    //     return RT_ERROR;
    // }

    ano_thread = rt_thread_create("ano", ano_thread_entry, RT_NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TICK);
    if (ano_thread == RT_NULL)
    {
        rt_kprintf("Can't create thread for ano\n");
        return RT_ERROR;
    }
    rt_thread_startup(ano_thread);
    rt_kprintf("ano_thread startup\n");

    return RT_EOK;
}
