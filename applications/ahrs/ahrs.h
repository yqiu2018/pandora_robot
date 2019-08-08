#ifndef __AHRS_H__
#define __AHRS_H__

// typedef struct 
// {
// 	float x;
// 	float y;
// 	float z;
// } axis3f;

void ahrs_6dof_update(float gx, float gy, float gz, float ax, float ay, float az, float dt,  float *rpy);
void ahrs_9dof_update(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz, float dt, float *rpy);
void MadgwickAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz, float dt, float *rpy);


#endif // __AHRS_H__
