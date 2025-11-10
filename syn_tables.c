#include "syn_tables.h"
#include <math.h>
#include <stdint.h>

// ==========================================================
//  GLOBAL DEFINES
// ==========================================================
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AMP 32767.0f
#define N   WAVE_TABLE_SIZE

// ==========================================================
//  STATIC STORAGE FOR LUTs
// ==========================================================
static int16_t sine_table[N];
static int16_t square_table[N];
static int16_t triangle_table[N];
static int16_t saw_table[N];

// ==========================================================
//  PUBLIC POINTERS (extern in syn_tables.h)
// ==========================================================
const int16_t* wave_sine     = sine_table;
const int16_t* wave_square   = square_table;
const int16_t* wave_triangle = triangle_table;
const int16_t* wave_saw      = saw_table;

// ==========================================================
//  ENVELOPE DEFINITIONS
// ==========================================================
const envelope_params_t envelope_softpad = {
    .attack_samples  = 2000,   // Slow fade-in
    .decay_samples   = 4000,   // Gentle decay
    .sustain_level   = 0.8f,
    .release_samples = 4000
};

const envelope_params_t envelope_percussive = {
    .attack_samples  = 100,    // Instant attack
    .decay_samples   = 8000,    // Quick decay
    .sustain_level   = 0.001f,
    .release_samples = 800
};

// ==========================================================
//  LUT INITIALIZATION FUNCTION
// ==========================================================
void SYN_GenerateTables(void)
{
    uint32_t i = 0;
    for (i = 0; i < N; i++)
    {
        float phase = (2.0f * M_PI * i) / N;

        // === Sine ===
        sine_table[i] = (int16_t)(AMP * sinf(phase));

        // === Square ===
        square_table[i] = (i < (N / 2)) ? (int16_t)AMP : (int16_t)-AMP;

        // === Triangle ===
        if (i < N / 4)
            triangle_table[i] = (int16_t)(AMP * (4.0f * i / N));
        else if (i < 3 * N / 4)
            triangle_table[i] = (int16_t)(AMP * (2.0f - 4.0f * i / N));
        else
            triangle_table[i] = (int16_t)(AMP * (-4.0f + 4.0f * i / N));

        // === Sawtooth ===
        saw_table[i] = (int16_t)(-AMP + (2.0f * AMP * i / N));
    }
}
