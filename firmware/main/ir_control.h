#pragma once

#include <cstdint>
#include "driver/rmt_tx.h"

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

#define NEC_CARRIER_FREQ_HZ 38000

class IRControl {
public:
    IRControl();
    ~IRControl();

    bool begin(int tx_gpio = 45);

    void send_nec(uint16_t address, uint16_t command, int repeat = 0);
    void send_raw(const uint8_t* data, size_t len);
    void send_learned(const uint32_t* timings, size_t count);
    void stop();
    void set_carrier_freq(uint32_t freq_hz);

private:
    rmt_channel_handle_t m_tx_channel;
    rmt_encoder_handle_t m_encoder;
    bool m_initialized;
    int m_tx_gpio;
    uint32_t m_carrier_freq;

    void build_nec_symbols(rmt_symbol_word_t* symbols, uint16_t address, uint16_t command, size_t* count);
    void send_symbols(rmt_symbol_word_t* symbols, size_t count, int repeat);
};
