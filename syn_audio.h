#ifndef SYN_AUDIO_H_
#define SYN_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>

#define AUDIO_SAMPLE_RATE 16000U   // 16 kHz sample rate
#define WAVE_TABLE_BITS   11U      // 2048 = 2^11
#define WAVE_TABLE_SIZE   2048

void SYN_Audio_Init(void);
void SYN_AudioISR(void);

#endif // SYN_AUDIO_H_
