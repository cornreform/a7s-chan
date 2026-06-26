#include "ir_control.h"
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "IRControl";

IRControl::IRControl()
    : m_tx_channel(nullptr)
    , m_encoder(nullptr)
    , m_initialized(false)
    , m_tx_gpio(45)
    , m_carrier_freq(NEC_CARRIER_FREQ_HZ)
{
}

IRControl::~IRControl() {
    stop();
    if (m_tx_channel) {
        rmt_del_channel(m_tx_channel);
    }
}

bool IRControl::begin(int tx_gpio, rmt_channel_t channel) {
    m_tx_gpio = tx_gpio;

    // RMT TX channel config
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = static_cast<gpio_num_t>(m_tx_gpio),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1 MHz, 1 us resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags = {
            .invert_out = false,
            .with_dma = false,
            .io_loop_back = false,
            .io_od_mode = false,
        }
    };

    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &m_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %d", err);
        return false;
    }

    // Carrier modulation for IR (38kHz)
    rmt_carrier_config_t carrier_config = {
        .carrier_en = true,
        .carrier_level = 1, // active high
        .carrier_duty_percent = 50,
        .carrier_freq_hz = m_carrier_freq,
    };

    err = rmt_apply_carrier(m_tx_channel, &carrier_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply carrier: %d", err);
        rmt_del_channel(m_tx_channel);
        m_tx_channel = nullptr;
        return false;
    }

    // Create a copy encoder for raw symbols
    rmt_copy_encoder_config_t copy_encoder_config = {};
    err = rmt_new_copy_encoder(&copy_encoder_config, &m_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create copy encoder: %d", err);
        rmt_del_channel(m_tx_channel);
        m_tx_channel = nullptr;
        return false;
    }

    // Enable the TX channel
    err = rmt_enable(m_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TX channel: %d", err);
        return false;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "IR blaster initialized on GPIO %d", m_tx_gpio);
    return true;
}

void IRControl::send_nec(uint16_t address, uint16_t command, int repeat) {
    if (!m_initialized) return;

    // Build NEC frame symbols
    // NEC frame: header + 8 address bits + 8 ~address bits + 8 command bits + 8 ~command bits
    // We encode as a sequence of RMT symbol words
    size_t symbol_count = 1 + 32 + 1; // header + 32 bits + stop bit
    size_t repeat_count = repeat;
    size_t total_symbols = symbol_count + (repeat_count * (1 + 32 + 1 + 1)); // +repeat gap per repeat

    rmt_symbol_word_t* symbols = (rmt_symbol_word_t*)heap_caps_malloc(
        total_symbols * sizeof(rmt_symbol_word_t), MALLOC_CAP_8BIT
    );
    if (!symbols) {
        ESP_LOGE(TAG, "Failed to allocate symbol buffer");
        return;
    }

    size_t idx = 0;

    // First frame
    build_nec_symbols(symbols, address, command, &idx);

    // Repeat frames
    for (int r = 0; r < repeat; r++) {
        // Repeat gap
        symbols[idx].duration0 = 40; // 40ms gap
        symbols[idx].level0 = 0;
        symbols[idx].duration1 = 0;
        symbols[idx].level1 = 0;
        idx++;

        build_nec_symbols(symbols + idx, address, command, &idx);
    }

    // Transmit
    rmt_tx_config_t tx_config = {
        .loop_count = 0, // one-shot
        .flags = {
            .stop_at_end = true
        }
    };

    esp_err_t err = rmt_transmit(m_tx_channel, m_encoder, symbols,
                                  idx * sizeof(rmt_symbol_word_t), &tx_config);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT transmit failed: %d", err);
    }

    // Wait for transmission to complete
    rmt_tx_wait_all_done(m_tx_channel, pdMS_TO_TICKS(1000));

    heap_caps_free(symbols);
}

void IRControl::send_raw(const uint8_t* data, size_t len) {
    // Send raw encoded symbol data
    if (!m_initialized || !data || len == 0) return;

    size_t symbol_count = len / sizeof(rmt_symbol_word_t);
    const rmt_symbol_word_t* symbols = reinterpret_cast<const rmt_symbol_word_t*>(data);

    rmt_tx_config_t tx_config = {
        .loop_count = 0,
        .flags = { .stop_at_end = true }
    };

    rmt_transmit(m_tx_channel, m_encoder, symbols,
                  symbol_count * sizeof(rmt_symbol_word_t), &tx_config);
    rmt_tx_wait_all_done(m_tx_channel, pdMS_TO_TICKS(1000));
}

