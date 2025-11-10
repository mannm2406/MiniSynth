#ifndef SYN_UART_H_
#define SYN_UART_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * Initializes UART0 (PA0 = RX, PA1 = TX)
 * param baud Desired baud rate (e.g. 115200)
 */
void SYN_UART_Init(uint32_t baud);

/**
 * UART0 interrupt handler.
 * Handles RX and TX interrupts (currently echoes received bytes).
 */
void SYN_UART0_Handler(void);

#endif  // SYN_UART_H_
