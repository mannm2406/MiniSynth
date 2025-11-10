#include "syn_audio.h"
#include "syn_tables.h"
#include "syn_state.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>     // memset
#include <math.h>       // used only in init (not ISR)

#include "driverlib/sysctl.h"
#include "driverlib/pwm.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"

// =============================================================
// Configuration
// =============================================================
#define SYSCLK_HZ         80000000UL
#define PWM_FREQUENCY     40000U    // 40 kHz carrier (PWM)
#define MAX_VOICES        NUM_NOTES // reuse NUM_NOTES = 8

// =============================================================
// Note frequencies for C4..C5 (Hz)
// =============================================================
static const double note_freqs[MAX_VOICES] = {
    261.6255653005986, // C4
    293.6647679174076, // D4
    329.6275569128699, // E4
    349.2282314330039, // F4
    392.0,             // G4
    440.0,             // A4
    493.8833012561241, // B4
    523.2511306011972  // C5
};

// =============================================================
// Per-voice state
// =============================================================
typedef enum {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} env_state_t;

typedef struct {
    bool active;                // whether note is currently on (driven by syn_state.note_active)
    uint32_t phase_acc;         // 32-bit phase accumulator
    uint32_t phase_inc;         // phase increment per sample (fixed-point)
    env_state_t env_state;      // ADSR state machine
    int32_t env_level;          // Q15 (0..32768) envelope amplitude
    int32_t release_inc;        // Q15 decrement used during release
} voice_t;

static voice_t voices[MAX_VOICES];

// =============================================================
// Precomputed arrays & LFO state
// =============================================================
static uint32_t note_phase_inc[MAX_VOICES]; // precomputed phase_inc for each note

// LFO (tremolo) state
static uint32_t lfo_phase = 0;
static uint32_t lfo_phase_inc = 0; // set for default LFO freq (~5 Hz)
static int32_t lfo_depth_q15 = 0;  // Q15 (0..32768) e.g., depth 0.3 -> ~9830

// Envelope increments in Q15
static int32_t attack_inc_q15;
static int32_t decay_inc_q15;
static int32_t sustain_level_q15;
static int32_t default_release_inc_q15;

// Misc
static uint32_t pwm_period_counts = 1;

// =============================================================
// Utility macros
// =============================================================
#define Q15_ONE    (32768)   // 1.0 in Q15
#define CLAMP(x, lo, hi) do { if ((x) < (lo)) (x) = (lo); else if ((x) > (hi)) (x) = (hi); } while(0)

// =============================================================
// Forward declarations
// =============================================================
static void SYN_Audio_Precompute(void);
static inline uint32_t phase_to_index(uint32_t phase);
static inline int16_t sample_from_table(const int16_t *table, uint32_t phase);

