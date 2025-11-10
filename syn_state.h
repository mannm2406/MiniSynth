#ifndef SYN_STATE_H_
#define SYN_STATE_H_

#include <stdint.h>
#include <stdbool.h>

#define NUM_NOTES 8  // C4..C5 range

// ===== ENUMERATIONS =====
typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_SAW,
    WAVE_COUNT
} waveform_t;

typedef enum {
    ENV_SOFT_PAD = 0,
    ENV_PERCUSSIVE,
    ENV_COUNT
} envelope_t;

// ===== GLOBAL SYNTH STATE =====
extern volatile waveform_t current_waveform;
extern volatile envelope_t current_envelope;
extern volatile bool note_active[NUM_NOTES];
extern volatile bool tremolo_enabled;

/**
 * @brief Parse an 8-bit UART command and update the synth state.
 *
 * Frame: [7-6]=waveform, [5-3]=note, [2]=on/off, [1]=tremolo, [0]=envelope
 */
void SYN_ParseCommand(uint8_t byte);

#endif  // SYN_STATE_H_
