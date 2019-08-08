#ifndef __MD6_PORT_H__
#define __MD6_PORT_H__
extern int Sensors_I2C_WriteRegister_swap(unsigned char slave_addr, unsigned char reg_addr, unsigned char length, unsigned char const *data);
extern int Sensors_I2C_ReadRegister_swap (unsigned char slave_addr, unsigned char reg_addr, unsigned char length, unsigned char *data);
extern void Delay(unsigned long ms);
extern void stm32l_get_clock_ms(unsigned long *count);

extern void md6_port_init(void *param);

#endif // __MD6_PORT_H__