// =============================================================
// Public initialization
// =============================================================
void SYN_Audio_Init(void)
{
    // 1) ensure waveform tables are present (should be called before)
    // SYN_GenerateTables() must have been called by main() already.

    // 2) precompute note increments, envelope rates, LFO defaults
    SYN_Audio_Precompute();

    // 3) enable peripherals
    SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_PWM0));
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB));
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0));

    // 4) configure GPIO pin for PWM: PB6 -> M0PWM0
    GPIOPinConfigure(GPIO_PB6_M0PWM0);
    GPIOPinTypePWM(GPIO_PORTB_BASE, GPIO_PIN_6);

    // 5) configure PWM clock & generator for desired PWM_FREQUENCY
    PWMClockSet(PWM0_BASE, PWM_SYSCLK_DIV_1);
    uint32_t pwmClock = SYSCLK_HZ / 1U;
    pwm_period_counts = pwmClock / PWM_FREQUENCY;
    if (pwm_period_counts < 4) pwm_period_counts = 4; // safety

    PWMGenConfigure(PWM0_BASE, PWM_GEN_0, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC);
    PWMGenPeriodSet(PWM0_BASE, PWM_GEN_0, pwm_period_counts);

    // start with center (50%) duty
    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_0, pwm_period_counts / 2);
    PWMOutputState(PWM0_BASE, PWM_OUT_0_BIT, true);
    PWMGenEnable(PWM0_BASE, PWM_GEN_0);

    // 6) Configure Timer0A to fire at AUDIO_SAMPLE_RATE
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    uint32_t timerLoad = SYSCLK_HZ / AUDIO_SAMPLE_RATE;
    TimerLoadSet(TIMER0_BASE, TIMER_A, timerLoad - 1);

    // register ISR, set priority, enable
    IntPrioritySet(INT_TIMER0A, 0x20);
    TimerIntRegister(TIMER0_BASE, TIMER_A, SYN_AudioISR);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);

    TimerEnable(TIMER0_BASE, TIMER_A);

    // voices array initialize
    uint32_t i = 0;
    for (i = 0; i < MAX_VOICES; ++i) {
        voices[i].active = false;
        voices[i].phase_acc = 0;
        voices[i].phase_inc = note_phase_inc[i];
        voices[i].env_state = ENV_IDLE;
        voices[i].env_level = 0;
        voices[i].release_inc = default_release_inc_q15;
    }
}

// =============================================================
// Precompute helper
// =============================================================
static void SYN_Audio_Precompute(void)
{
    // compute phase increments for each note:
    // phase_inc = note_freq * (2^32 / SAMPLE_RATE)
    double phase_scale = (double)( (uint64_t)1 << 32 ) / (double)AUDIO_SAMPLE_RATE;
    uint32_t i = 0;
    for (i = 0; i < MAX_VOICES; ++i) {
        double v = note_freqs[i] * phase_scale;
        if (v < 0.0) v = 0.0;
        note_phase_inc[i] = (uint32_t)(v + 0.5);
    }

    // Envelope params: use tables from syn_tables.h
    const envelope_params_t *envp = (current_envelope == ENV_PERCUSSIVE) ? &envelope_percussive : &envelope_softpad;
    // attack_inc: Q15 increment per sample = Q15_ONE / attack_samples
    if (envp->attack_samples > 0) attack_inc_q15 = (int32_t)(( (double)Q15_ONE / (double)envp->attack_samples ) + 0.5);
    else attack_inc_q15 = Q15_ONE;

    // decay: drop from Q15_ONE to sustain_level*Q15_ONE over decay_samples
    sustain_level_q15 = (int32_t)(envp->sustain_level * (double)Q15_ONE + 0.5);
    if (envp->decay_samples > 0) {
        decay_inc_q15 = (int32_t)(( (double)(Q15_ONE - sustain_level_q15) / (double)envp->decay_samples ) + 0.5);
    } else {
        decay_inc_q15 = (Q15_ONE - sustain_level_q15);
    }

    // default release increment (from sustain to 0)
    if (envp->release_samples > 0)
        default_release_inc_q15 = (int32_t)(( (double)sustain_level_q15 / (double)envp->release_samples ) + 0.5);
    else
        default_release_inc_q15 = sustain_level_q15;

    // LFO defaults: 5 Hz depth 0.25 (Q15)
    double lfo_hz = 5.0;
    double lfo_phase_scale = (double)( (uint64_t)1 << 32 ) / (double)AUDIO_SAMPLE_RATE;
    lfo_phase_inc = (uint32_t)(lfo_hz * lfo_phase_scale + 0.5);
    lfo_depth_q15 = (int32_t)(0.25 * (double)Q15_ONE + 0.5);

    // Ensure voices phase_inc set
    i = 0;
    for (i = 0; i < MAX_VOICES; ++i) voices[i].phase_inc = note_phase_inc[i];
}

