#include "ir_control.h"
#include "esp_check.h"
#include "esp_log.h"

static const char* TAG = "IRControl";

IRControl::IRControl()
    : m_tx_channel(nullptr)
    , m_encoder(nullptr)
    , m_initialized(false)
    , m_tx_gpio(45)
    , m_carrier_freq(NEC_CARRIER_FREQ_HZ)
{}

IRControl::~IRControl() {
    stop();
}

bool IRControl::begin(int tx_gpio) {
    if (m_initialized) return true;

    m_tx_gpio = tx_gpio;

    // Create RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)m_tx_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1us resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &m_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %d", err);
        return false;
    }

    // Create a copy encoder
    rmt_copy_encoder_config_t encoder_config = {};
    err = rmt_new_copy_encoder(&encoder_config, &m_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT encoder: %d", err);
        rmt_del_channel(m_tx_channel);
        m_tx_channel = nullptr;
        return false;
    }

    // Apply carrier
    rmt_carrier_config_t carrier_config = {
        .frequency_hz = m_carrier_freq,
        .duty_cycle = 0.5,
    };
    err = rmt_apply_carrier(m_tx_channel, &carrier_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Carrier config failed: %d", err);
    }

    err = rmt_enable(m_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %d", err);
        rmt_del_channel(m_tx_channel);
        m_tx_channel = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

void IRControl::send_nec(uint16_t address, uint16_t command, int repeat) {
    if (!m_initialized) return;

    // NEC frame: address (low byte + high byte inverted) + command (low byte + high byte inverted)
    uint16_t addr_inv = ~address;
    uint16_t cmd_inv = ~command;

    // Total symbols: header + 32 data bits + stop bit (each = 2 symbols: high+low)
    // For repeat frames, we need header + stop
    size_t total_symbols = 2 + 32 * 2 + 2; // header + data + stop
    if (repeat > 0) {
        total_symbols += repeat * (2 + 2); // repeat gap + repeat header + stop
    }

    rmt_symbol_word_t* symbols = (rmt_symbol_word_t*)calloc(total_symbols, sizeof(rmt_symbol_word_t));
    if (!symbols) return;

    size_t idx = 0;

    // Header
    symbols[idx++] = (rmt_symbol_word_t){
        .duration0 = NEC_HEADER_HIGH,
        .level0 = 1,
        .duration1 = NEC_HEADER_LOW,
        .level1 = 0,
    };

    // Data bits (LSB first)
    uint32_t frame = (cmd_inv << 24) | (command << 16) | (addr_inv << 8) | address;
    for (int i = 0; i < 32; i++) {
        if (frame & 0x01) {
            symbols[idx++] = (rmt_symbol_word_t){
                .duration0 = NEC_BIT1_HIGH, .level0 = 1,
                .duration1 = NEC_BIT1_LOW, .level1 = 0,
            };
        } else {
            symbols[idx++] = (rmt_symbol_word_t){
                .duration0 = NEC_BIT0_HIGH, .level0 = 1,
                .duration1 = NEC_BIT0_LOW, .level1 = 0,
            };
        }
        frame >>= 1;
    }

    // Stop bit
    symbols[idx++] = (rmt_symbol_word_t){
        .duration0 = NEC_BIT0_HIGH, .level0 = 1,
        .duration1 = 0, .level1 = 0,
    };

    // Repeat frames
    for (int r = 0; r < repeat; r++) {
        // Repeat gap
        symbols[idx++] = (rmt_symbol_word_t){
            .duration0 = NEC_REPEAT_GAP, .level0 = 0,
            .duration1 = 0, .level1 = 0,
        };
        // Repeat header
        symbols[idx++] = (rmt_symbol_word_t){
            .duration0 = NEC_REPEAT_HIGH, .level0 = 1,
            .duration1 = NEC_REPEAT_LOW, .level1 = 0,
        };
        // Stop bit
        symbols[idx++] = (rmt_symbol_word_t){
            .duration0 = NEC_BIT0_HIGH, .level0 = 1,
            .duration1 = 0, .level1 = 0,
        };
    }

    send_symbols(symbols, idx, 1);
    free(symbols);
}

void IRControl::send_symbols(rmt_symbol_word_t* symbols, size_t count, int repeat) {
    if (!m_initialized || !m_tx_channel) return;

    rmt_tx_config_t tx_config = {
        .loop_count = repeat - 1,
        .flags = {
            .eot_level = 0,
        },
    };

    for (int i = 0; i < (repeat > 0 ? repeat : 1); i++) {
        esp_err_t err = rmt_transmit(m_tx_channel, m_encoder, symbols,
                                      count * sizeof(rmt_symbol_word_t), &tx_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RMT transmit failed: %d", err);
            break;
        }
    }

    // Wait for transmission to complete
    rmt_tx_wait_all_done(m_tx_channel, 1000);
}

void IRControl::send_raw(const uint8_t* data, size_t len) {
    if (!m_initialized || !data || len == 0) return;

    size_t symbol_count = len / 2; // 2 bytes per symbol (duration+level repeated)
    rmt_symbol_word_t* symbols = (rmt_symbol_word_t*)calloc(symbol_count, sizeof(rmt_symbol_word_t));
    if (!symbols) return;

    for (size_t i = 0; i < symbol_count; i++) {
        symbols[i].duration0 = data[i * 2];
        symbols[i].level0 = data[i * 2 + 1] & 0x01;
        symbols[i].duration1 = 0;
        symbols[i].level1 = 0;
    }

    send_symbols(symbols, symbol_count, 1);
    free(symbols);
}

void IRControl::send_learned(const uint32_t* timings, size_t count) {
    if (!m_initialized || !timings || count == 0) return;

    size_t symbol_count = count / 2;
    rmt_symbol_word_t* symbols = (rmt_symbol_word_t*)calloc(symbol_count, sizeof(rmt_symbol_word_t));
    if (!symbols) return;

    for (size_t i = 0; i < symbol_count; i++) {
        symbols[i].duration0 = timings[i * 2];
        symbols[i].level0 = 1;
        symbols[i].duration1 = (i * 2 + 1 < count) ? timings[i * 2 + 1] : 0;
        symbols[i].level1 = 0;
    }

    send_symbols(symbols, symbol_count, 1);
    free(symbols);
}

void IRControl::stop() {
    if (m_tx_channel) {
        rmt_disable(m_tx_channel);
        rmt_del_channel(m_tx_channel);
        m_tx_channel = nullptr;
    }
    if (m_encoder) {
        m_encoder = nullptr; // encoder is auto-deleted with channel
    }
    m_initialized = false;
}

void IRControl::set_carrier_freq(uint32_t freq_hz) {
    m_carrier_freq = freq_hz;
    if (m_initialized && m_tx_channel) {
        rmt_carrier_config_t carrier_config = {
            .frequency_hz = freq_hz,
            .duty_cycle = 0.5,
        };
        rmt_apply_carrier(m_tx_channel, &carrier_config);
    }
}