void IRControl::send_learned(const uint32_t* timings, size_t count) {
    if (!m_initialized || !timings || count == 0) return;

    // Convert microsecond timing array to RMT symbols
    // Each pair is (mark, space) or a single ending mark
    size_t symbol_count = count / 2;

    rmt_symbol_word_t* symbols = (rmt_symbol_word_t*)heap_caps_malloc(
        symbol_count * sizeof(rmt_symbol_word_t), MALLOC_CAP_8BIT
    );
    if (!symbols) return;

    for (size_t i = 0; i < symbol_count; i++) {
        symbols[i].duration0 = timings[i * 2];
        symbols[i].level0 = 1; // mark
        if (i * 2 + 1 < count) {
            symbols[i].duration1 = timings[i * 2 + 1];
            symbols[i].level1 = 0; // space
        } else {
            symbols[i].duration1 = 0;
            symbols[i].level1 = 0;
        }
    }

    rmt_tx_config_t tx_config = {
        .loop_count = 0,
        .flags = { .stop_at_end = true }
    };

    rmt_transmit(m_tx_channel, m_encoder, symbols,
                  symbol_count * sizeof(rmt_symbol_word_t), &tx_config);
    rmt_tx_wait_all_done(m_tx_channel, pdMS_TO_TICKS(1000));

    heap_caps_free(symbols);
}

void IRControl::stop() {
    if (m_initialized && m_tx_channel) {
        rmt_disable(m_tx_channel);
        rmt_enable(m_tx_channel);
    }
}

void IRControl::set_carrier_freq(uint32_t freq_hz) {
    m_carrier_freq = freq_hz;
    if (m_initialized && m_tx_channel) {
        rmt_carrier_config_t carrier_config = {
            .carrier_en = true,
            .carrier_level = 1,
            .carrier_duty_percent = 50,
            .carrier_freq_hz = freq_hz,
        };
        rmt_apply_carrier(m_tx_channel, &carrier_config);
    }
}

void IRControl::build_nec_symbols(rmt_symbol_word_t* symbols, uint16_t address, uint16_t command, size_t* idx) {
    size_t i = *idx;

    // NEC header
    symbols[i].duration0 = NEC_HEADER_HIGH;
    symbols[i].level0 = 1;
    symbols[i].duration1 = NEC_HEADER_LOW;
    symbols[i].level1 = 0;
    i++;

    // 32 data bits: address (8) + ~address (8) + command (8) + ~command (8)
    uint32_t frame = 0;
    uint8_t addr_low = address & 0xFF;
    uint8_t addr_high = (address >> 8) & 0xFF;
    uint8_t cmd_low = command & 0xFF;
    uint8_t cmd_high = (command >> 8) & 0xFF;

    // Standard NEC: address + ~address + command + ~command
    uint32_t nec_data = ((uint32_t)addr_low) |
                        ((uint32_t)(~addr_low & 0xFF) << 8) |
                        ((uint32_t)cmd_low << 16) |
                        ((uint32_t)(~cmd_low & 0xFF) << 24);

    // Also support extended NEC (16-bit address)
    // If high byte of address is non-zero, send 16-bit address
    if (addr_high != 0) {
        nec_data = ((uint32_t)address) |
                   ((uint32_t)command << 16);
    }

    for (int bit = 0; bit < 32; bit++) {
        bool is_one = (nec_data >> bit) & 1;

        symbols[i].duration0 = NEC_BIT1_HIGH;
        symbols[i].level0 = 1;
        symbols[i].duration1 = is_one ? NEC_BIT1_LOW : NEC_BIT0_LOW;
        symbols[i].level1 = 0;
        i++;
    }

    // Stop bit (just a pulse with no trailing space)
    symbols[i].duration0 = NEC_BIT1_HIGH;
    symbols[i].level0 = 1;
    symbols[i].duration1 = 0;
    symbols[i].level1 = 0;
    i++;

    *idx = i;
}