// =============================================================
// Fast helpers (inline)
// =============================================================
static inline uint32_t phase_to_index(uint32_t phase)
{
    // phase is 32-bit, table size = 2^WAVE_TABLE_BITS
    // index = phase >> (32 - WAVE_TABLE_BITS)
    return (phase >> (32 - WAVE_TABLE_BITS)) & (WAVE_TABLE_SIZE - 1);
}

static inline int16_t sample_from_table(const int16_t *table, uint32_t phase)
{
    uint32_t idx = phase_to_index(phase);
    return table[idx];
}

// =============================================================
// Audio ISR: runs at AUDIO_SAMPLE_RATE (16 kHz)
// =============================================================
void SYN_AudioISR(void)
{
    // clear timer interrupt flag
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    // 1) read global user-controlled states (cheap copies)
    waveform_t cur_wave = current_waveform;
    envelope_t cur_env  = current_envelope;
    bool trem_on        = tremolo_enabled;

    // If envelope type changed since init, update incremental values.
    // (This check is relatively cheap: compare sustain_level)
    const envelope_params_t *envp = (cur_env == ENV_PERCUSSIVE) ? &envelope_percussive : &envelope_softpad;
    // Lazy update: if sustain_level differs from precomputed sustain, recompute per-sample increments
    int32_t env_sustain_q15 = (int32_t)(envp->sustain_level * (double)Q15_ONE + 0.5);
    if (env_sustain_q15 != sustain_level_q15)
    {
        // recompute
        if (envp->attack_samples > 0) attack_inc_q15 = (int32_t)(( (double)Q15_ONE / (double)envp->attack_samples ) + 0.5);
        else attack_inc_q15 = Q15_ONE;
        sustain_level_q15 = env_sustain_q15;
        if (envp->decay_samples > 0) decay_inc_q15 = (int32_t)(( (double)(Q15_ONE - sustain_level_q15) / (double)envp->decay_samples ) + 0.5);
        else decay_inc_q15 = (Q15_ONE - sustain_level_q15);
        if (envp->release_samples > 0) default_release_inc_q15 = (int32_t)(( (double)sustain_level_q15 / (double)envp->release_samples ) + 0.5);
        else default_release_inc_q15 = sustain_level_q15;
    }

    // 2) LFO step (update phase)
    lfo_phase += lfo_phase_inc;
    int16_t lfo_sample = sample_from_table(wave_sine, lfo_phase); // reuse sine LUT for LFO
    // lfo_sample is -32767..32767

    // 3) Per-voice processing & mixing
    int active_count = 0;
    int64_t mix_acc = 0; // 64-bit accumulator: sum of (sample * env_level_Q15) after Q15 downshift
    uint32_t i = 0;
    for (i = 0; i < MAX_VOICES; ++i)
    {
        bool want_on = note_active[i]; // from syn_state (volatile)
        voice_t *v = &voices[i];

        // state transitions: note-on event
        if (want_on && !v->active) {
            // start voice
            v->active = true;
//            v->phase_acc = 0;                 // can also keep previous for legato; reset for clarity
            v->phase_inc = note_phase_inc[i];
            v->env_state = ENV_ATTACK;
            v->env_level = 0;                 // start from 0 (Q15)
            v->release_inc = default_release_inc_q15;
        }
        // note-off event
        if (!want_on && v->active && v->env_state != ENV_RELEASE) {
            // begin release
            v->env_state = ENV_RELEASE;
            // compute release_inc based on current env_level and envelope's release time
            if (envp->release_samples > 0) {
                v->release_inc = (int32_t)(( (double) (v->env_level) / (double) envp->release_samples ) + 0.5);
                if (v->release_inc < 1) v->release_inc = 1;
            } else {
                v->release_inc = v->env_level;
            }
            // keep v->active true until env reaches zero
        }

        // If voice is idle and off, skip
        if (!v->active && v->env_state == ENV_IDLE) continue;

        // If note_active was false but env is release, we still process
        // Envelope update
        switch (v->env_state)
        {
            case ENV_ATTACK:
                v->env_level += attack_inc_q15;
                if (v->env_level >= Q15_ONE) {
                    v->env_level = Q15_ONE;
                    v->env_state = ENV_DECAY;
                }
                break;
            case ENV_DECAY:
                v->env_level -= decay_inc_q15;
                if (v->env_level <= sustain_level_q15) {
                    v->env_level = sustain_level_q15;
                    v->env_state = ENV_SUSTAIN;
                }
                break;
            case ENV_SUSTAIN:
                // stay at sustain level
                v->env_level = sustain_level_q15;
                break;
            case ENV_RELEASE:
                v->env_level -= v->release_inc;
                if (v->env_level <= 0) {
                    v->env_level = 0;
                    v->env_state = ENV_IDLE;
                    v->active = false;
                }
                break;
            case ENV_IDLE:
            default:
                v->env_level = 0;
                v->active = false;
                break;
        }

        if (v->env_state == ENV_IDLE || v->env_level == 0) {
            // voice silent, skip oscillator increment to save cycles
            continue;
        }

        // oscillator step
        v->phase_acc += v->phase_inc;
        const int16_t *table = wave_sine;
        switch (cur_wave) {
            case WAVE_SINE:    table = wave_sine; break;
            case WAVE_SQUARE:  table = wave_square; break;
            case WAVE_TRIANGLE:table = wave_triangle; break;
            case WAVE_SAW:     table = wave_saw; break;
            default: table = wave_sine; break;
        }
        int16_t sample = sample_from_table(table, v->phase_acc); // -32767..+32767

        // multiply sample by env_level (Q15): result in Q15 * int16 -> int32-ish
        // product = (int32_t)sample * (int32_t)v->env_level -> up to ~1.07e9 fits in 32 bit signed? borderline.
        // use 64-bit for safety.
        int64_t prod = (int64_t)sample * (int64_t)v->env_level; // range roughly -1.1e9..1.1e9
        // shift down by 15 to bring back to audio range
        int64_t scaled = prod >> 15; // now roughly -32767..+32767 scaled by envelope
        mix_acc += scaled;
        active_count++;
    }

    // 4) Normalize mix (prevent clipping)
    int32_t mixed_sample;
    if (active_count > 0) {
        // divide by active_count (integer)
        int64_t avg = mix_acc / active_count; // still in approx -32767..+32767
        // apply tremolo if enabled
        if (trem_on) {
            // trem multiplier = Q15_ONE + (lfo_sample * lfo_depth_q15) >> 15
            int32_t trem_mul = (int32_t)Q15_ONE + (int32_t)(( (int32_t)lfo_sample * lfo_depth_q15 ) >> 15);
            // apply multiplier: avg * trem_mul >> 15
            int64_t tremed = (avg * trem_mul) >> 15;
            // clamp to signed 16-bit range
            if (tremed > 32767) tremed = 32767;
            if (tremed < -32768) tremed = -32768;
            mixed_sample = (int32_t)tremed;
        } else {
            // no tremolo
            if (avg > 32767) avg = 32767;
            if (avg < -32768) avg = -32768;
            mixed_sample = (int32_t)avg;
        }
    } else {
        // no active voices: output silence (middle PWM)
        mixed_sample = 0;
    }

    // 5) Map mixed_sample (-32768..32767) to PWM duty (0..pwm_period_counts)
    // Convert to 0..65535 space: value = mixed_sample + 32768 (uint32)
    uint32_t uval = (uint32_t)( (int32_t)mixed_sample + 32768 );
    // duty = (uval * pwm_period_counts) >> 16
    uint64_t duty64 = (uint64_t)uval * (uint64_t)pwm_period_counts;
    uint32_t duty = (uint32_t)(duty64 >> 16);

    // clamp
    if (duty >= pwm_period_counts) duty = pwm_period_counts - 1;

    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_0, duty);
}
