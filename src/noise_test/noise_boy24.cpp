// Noise test firmware for Boy Rev A at 24 bits, temporary: for eurorack_electronics/docs/noise_test.md
// ("Rev A's input figure was flattered"). Reflash Boy's own firmware (env teensy41) after.
//
// noise_boy.cpp goes through the Teensy Audio Library, which keeps only the top 16 bits of each codec word
// (input_i2s.cpp reads I2S1_RDR0 + 2, 16-bit transfers), and noise_stream.h's round trip rounds the codec's
// hiss, a quarter of a 16-bit step, to silence. This one moves whole 32-bit words both ways with its own DMA,
// so the PCM3060's 24 bits reach the schedule and go back out, as Health's firmware does
// (firmware/revb_noise_test in eurorack_electronics).
//
// The clocks and pins are the library's own (AudioOutputI2S::config_i2s: MCLK 256 fs, BCK 64 fs, 32-bit
// slots, MSB first, I2S framing), so the codec sees exactly what it saw on 11 Nov. The schedule is
// noise_schedule.h with no screen states (record.py boy24 --no-screen): the OLED is never started, which
// is 11 Nov's "off".

#include <Arduino.h>
#include <Audio.h>
#include <DMAChannel.h>

#include "../teensy_move/board.h"
#include "noise_schedule.h"

#if AUDIO_BLOCK_SAMPLES != 48
#error "the noise test runs 48-sample blocks"
#endif

// config_i2s is protected; a using-declaration in a subclass reaches it without making one
struct I2SClocks : public AudioOutputI2S {
  using AudioOutputI2S::config_i2s;
};

static constexpr int N = AUDIO_BLOCK_SAMPLES;  // frames per half buffer
// two halves of N stereo frames, L R interleaved, one 32-bit word per slot
DMAMEM __attribute__((aligned(32))) static int32_t tx_buf[2 * N * 2];
DMAMEM __attribute__((aligned(32))) static int32_t rx_buf[2 * N * 2];
static DMAChannel dma_tx(false), dma_rx(false);

static noise::Player player;
static float in_l[N], in_r[N], out_l[N], out_r[N];
static volatile uint32_t blocks = 0;

static inline int32_t to_codec(float v) {
  v = v > 1.f ? 1.f : v < -1.f ? -1.f : v;
  return (int32_t)lrintf(v * 8388607.f) * 256;  // 24 bits at the top of the 32-bit slot
}

// on each half of the receive buffer: the half just filled in, through the schedule, into the half of the
// transmit buffer that isn't being sent
static void rx_isr() {
  const uint32_t daddr = (uint32_t)dma_rx.TCD->DADDR;
  dma_rx.clearInterrupt();
  const int32_t *src = daddr < (uint32_t)rx_buf + sizeof(rx_buf) / 2 ? &rx_buf[N * 2] : &rx_buf[0];
  const uint32_t saddr = (uint32_t)dma_tx.TCD->SADDR;
  int32_t *dst = saddr < (uint32_t)tx_buf + sizeof(tx_buf) / 2 ? &tx_buf[N * 2] : &tx_buf[0];
  arm_dcache_delete((void *)src, sizeof(rx_buf) / 2);
  for (int i = 0; i < N; i++) {
    in_l[i] = (float)src[2 * i] * (1.f / 2147483648.f);
    in_r[i] = (float)src[2 * i + 1] * (1.f / 2147483648.f);
  }
  player.Process(in_l, in_r, out_l, out_r, N);
  for (int i = 0; i < N; i++) {
    dst[2 * i] = to_codec(out_l[i]);
    dst[2 * i + 1] = to_codec(out_r[i]);
  }
  arm_dcache_flush_delete(dst, sizeof(tx_buf) / 2);
  blocks = blocks + 1;
}

static void audio_begin() {
  memset(tx_buf, 0, sizeof(tx_buf));
  arm_dcache_flush_delete(tx_buf, sizeof(tx_buf));
  dma_tx.begin(true);
  dma_rx.begin(true);
  I2SClocks::config_i2s();
  CORE_PIN7_CONFIG = 3;  // SAI1 TX_DATA0
  CORE_PIN8_CONFIG = 3;  // SAI1 RX_DATA0
  IOMUXC_SAI1_RX_DATA0_SELECT_INPUT = 2;

  dma_tx.TCD->SADDR = tx_buf;
  dma_tx.TCD->SOFF = 4;
  dma_tx.TCD->ATTR = DMA_TCD_ATTR_SSIZE(2) | DMA_TCD_ATTR_DSIZE(2);
  dma_tx.TCD->NBYTES_MLNO = 4;
  dma_tx.TCD->SLAST = -(int32_t)sizeof(tx_buf);
  dma_tx.TCD->DADDR = (void *)&I2S1_TDR0;
  dma_tx.TCD->DOFF = 0;
  dma_tx.TCD->CITER_ELINKNO = sizeof(tx_buf) / 4;
  dma_tx.TCD->DLASTSGA = 0;
  dma_tx.TCD->BITER_ELINKNO = sizeof(tx_buf) / 4;
  dma_tx.TCD->CSR = 0;
  dma_tx.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_TX);

  dma_rx.TCD->SADDR = (void *)&I2S1_RDR0;
  dma_rx.TCD->SOFF = 0;
  dma_rx.TCD->ATTR = DMA_TCD_ATTR_SSIZE(2) | DMA_TCD_ATTR_DSIZE(2);
  dma_rx.TCD->NBYTES_MLNO = 4;
  dma_rx.TCD->SLAST = 0;
  dma_rx.TCD->DADDR = rx_buf;
  dma_rx.TCD->DOFF = 4;
  dma_rx.TCD->CITER_ELINKNO = sizeof(rx_buf) / 4;
  dma_rx.TCD->DLASTSGA = -(int32_t)sizeof(rx_buf);
  dma_rx.TCD->BITER_ELINKNO = sizeof(rx_buf) / 4;
  dma_rx.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
  dma_rx.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_RX);
  dma_rx.attachInterrupt(rx_isr);

  dma_tx.enable();
  dma_rx.enable();
  I2S1_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
  I2S1_TCSR = I2S_TCSR_TE | I2S_TCSR_BCE | I2S_TCSR_FRDE;
}

void setup() {
  if (!board::begin()) Serial.println("noise test: board::begin() failed (no EEPROM answer)");
  player.Begin(false);
  // after board::begin(), which returns every board pin, the I2S ones among them, to an input first
  audio_begin();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    Serial.printf("noise_boy24: %lu blocks, %.1f per second\n", (unsigned long)blocks, blocks * 1000.f / millis());
  }
}
