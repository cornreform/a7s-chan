#include "ir_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdlib>

static const char* TAG = "ir";

// NEC encoder structure
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t* copy_encoder;
    int state;
    rmt_symbol_word_t nec_symbols[68];  // Pre-computed NEC frame symbols
    int num_symbols;
} nec_encoder_t;

// Build NEC frame symbols
static int nec_build_frame(nec_encoder_t* nec_enc, uint16_t address, uint16_t command) {
    int idx = 0;

    // Look-ahead / mark: check the header format expected by RMT
    // The rmt_symbol_word_t stores level (1=high) and duration0 (duration in RMT ticks)
    // For NEC: leading pulse = high for 9ms, leading space = low for 4.5ms
    // We'll use the copy encoder to send raw symbols.
    
    // Actually we'll use a simpler approach: build all symbols manually
    
    uint16_t addr = address & 0xFF;
    uint16_t inv_addr = (~addr) & 0xFF;
    uint16_t cmd = command & 0xFF;
    uint16_t inv_cmd = (~cmd) & 0xFF;
    uint32_t frame = ((uint32_t)cmd << 24) | ((uint32_t)inv_cmd << 16) | ((uint32_t)addr << 8) | inv_addr;

    // Leading pulse (active high)
    nec_enc->nec_symbols[idx].level0 = 1;
    nec_enc->nec_symbols[idx].duration0 = NEC_LEADING_PULSE_US;
    nec_enc->nec_symbols[idx].level1 = 0;  // Not used in RMT TX
    nec_enc->nec_symbols[idx].duration1 = 0;
    idx++;

    // Leading space (active low)
    nec_enc->nec_symbols[idx].level0 = 0;
    nec_enc->nec_symbols[idx].duration0 = NEC_LEADING_SPACE_US;
    nec_enc->nec_symbols[idx].level1 = 0;
    nec_enc->nec_symbols[idx].duration1 = 0;
    idx++;

    // 32 data bits (LSB first)
    for (int i = 0; i < 32; i++) {
        bool bit = (frame >> i) & 1;

        // Pulse (always the same)
        nec_enc->nec_symbols[idx].level0 = 1;
        nec_enc->nec_symbols[idx].duration0 = NEC_PULSE_US;
        nec_enc->nec_symbols[idx].level1 = 0;
        nec_enc->nec_symbols[idx].duration1 = 0;
        idx++;

        // Space (depends on bit value)
        nec_enc->nec_symbols[idx].level0 = 0;
        nec_enc->nec_symbols[idx].duration0 = bit ? NEC_ONE_SPACE_US : NEC_ZERO_SPACE_US;
        nec_enc->nec_symbols[idx].level1 = 0;
        nec_enc->nec_symbols[idx].duration1 = 0;
        idx++;
    }

    // End pulse (stop bit)
    nec_enc->nec_symbols[idx].level0 = 1;
    nec_enc->nec_symbols[idx].duration0 = NEC_PULSE_US;
    nec_enc->nec_symbols[idx].level1 = 0;
    nec_enc->nec_symbols[idx].duration1 = 0;
    idx++;

    nec_enc->num_symbols = idx;
    return idx;
}

