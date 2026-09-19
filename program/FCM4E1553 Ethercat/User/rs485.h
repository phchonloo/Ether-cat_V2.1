#ifndef USER_RS485_H
#define USER_RS485_H

#include "N32G45XSYS.h"

#define RS485_PORT                   UART5
#define RS485_CLK                    RCC_APB1_PERIPH_UART5
#define RS485_GPIO_CLK               (RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_AFIO)
#define RS485_GPIO_REMAP             GPIO_RMP3_UART5
#define RS485_TX_GPIO_PORT           GPIOB
#define RS485_TX_GPIO_PIN            GPIO_PIN_8
#define RS485_RX_GPIO_PORT           GPIOB
#define RS485_RX_GPIO_PIN            GPIO_PIN_9
/* STN245M_ESC: SP3485 DI/RO use PB8/PB9 and DE plus /RE share PC13. */
#define RS485_DE_GPIO_CLK             RCC_APB2_PERIPH_GPIOC
#define RS485_DE_GPIO_PORT            GPIOC
#define RS485_DE_GPIO_PIN             GPIO_PIN_13
#define RS485_IRQ                    UART5_IRQn
#define RS485_IRQ_PREEMPT_PRIORITY   (3U)
#define RS485_IRQ_SUB_PRIORITY       (0U)
#define RS485_DMA_IRQ_PREEMPT_PRIORITY (3U)
#define RS485_DMA_IRQ_SUB_PRIORITY     (1U)
#define RS485_RX_BUFFER_SIZE         (256U)
#define RS485_TX_BUFFER_SIZE         (256U)

/**
 * @brief Initialize UART5, DMA reception/transmission and the RS485 DE pin.
 * @param[in] baudrate UART bit rate; zero selects 115200 bit/s.
 * @note Interface format is 8 data bits, no parity and one stop bit.
 */
void rs485_init(uint32_t baudrate);

/**
 * @brief Start a nonblocking RS485 DMA transmission.
 * @param[in] data Source buffer retained internally before DMA starts.
 * @param[in] length Number of bytes to send, 1..RS485_TX_BUFFER_SIZE.
 * @return 1 when accepted; 0 for invalid input or while another TX is active.
 * @note DE returns to receive mode only after the UART transmission-complete
 *       interrupt, not merely after the DMA transfer completes.
 */
uint8_t rs485_send(const uint8_t *data, uint16_t length);

/**
 * @brief Read one frame delimited by the UART idle interrupt.
 * @param[out] data Destination buffer.
 * @param[in] capacity Size of the destination buffer in bytes.
 * @return Received byte count, or 0 when no frame is ready/space is insufficient.
 * @note Call from the foreground loop; this function is not an ISR API.
 */
uint16_t rs485_read(uint8_t *data, uint16_t capacity);

#endif
