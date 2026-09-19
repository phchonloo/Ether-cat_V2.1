#ifndef USER_SCI_INIT_H
#define USER_SCI_INIT_H

#include "mcuinit.h"

/* 串口 DMA 接收缓冲区大小（一帧最长字节数，按协议可调大） */
#define SCI_RX_BUF_SIZE     32
#define UART_BUFFER_SIZE      (256U)

void com_gpio_init(void);
void com_usart_init(void);
void SciReceive(void);
void SendFunction(u16 indata[]);
void CrcCal(u16 indata[]);
void RWFunction(u16 indata[]);

extern u16 CrcH;
extern u16 CrcL;
extern u8 RxSci_i;
extern u16 RxSciData[8];
extern u8 RxSciData_i;

/**
 * @brief Initialize the PA9/PA10 USART1 DMA command port.
 * @param[in] baudrate UART bit rate; zero selects 115200 bit/s.
 * @note Reception uses circular DMA and an idle-line interrupt.
 */
void uart_init(uint32_t baudrate);

/**
 * @brief Queue bytes for nonblocking USART1 DMA transmission.
 * @param[in] data Source bytes.
 * @param[in] length Number of bytes to queue.
 * @return Number of bytes accepted by the transmit queue.
 */
uint16_t uart_write(const uint8_t *data, uint16_t length);

/**
 * @brief Read bytes received by USART1 from the foreground queue.
 * @param[out] data Destination buffer.
 * @param[in] capacity Maximum number of bytes to read.
 * @return Number of bytes copied to data.
 */
uint16_t uart_read(uint8_t *data, uint16_t capacity);

/**
 * @brief Start transmission of queued USART1 command responses.
 * @note Call repeatedly from the foreground loop; the operation is nonblocking.
 */
void uart_process(void);

#endif