// RMT encoder callbacks
static size_t nec_encoder_encode(rmt_encoder_t* encoder, rmt_channel_handle_t channel,
                                  const void* primary_data, size_t data_bytes,
                                  rmt_encode_state_t* ret_state) {
    nec_encoder_t* nec_enc = __containerof(encoder, nec_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    if (nec_enc->state == 0) {
        nec_enc->state = 1;
        // Encode the primary NEC frame
        encoded_symbols = nec_enc->copy_encoder->encode(
            nec_enc->copy_encoder, channel,
            nec_enc->nec_symbols,
            nec_enc->num_symbols * sizeof(rmt_symbol_word_t),
            &session_state
        );
        if (session_state & RMT_ENCODING_COMPLETE) {
            state = RMT_ENCODING_COMPLETE;
        }
    }

    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t nec_encoder_del(rmt_encoder_t* encoder) {
    nec_encoder_t* nec_enc = __containerof(encoder, nec_encoder_t, base);
    if (nec_enc->copy_encoder) {
        nec_enc->copy_encoder->del(nec_enc->copy_encoder);
    }
    free(nec_enc);
    return ESP_OK;
}

static esp_err_t nec_encoder_reset(rmt_encoder_t* encoder) {
    nec_encoder_t* nec_enc = __containerof(encoder, nec_encoder_t, base);
    nec_enc->state = 0;
    if (nec_enc->copy_encoder) {
        nec_enc->copy_encoder->reset(nec_enc->copy_encoder);
    }
    return ESP_OK;
}

int ir_init(ir_controller_t* ir, int gpio_pin) {
    ir->gpio_pin = gpio_pin;
    ir->initialized = false;

    // RMT TX channel config
    rmt_tx_channel_config_t tx_chan_cfg = {
        .gpio_num = (gpio_num_t)gpio_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz -> 1us resolution
        .mem_block_symbols = 128,
        .trans_queue_depth = 4,
        .flags = {
            .invert_out = 0,
            .with_dma = 0,
            .io_loop_back = 0,
            .io_od_mode = 0
        }
    };
    esp_err_t err = rmt_new_tx_channel(&tx_chan_cfg, &ir->tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return -1;
    }

    // Carrier config (38kHz NEC)
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = NEC_CARRIER_FREQ_HZ,
        .duty_cycle = 0.33f,
        .flags = {
            .polarity_active_low = 0,
            .always_on = 0
        }
    };
    err = rmt_apply_carrier(ir->tx_chan, &carrier_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_apply_carrier failed: %s", esp_err_to_name(err));
        rmt_del_channel(ir->tx_chan);
        return -1;
    }

    // Copy encoder for raw symbols
    rmt_copy_encoder_config_t copy_enc_cfg = {};
    rmt_encoder_handle_t copy_enc = NULL;
    err = rmt_new_copy_encoder(&copy_enc_cfg, &copy_enc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(err));
        rmt_del_channel(ir->tx_chan);
        return -1;
    }

    // Store copy encoder directly - we'll use it without a custom NEC encoder wrapper
    ir->encoder = copy_enc;

    // Enable the channel
    rmt_tx_channel_enable(ir->tx_chan);

    ir->initialized = true;
    ESP_LOGI(TAG, "IR initialized on GPIO %d", gpio_pin);
    return 0;
}

