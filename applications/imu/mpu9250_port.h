#ifndef __MPU9250_PORT_H__
#define __MPU9250_PORT_H__

#include <stdint.h>

#define MPU6500_I2C_ADDR            ((uint8_t)0x68)
#define AK8963_I2C_ADDR             ((uint8_t)0x0C)

extern int       mpu9250_port_init(void);

extern void      mpu92_write_reg( uint8_t writeAddr, uint8_t writeData );
extern void      mpu92_write_regs( uint8_t writeAddr, uint8_t *writeData, uint8_t lens );
extern uint8_t   mpu92_read_reg( uint8_t readAddr );
extern void      mpu92_read_regs( uint8_t readAddr, uint8_t *readData, uint8_t lens );
extern void      mpu92_mag_write_reg( uint8_t writeAddr, uint8_t writeData );
extern void      mpu92_mag_write_regs( uint8_t writeAddr, uint8_t *writeData, uint8_t lens );
extern uint8_t   mpu92_mag_read_reg( uint8_t readAddr );
extern void      mpu92_mag_read_regs( uint8_t readAddr, uint8_t *readData, uint8_t lens );

#endif // __MPU9250_PORT_H__
