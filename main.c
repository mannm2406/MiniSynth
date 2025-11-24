#include <stdint.h>
#include <stdbool.h>

#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/interrupt.h"

#include "syn_uart.h"
#include "syn_state.h"
#include "syn_tables.h"
#include "syn_audio.h"

#define SYSCLK_HZ 80000000UL

uint32_t freq;
int main(void)
{
    // =======================================================
    // 1. System Clock Setup (80 MHz from PLL)
    // =======================================================
    SysCtlClockSet(SYSCTL_SYSDIV_2_5|SYSCTL_USE_PLL|SYSCTL_OSC_MAIN|SYSCTL_XTAL_16MHZ);

    freq = SysCtlClockGet();


    // =======================================================
    // 2. Debug LED setup (optional)
    // =======================================================
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);


    // =======================================================
    // 3. Initialize modules
    // =======================================================
    SYN_GenerateTables();    // Build all wave + envelope LUTs
    SYN_UART_Init(115200);   // Setup UART for PC communication
    SYN_Audio_Init();        // Setup PWM + Timer0A (16 kHz ISR)

    // =======================================================
    // 4. Enable global interrupts
    // =======================================================
    IntMasterEnable();

    // =======================================================
    // 5. Main Loop
    // =======================================================
    while (1)
    {
//        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);  // toggle LED if you want activity
        __asm(" WFI");  // Wait for interrupt (low power)
    }
}