int ir_send_nec(ir_controller_t* ir, uint16_t address, uint16_t command, int repeat_count) {
    if (!ir->initialized) {
        ESP_LOGE(TAG, "IR not initialized");
        return -1;
    }

    // Build the NEC frame symbols
    rmt_symbol_word_t symbols[68];
    int idx = 0;

    uint16_t addr = address & 0xFF;
    uint16_t inv_addr = (~addr) & 0xFF;
    uint16_t cmd = command & 0xFF;
    uint16_t inv_cmd = (~cmd) & 0xFF;
    uint32_t frame = ((uint32_t)cmd << 24) | ((uint32_t)inv_cmd << 16) | ((uint32_t)addr << 8) | inv_addr;

    // Leading pulse + space
    symbols[idx].level0 = 1;
    symbols[idx].duration0 = NEC_LEADING_PULSE_US;
    symbols[idx].level1 = 0;
    symbols[idx].duration1 = NEC_LEADING_SPACE_US;
    idx++;

    // 32 data bits
    for (int i = 0; i < 32; i++) {
        bool bit = (frame >> i) & 1;
        symbols[idx].level0 = 1;
        symbols[idx].duration0 = NEC_PULSE_US;
        symbols[idx].level1 = 0;
        symbols[idx].duration1 = bit ? NEC_ONE_SPACE_US : NEC_ZERO_SPACE_US;
        idx++;
    }

    // End pulse
    symbols[idx].level0 = 1;
    symbols[idx].duration0 = NEC_PULSE_US;
    symbols[idx].level1 = 0;
    symbols[idx].duration1 = 0;
    idx++;

    // Send with repeat frames
    for (int r = 0; r <= repeat_count; r++) {
        rmt_tx_channel_enable(ir->tx_chan);
        
        if (r == 0) {
            // Send main frame
            rmt_transmit_config_t tx_config = {
                .loop_count = 0,
                .flags = {
                    .eot_level = 0,
                    .eot_idle_duration_ns = 0,
                    .encoding_phase = 0
                }
            };
            esp_err_t err = rmt_transmit(ir->tx_chan, ir->encoder, symbols,
                                         idx * sizeof(rmt_symbol_word_t), &tx_config);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
                return -1;
            }
        } else {
            // Repeat frame: leading pulse + repeat space + end pulse
            rmt_symbol_word_t repeat_sym[3];
            repeat_sym[0].level0 = 1;
            repeat_sym[0].duration0 = NEC_LEADING_PULSE_US;
            repeat_sym[0].level1 = 0;
            repeat_sym[0].duration1 = NEC_REPEAT_SPACE_US;
            repeat_sym[1].level0 = 1;
            repeat_sym[1].duration0 = NEC_PULSE_US;
            repeat_sym[1].level1 = 0;
            repeat_sym[1].duration1 = 0;

            rmt_transmit_config_t tx_config = {
                .loop_count = 0,
                .flags = {
                    .eot_level = 0,
                    .eot_idle_duration_ns = 0,
                    .encoding_phase = 0
                }
            };
            esp_err_t err = rmt_transmit(ir->tx_chan, ir->encoder, repeat_sym,
                                         3 * sizeof(rmt_symbol_word_t), &tx_config);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "rmt_transmit repeat failed: %s", esp_err_to_name(err));
                return -1;
            }
        }

        // Wait for transmission to complete
        // rmt_tx_wait_all_done(ir->tx_chan, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(110)); // ~108ms for one NEC frame
    }

    // Final disable
    rmt_tx_channel_disable(ir->tx_chan);

    return 0;
}

int ir_send_raw(ir_controller_t* ir, const uint32_t* durations_us, int num_durations) {
    if (!ir->initialized) return -1;

    // Build symbols from durations array
    // Each pair [on_us, off_us] forms one symbol
    int num_symbols = num_durations / 2;
    if (num_symbols == 0) return -1;

    // Allocate on stack (max 64 symbols = 128 entries)
    if (num_symbols > 64) num_symbols = 64;
    
    rmt_symbol_word_t symbols[64];
    for (int i = 0; i < num_symbols; i++) {
        symbols[i].level0 = 1;
        symbols[i].duration0 = durations_us[i * 2];
        symbols[i].level1 = 0;
        symbols[i].duration1 = durations_us[i * 2 + 1];
    }

    rmt_tx_channel_enable(ir->tx_chan);
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
            .eot_idle_duration_ns = 0,
            .encoding_phase = 0
        }
    };
    esp_err_t err = rmt_transmit(ir->tx_chan, ir->encoder, symbols,
                                 num_symbols * sizeof(rmt_symbol_word_t), &tx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit raw failed: %s", esp_err_to_name(err));
        return -1;
    }

    rmt_tx_wait_all_done(ir->tx_chan, pdMS_TO_TICKS(500));
    rmt_tx_channel_disable(ir->tx_chan);
    return 0;
}

void ir_deinit(ir_controller_t* ir) {
    if (ir->initialized) {
        rmt_tx_channel_disable(ir->tx_chan);
        rmt_del_channel(ir->tx_chan);
        if (ir->encoder) {
            ir->encoder->del(ir->encoder);
        }
        ir->initialized = false;
    }
}
