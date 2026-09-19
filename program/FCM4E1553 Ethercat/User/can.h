#ifndef USER_CAN_H
#define USER_CAN_H

#include "N32G45XSYS.h"

#define BSP_CAN_PORT                    CAN1
#define BSP_CAN_CLK                     RCC_APB1_PERIPH_CAN1
#define BSP_CAN_GPIO_CLK                (RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO)
#define BSP_CAN_RX_GPIO_PORT            GPIOA
#define BSP_CAN_RX_GPIO_PIN             GPIO_PIN_11
#define BSP_CAN_TX_GPIO_PORT            GPIOA
#define BSP_CAN_TX_GPIO_PIN             GPIO_PIN_12
#define BSP_CAN_RX_IRQ                  USB_LP_CAN1_RX0_IRQn
#define BSP_CAN_RX_IRQ_PREEMPT_PRIORITY (2U)
#define BSP_CAN_RX_IRQ_SUB_PRIORITY     (1U)
#define BSP_CAN_RX_FIFO                 CAN_FIFO0
#define BSP_CAN_RX_INT                  CAN_INT_FMP0

#define BSP_CAN_FILTER_NUM              (0U)
#define BSP_CAN_FILTER_HIGH_ID          (0x0000U)
#define BSP_CAN_FILTER_LOW_ID           (0x0000U)
#define BSP_CAN_FILTER_MASK_HIGH_ID     (0x0000U)
#define BSP_CAN_FILTER_MASK_LOW_ID      (0x0000U)

#define BSP_CAN_TIMING_RSJW             CAN_RSJW_1tq
#define BSP_CAN_TIMING_TBS1             CAN_TBS1_13tq
#define BSP_CAN_TIMING_TBS2             CAN_TBS2_4tq

#define BSP_CAN_ID_STD                  (0U)
#define BSP_CAN_ID_EXT                  (1U)
#define BSP_CAN_RTR_DATA                (0U)
#define BSP_CAN_RTR_REMOTE              (1U)
#define BSP_CAN_DATA_MAX_LEN            (8U)

typedef struct
{
    uint32_t id;
    uint8_t id_type;
    uint8_t rtr;
    uint8_t dlc;
    uint8_t data[BSP_CAN_DATA_MAX_LEN];
} can_frame_t;

typedef void (*can_rx_callback_t)(const can_frame_t *frame);

/**
 * @brief Initialize CAN1 pins, timing, receive filter and FIFO0 interrupt.
 * @param[in] baudrate Requested rate: 125000, 250000 or 500000 bit/s;
 *                     zero or unsupported values select 500000 bit/s.
 */
void can_init(uint32_t baudrate);

/**
 * @brief Register the callback executed for each received CAN frame.
 * @param[in] callback Function called from the CAN RX interrupt, or null to
 *                     disable callback notification.
 * @warning The callback runs in interrupt context and must return quickly.
 */
void can_set_rx_callback(can_rx_callback_t callback);

/**
 * @brief Queue one CAN data or remote frame for transmission.
 * @param[in] frame Frame description; DLC values above 8 are saturated to 8.
 * @return 1 when a transmit mailbox accepts the frame; otherwise 0.
 */
uint8_t can_send(const can_frame_t *frame);

/**
 * @brief Read one frame from CAN receive FIFO0.
 * @param[out] frame Destination frame structure.
 * @return 1 when a frame was copied; 0 for null input or an empty FIFO.
 */
uint8_t can_read(can_frame_t *frame);

#endif
