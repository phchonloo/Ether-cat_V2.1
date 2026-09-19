#ifndef USER_U_I2C_H
#define USER_U_I2C_H

#include "N32G45XSYS.h"

void i2c_init(void);
void IIC_Start(void);
void IIC_Stop(void);
uint8_t IIC_Wait_Ack(void);
void IIC_Ack(void);
void IIC_NAck(void);
void IIC_Send_Byte(uint8_t txd);
uint8_t IIC_Read_Byte(unsigned char ack);
int32_t i2cRead(uint8_t addr,
                uint8_t reg,
                uint8_t len,
                uint8_t *buf);
int32_t i2cWriteBuffer(uint8_t addr,
                       uint8_t reg,
                       uint8_t len,
                       uint8_t *data);
void delay_i2c(uint32_t nCount);

#define i2c_write(dev, reg, data, len) \
    (i2cWriteBuffer((dev), (reg), (len), (uint8_t *)(data)) == 0)
#define i2c_read(dev, reg, data, len) \
    (i2cRead((dev), (reg), (len), (uint8_t *)(data)) == 0)

#endif
