#include <math.h>
#include "ahrs.h"


#define DEG2RAD        0.017453293f    /* 度转弧度 π/180 */
#define RAD2DEG        57.29578f        /* 弧度转度 180/π */

// Mahony_update(ahrs_9dof) parameters
#define TWOKP    (120.0f * 0.5f)    // 2 * proportional gain
#define TWOKI    (0.5f * 1.0f)    // 2 * integral gain
float integralFBx = 0.0f;   // integral error terms scaled by Ki
float integralFBy = 0.0f;
float integralFBz = 0.0f;  

// ahrs_6dof parameters
float Kp = 0.4f;        /*比例增益*/
float Ki = 0.001f;        /*积分增益*/
float exInt = 0.0f;
float eyInt = 0.0f;
float ezInt = 0.0f;        /*积分误差累计*/

static float q0 = 1.0f;    /*四元数*/
static float q1 = 0.0f;
static float q2 = 0.0f;
static float q3 = 0.0f;    

static float fast_invSqrt(float x)
{
    float halfx = 0.5f * x;
    float y = x;
    long i = *(long*)&y;
    i = 0x5f375a86 - (i>>1);
    y = *(float*)&i;
    y = y * (1.5f - (halfx * y * y));
    y = y * (1.5f - (halfx * y * y));
    return y;
}


/**
 * @note source from atk
 */
void ahrs_6dof_update(float gx, float gy, float gz, float ax, float ay, float az, float dt, float *rpy)
{
    float normalise;
    float ex, ey, ez;
    float q0s, q1s, q2s, q3s;    /*四元数的平方*/
    static float R11,R21;        /*矩阵(1,1),(2,1)项*/
    static float vecxZ, vecyZ, veczZ;    /*机体坐标系下的Z方向向量*/
    float halfT =0.5f * dt;

    gx = gx * DEG2RAD;    /* 度转弧度 */
    gy = gy * DEG2RAD;
    gz = gz * DEG2RAD;

    /* 某一个方向加速度不为0 */
    if((ax != 0.0f) || (ay != 0.0f) || (az != 0.0f))
    {
        /*单位化加速计测量值*/
        normalise = fast_invSqrt(ax * ax + ay * ay + az * az);
        ax *= normalise;
        ay *= normalise;
        az *= normalise;

        /*加速计读取的方向与重力加速计方向的差值，用向量叉乘计算*/
        ex = (ay * veczZ - az * vecyZ);
        ey = (az * vecxZ - ax * veczZ);
        ez = (ax * vecyZ - ay * vecxZ);
        
        /*误差累计，与积分常数相乘*/
        exInt += Ki * ex * dt ;  
        eyInt += Ki * ey * dt ;
        ezInt += Ki * ez * dt ;
        
        /*用叉积误差来做PI修正陀螺零偏，即抵消陀螺读数中的偏移量*/
        gx += Kp * ex + exInt;
        gy += Kp * ey + eyInt;
        gz += Kp * ez + ezInt;
    }
    /* 一阶近似算法，四元数运动学方程的离散化形式和积分 */
    q0 += (-q1 * gx - q2 * gy - q3 * gz) * halfT;
    q1 += (q0 * gx + q2 * gz - q3 * gy) * halfT;
    q2 += (q0 * gy - q1 * gz + q3 * gx) * halfT;
    q3 += (q0 * gz + q1 * gy - q2 * gx) * halfT;
    
    /*单位化四元数*/
    normalise = fast_invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= normalise;
    q1 *= normalise;
    q2 *= normalise;
    q3 *= normalise;
    /*四元数的平方*/
    q0s = q0 * q0;
    q1s = q1 * q1;
    q2s = q2 * q2;
    q3s = q3 * q3;
    
    R11 = q0s + q1s - q2s - q3s;    /*矩阵(1,1)项*/
    R21 = 2 * (q1 * q2 + q0 * q3);    /*矩阵(2,1)项*/

    /*机体坐标系下的Z方向向量*/
    vecxZ = 2 * (q1 * q3 - q0 * q2);/*矩阵(3,1)项*/
    vecyZ = 2 * (q0 * q1 + q2 * q3);/*矩阵(3,2)项*/
    veczZ = q0s - q1s - q2s + q3s;    /*矩阵(3,3)项*/
    
    if (vecxZ>1) vecxZ=1;
    if (vecxZ<-1) vecxZ=-1;
    
    /*计算roll pitch yaw 欧拉角*/
    rpy[0] = atan2f(vecyZ, veczZ) * RAD2DEG;
    rpy[1] = -asinf(vecxZ) * RAD2DEG; 
    rpy[2] = atan2f(R21, R11) * RAD2DEG;
}


/**
 * @brief Mahony ahrs algorithm
 */
