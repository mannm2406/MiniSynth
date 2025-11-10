#ifndef SYN_TABLES_H_
#define SYN_TABLES_H_

#include <stdint.h>

#define WAVE_TABLE_SIZE 2048

// ===== ENUMERATIONS =====
//typedef enum {
//    WAVE_SINE = 0,
//    WAVE_SQUARE,
//    WAVE_TRIANGLE,
//    WAVE_SAW,
//    WAVE_COUNT
//} waveform_t;
//
//typedef enum {
//    ENV_SOFT_PAD = 0,
//    ENV_PERCUSSIVE,
//    ENV_COUNT
//} envelope_t;

// ===== LUT POINTERS =====
extern const int16_t* wave_sine;
extern const int16_t* wave_square;
extern const int16_t* wave_triangle;
extern const int16_t* wave_saw;

// ===== ENVELOPE STRUCT =====
typedef struct {
    uint32_t attack_samples;
    uint32_t decay_samples;
    float sustain_level;
    uint32_t release_samples;
} envelope_params_t;

// ===== ENVELOPE INSTANCES =====
extern const envelope_params_t envelope_softpad;
extern const envelope_params_t envelope_percussive;

// ===== INITIALIZER =====
void SYN_GenerateTables(void);

#endif  // SYN_TABLES_H_
