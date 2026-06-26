#pragma once

#include <cstdint>
#include "driver/rmt_tx.h"

// IRControl - NEC IR blaster via RMT peripheral
// Uses IR LED on M5Stack CoreS3 (typically GPIO 45 for IR)

// NEC protocol timings (us)
#define NEC_HEADER_HIGH    9000
#define NEC_HEADER_LOW     4500
#define NEC_BIT1_HIGH      560
#define NEC_BIT1_LOW       1690
#define NEC_BIT0_HIGH      560
#define NEC_BIT0_LOW       560
#define NEC_REPEAT_GAP     40000
#define NEC_REPEAT_HIGH    9000
#define NEC_REPEAT_LOW     2250

// Carrier frequency: 38kHz
#define NEC_CARRIER_FREQ_HZ 38000

class IRControl {
public:
    IRControl();
    ~IRControl();

    // Initialize RMT for IR transmission
    bool begin(int tx_gpio = 45, rmt_channel_t channel = RMT_CHANNEL_0);

    // Send a full NEC frame (16-bit address + 16-bit command)
    // address: device address
    // command: command code
    // repeat: number of repeat frames to send
    void send_nec(uint16_t address, uint16_t command, int repeat = 0);

    // Send raw NEC data (pre-encoded frame)
    void send_raw(const uint8_t* data, size_t len);

    // Send a learned NEC signal (already encoded)
    void send_learned(const uint32_t* timings, size_t count);

    // Stop current transmission
    void stop();

    // Set carrier frequency
    void set_carrier_freq(uint32_t freq_hz);

private:
    rmt_channel_handle_t m_tx_channel;
    rmt_encoder_handle_t m_encoder;
    rmt_tx_channel_config_t m_tx_config;
    bool m_initialized;
    int m_tx_gpio;
    uint32_t m_carrier_freq;

    // Internal helper to encode NEC packet
    void build_nec_symbols(rmt_symbol_word_t* symbols, uint16_t address, uint16_t command, size_t* count);
};
