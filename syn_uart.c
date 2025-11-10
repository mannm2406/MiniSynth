#include "syn_uart.h"
#include "syn_state.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"

void SYN_UART_Init(uint32_t baud)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0));

    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), baud,
                        (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));

    UARTFIFOEnable(UART0_BASE);
    UARTIntClear(UART0_BASE, UARTIntStatus(UART0_BASE, false));
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);

    IntRegister(INT_UART0, SYN_UART0_Handler);
    IntEnable(INT_UART0);

    UARTEnable(UART0_BASE);
}

void SYN_UART0_Handler(void)
{
    uint32_t status = UARTIntStatus(UART0_BASE, true);
    UARTIntClear(UART0_BASE, status);

    while (UARTCharsAvail(UART0_BASE))
    {
        uint8_t c = UARTCharGetNonBlocking(UART0_BASE);
        SYN_ParseCommand(c);                  // decode byte
        UARTCharPutNonBlocking(UART0_BASE, c); // echo back for debug
    }
}
