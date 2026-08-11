#pragma once

#include <stdint.h>

// Audio buffer geometry — matches Ksoloti firmware (axoloti_defines.h)
// 16 stereo frames per half-buffer, stored as interleaved int32_t L/R pairs.
// DMA double-buffers (buf/buf2, rbuf/rbuf2) are 32 words each.
// Set by the ADAU1961 PLL, not by us: the codec is I2S master and the SAI is slave, so
// this constant must mirror adau_init()'s PLL bytes (8 MHz * (4 + 12/125) = 32.768 MHz,
// = 1024 x FS). It read 48000 for a long while, which was simply wrong - nothing used it
// until the CPU budget did, and then it understated the budget by a third.
#define SAMPLERATE      32000
#define BUFSIZE         16      // stereo frames per DMA half-buffer
#define DOUBLE_BUFSIZE  32      // int32_t words per DMA buffer (BUFSIZE * 2 channels)

// DMA TX buffers (SAI1_A → codec DAC): CPU fills the inactive half while DMA sends the active half
extern int32_t buf[DOUBLE_BUFSIZE];
extern int32_t buf2[DOUBLE_BUFSIZE];

// DMA RX buffers (codec ADC → SAI1_B → CPU): CPU reads the inactive half
extern int32_t rbuf[DOUBLE_BUFSIZE];
extern int32_t rbuf2[DOUBLE_BUFSIZE];

// Initialise clocks (168 MHz, MCO1=8 MHz), SAI1 (slave stereo 32-bit), ADAU1961 (48 kHz),
// and DMA2 double-buffer streams.  Call once from main() before entering the audio loop.
void codec_init(void);

// Audio processing callback — invoked from DMA ISR every BUFSIZE frames (~333 µs at 48 kHz).
// inp  : DOUBLE_BUFSIZE int32_t words, interleaved L[0] R[0] L[1] R[1] … (24-bit, left-justified)
// outp : same layout, written by this function and sent to the DAC
// Provide your own definition in main.cc (or a DSP file).
extern "C" void computebufI(int32_t *inp, int32_t *outp);