void ahrs_9dof_update(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz, float dt, float *rpy)
{
    float recipNorm;
    float q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;
    float hx, hy, bx, bz;
    float halfvx, halfvy, halfvz, halfwx, halfwy, halfwz;
    float halfex, halfey, halfez;
    float qa, qb, qc;
    // Convert gyroscope degrees/sec to radians/sec
    gx *= DEG2RAD;
    gy *= DEG2RAD;
    gz *= DEG2RAD;

    // Compute feedback only if accelerometer measurement valid
    // (avoids NaN in accelerometer normalisation)
    if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

        // Normalise accelerometer measurement
        recipNorm = fast_invSqrt(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;

        // Normalise magnetometer measurement
        recipNorm = fast_invSqrt(mx * mx + my * my + mz * mz);
        mx *= recipNorm;
        my *= recipNorm;
        mz *= recipNorm;

        // Auxiliary variables to avoid repeated arithmetic
        q0q0 = q0 * q0;
        q0q1 = q0 * q1;
        q0q2 = q0 * q2;
        q0q3 = q0 * q3;
        q1q1 = q1 * q1;
        q1q2 = q1 * q2;
        q1q3 = q1 * q3;
        q2q2 = q2 * q2;
        q2q3 = q2 * q3;
        q3q3 = q3 * q3;

        // Reference direction of Earth's magnetic field
        hx = 2.0f * (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2));
        hy = 2.0f * (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) + mz * (q2q3 - q0q1));
        bx = sqrtf(hx * hx + hy * hy);
        bz = 2.0f * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5f - q1q1 - q2q2));

        // Estimated direction of gravity and magnetic field
        halfvx = q1q3 - q0q2;
        halfvy = q0q1 + q2q3;
        halfvz = q0q0 - 0.5f + q3q3;
        halfwx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
        halfwy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
        halfwz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);

        // Error is sum of cross product between estimated direction
        // and measured direction of field vectors
        halfex = (ay * halfvz - az * halfvy) + (my * halfwz - mz * halfwy);
        halfey = (az * halfvx - ax * halfvz) + (mz * halfwx - mx * halfwz);
        halfez = (ax * halfvy - ay * halfvx) + (mx * halfwy - my * halfwx);

        // Compute and apply integral feedback if enabled
        if(TWOKI > 0.0f) {
            // integral error scaled by Ki
            integralFBx += TWOKI * halfex * dt;
            integralFBy += TWOKI * halfey * dt;
            integralFBz += TWOKI * halfez * dt;
            gx += integralFBx;    // apply integral feedback
            gy += integralFBy;
            gz += integralFBz;
        } else {
            integralFBx = 0.0f;    // prevent integral windup
            integralFBy = 0.0f;
            integralFBz = 0.0f;
        }

        // Apply proportional feedback
        gx += TWOKP * halfex;
        gy += TWOKP * halfey;
        gz += TWOKP * halfez;
    }

    // Integrate rate of change of quaternion
    gx *= (0.5f * dt);        // pre-multiply common factors
    gy *= (0.5f * dt);
    gz *= (0.5f * dt);
    qa = q0;
    qb = q1;
    qc = q2;
    q0 += (-qb * gx - qc * gy - q3 * gz);
    q1 += (qa * gx + qc * gz - q3 * gy);
    q2 += (qa * gy - qb * gz + q3 * gx);
    q3 += (qa * gz + qb * gy - qc * gx);

    // Normalise quaternion
    recipNorm = fast_invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;
    
    // roll 
    rpy[0] = RAD2DEG * atan2f(q0*q1 + q2*q3, 0.5f - q1*q1 - q2*q2);
    // pitch
    rpy[1] = RAD2DEG * asinf(-2.0f * (q1*q3 - q0*q2));
    // yaw 
    rpy[2] = RAD2DEG * atan2f(q1*q2 + q0*q3, 0.5f - q2*q2 - q3*q3);
}


#define BETA_DEF        0.1f    // 2 * proportional gain


//---------------------------------------------------------------------------------------------------
// IMU algorithm update

void MadgwickAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az, float dt, float *rpy) 
{
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

    gx = gx * DEG2RAD;    /* 度转弧度 */
    gy = gy * DEG2RAD;
    gz = gz * DEG2RAD;

    // Rate of change of quaternion from gyroscope
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

    // Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
    if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

        // Normalise accelerometer measurement
        recipNorm = fast_invSqrt(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;   

        // Auxiliary variables to avoid repeated arithmetic
        _2q0 = 2.0f * q0;
        _2q1 = 2.0f * q1;
        _2q2 = 2.0f * q2;
        _2q3 = 2.0f * q3;
        _4q0 = 4.0f * q0;
        _4q1 = 4.0f * q1;
        _4q2 = 4.0f * q2;
        _8q1 = 8.0f * q1;
        _8q2 = 8.0f * q2;
        q0q0 = q0 * q0;
        q1q1 = q1 * q1;
        q2q2 = q2 * q2;
        q3q3 = q3 * q3;

        // Gradient decent algorithm corrective step
        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
        recipNorm = fast_invSqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); // normalise step magnitude
        s0 *= recipNorm;
        s1 *= recipNorm;
        s2 *= recipNorm;
        s3 *= recipNorm;

        // Apply feedback step
        qDot1 -= BETA_DEF * s0;
        qDot2 -= BETA_DEF * s1;
        qDot3 -= BETA_DEF * s2;
        qDot4 -= BETA_DEF * s3;
    }

    // Integrate rate of change of quaternion to yield quaternion
    q0 += qDot1 * dt; //(1.0f / sampleFreq);
    q1 += qDot2 * dt; //(1.0f / sampleFreq);
    q2 += qDot3 * dt; //(1.0f / sampleFreq);
    q3 += qDot4 * dt; //(1.0f / sampleFreq);

    // Normalise quaternion
    recipNorm = fast_invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;

    // roll 
    rpy[0] = RAD2DEG * atan2f(q0*q1 + q2*q3, 0.5f - q1*q1 - q2*q2);
    // pitch
    rpy[1] = RAD2DEG * asinf(-2.0f * (q1*q3 - q0*q2));
    // yaw 
    rpy[2] = RAD2DEG * atan2f(q1*q2 + q0*q3, 0.5f - q2*q2 - q3*q3);
}


void MadgwickAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz, float dt, float *rpy) 
{
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float hx, hy;
    float _2q0mx, _2q0my, _2q0mz, _2q1mx, _2bx, _2bz, _4bx, _4bz, _2q0, _2q1, _2q2, _2q3, _2q0q2, _2q2q3, q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;

    // Use IMU algorithm if magnetometer measurement invalid (avoids NaN in magnetometer normalisation)
    if((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f)) {
        MadgwickAHRSupdateIMU(gx, gy, gz, ax, ay, az, dt, rpy);
        return;
    }

    gx = gx * DEG2RAD;    /* 度转弧度 */
    gy = gy * DEG2RAD;
    gz = gz * DEG2RAD;

    // Rate of change of quaternion from gyroscope
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

    // Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
    if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

        // Normalise accelerometer measurement
        recipNorm = fast_invSqrt(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;   

        // Normalise magnetometer measurement
        recipNorm = fast_invSqrt(mx * mx + my * my + mz * mz);
        mx *= recipNorm;
        my *= recipNorm;
        mz *= recipNorm;

        // Auxiliary variables to avoid repeated arithmetic
        _2q0mx = 2.0f * q0 * mx;
        _2q0my = 2.0f * q0 * my;
        _2q0mz = 2.0f * q0 * mz;
        _2q1mx = 2.0f * q1 * mx;
        _2q0 = 2.0f * q0;
        _2q1 = 2.0f * q1;
        _2q2 = 2.0f * q2;
        _2q3 = 2.0f * q3;
        _2q0q2 = 2.0f * q0 * q2;
        _2q2q3 = 2.0f * q2 * q3;
        q0q0 = q0 * q0;
        q0q1 = q0 * q1;
        q0q2 = q0 * q2;
        q0q3 = q0 * q3;
        q1q1 = q1 * q1;
        q1q2 = q1 * q2;
        q1q3 = q1 * q3;
        q2q2 = q2 * q2;
        q2q3 = q2 * q3;
        q3q3 = q3 * q3;

        // Reference direction of Earth's magnetic field
        hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
        hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3;
        _2bx = sqrtf(hx * hx + hy * hy);
        _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 - mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
        _4bx = 2.0f * _2bx;
        _4bz = 2.0f * _2bz;

        // Gradient decent algorithm corrective step
        s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) + _2q1 * (2.0f * q0q1 + _2q2q3 - ay) - _2bz * q2 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) + _2q0 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q1 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) + _2bz * q3 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) + _2q3 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q2 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) + (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) + _2q2 * (2.0f * q0q1 + _2q2q3 - ay) + (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        recipNorm = fast_invSqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); // normalise step magnitude
        s0 *= recipNorm;
        s1 *= recipNorm;
        s2 *= recipNorm;
        s3 *= recipNorm;

        // Apply feedback step
        qDot1 -= BETA_DEF * s0;
        qDot2 -= BETA_DEF * s1;
        qDot3 -= BETA_DEF * s2;
        qDot4 -= BETA_DEF * s3;
    }

    // Integrate rate of change of quaternion to yield quaternion
    q0 += qDot1 * dt; //(1.0f / sampleFreq);
    q1 += qDot2 * dt; //(1.0f / sampleFreq);
    q2 += qDot3 * dt; //(1.0f / sampleFreq);
    q3 += qDot4 * dt; //(1.0f / sampleFreq);

    // Normalise quaternion
    recipNorm = fast_invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;

    // roll 
    rpy[0] = RAD2DEG * atan2f(q0*q1 + q2*q3, 0.5f - q1*q1 - q2*q2);
    // pitch
    rpy[1] = RAD2DEG * asinf(-2.0f * (q1*q3 - q0*q2));
    // yaw 
    rpy[2] = RAD2DEG * atan2f(q1*q2 + q0*q3, 0.5f - q2*q2 - q3*q3);
}
