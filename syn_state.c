#include "syn_state.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"

// ===== GLOBAL VARIABLES =====
volatile waveform_t current_waveform = WAVE_SINE;
volatile envelope_t current_envelope = ENV_SOFT_PAD;
volatile bool note_active[NUM_NOTES] = { false };
volatile bool tremolo_enabled = false;

/**
 * Parse a single UART command byte and update the synth engine state.
 */
void SYN_ParseCommand(uint8_t byte)
{
    waveform_t wf = (waveform_t)((byte >> 6) & 0x03);
    uint8_t note = (byte >> 3) & 0x07;
    bool note_on = ((byte >> 2) & 0x01);
    bool trem = ((byte >> 1) & 0x01);
    envelope_t env = (envelope_t)(byte & 0x01);

    current_waveform = wf;
    current_envelope = env;
    tremolo_enabled = trem;

    if (note < NUM_NOTES)
        note_active[note] = note_on;

    //LED DEBUG FEEDBACK
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);

    // PF1 = RED (any note on)
    // PF2 = BLUE (tremolo active)
    // PF3 = GREEN (Percussive envelope)
    bool any_on = false;
    uint8_t i = 0;
    for (i = 0; i < NUM_NOTES; ++i) {
        if (note_active[i]) {
            any_on = true;
            break;
        }
    }

    uint8_t led_mask = 0;
    if (any_on) led_mask |= GPIO_PIN_1;      // red
    if (tremolo_enabled) led_mask |= GPIO_PIN_2; // blue
    if (current_envelope == ENV_PERCUSSIVE) led_mask |= GPIO_PIN_3; // green

    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, led_mask);
}
