#include "led_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

static const char* TAG = "led";

// WS2812 timing constants (in nanosecond / RMT ticks at 40MHz = 25ns per tick)
// We'll use 1us = 40 ticks at 40MHz resolution
#define RMT_RESOLUTION_HZ  80000000  // 80MHz -> 12.5ns per tick

// WS2812 timings at 80MHz resolution
// T0H = 350ns  -> 28 ticks
// T0L = 800ns  -> 64 ticks
// T1H = 700ns  -> 56 ticks
// T1L = 600ns  -> 48 ticks
// RES = >280us -> don't care, auto by RMT

// We'll use a simpler approach: one symbol per bit
typedef struct {
    uint16_t duration0;  // High time (in RMT ticks at 80MHz = 12.5ns/tick)
    uint16_t duration1;  // Low time
} ws2812_bit_timing_t;

static const ws2812_bit_timing_t WS2812_T0 = { 28, 64 };  // 350ns high, 800ns low
static const ws2812_bit_timing_t WS2812_T1 = { 56, 48 };  // 700ns high, 600ns low

// Color ordering: GRB (WS2812)
#define LED_ORDER_G 0
#define LED_ORDER_R 1
#define LED_ORDER_B 2

// Convert RGB to GRB byte array
static inline void rgb_to_grb(uint8_t r, uint8_t g, uint8_t b, uint8_t* grb) {
    grb[0] = g;
    grb[1] = r;
    grb[2] = b;
}

int led_init(led_controller_t* led, int gpio_pin) {
    led->gpio_pin = gpio_pin;
    led->pattern = LED_PATTERN_OFF;
    led->pattern_speed = 0.5f;
    led->pattern_tick = 0;
    memset(led->leds, 0, sizeof(led->leds));

    // RMT TX channel config
    rmt_tx_channel_config_t tx_chan_cfg = {
        .gpio_num = (gpio_num_t)gpio_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 128,
        .trans_queue_depth = 4,
        .flags = {
            .invert_out = 0,
            .with_dma = 0,
            .io_loop_back = 0,
            .io_od_mode = 0
        }
    };
    esp_err_t err = rmt_new_tx_channel(&tx_chan_cfg, &led->tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return -1;
    }

    // Copy encoder
    rmt_copy_encoder_config_t copy_enc_cfg = {};
    err = rmt_new_copy_encoder(&copy_enc_cfg, &led->encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(err));
        rmt_del_channel(led->tx_chan);
        return -1;
    }

    led->initialized = true;
    ESP_LOGI(TAG, "LED strip initialized on GPIO %d", gpio_pin);
    return 0;
}

// Build symbol buffer for all LEDs
// Each LED = 24 bits -> 24 symbols
// Each symbol = (high_duration, low_duration) encoded as rmt_symbol_word_t where
//   level0=1, duration0=high_ticks
//   level1=0, duration1=low_ticks
static void build_symbols(led_controller_t* led, rmt_symbol_word_t* symbols, int* num_symbols) {
    int idx = 0;
    for (int led_idx = 0; led_idx < LED_NUM_LEDS; led_idx++) {
        uint8_t grb[3];
        rgb_to_grb(led->leds[led_idx].r, led->leds[led_idx].g, led->leds[led_idx].b, grb);
        
        for (int byte_idx = 0; byte_idx < 3; byte_idx++) {
            uint8_t byte_val = grb[byte_idx];
            for (int bit = 7; bit >= 0; bit--) {
                bool bit_val = (byte_val >> bit) & 1;
                const ws2812_bit_timing_t& timing = bit_val ? WS2812_T1 : WS2812_T0;
                symbols[idx].level0 = 1;
                symbols[idx].duration0 = timing.duration0;
                symbols[idx].level1 = 0;
                symbols[idx].duration1 = timing.duration1;
                idx++;
            }
        }
    }
    // Add reset symbol (low for > 280us = > 22400 ticks at 80MHz)
    // Use a smaller reset to save memory, ~300us = 24000 ticks
    symbols[idx].level0 = 0;
    symbols[idx].duration0 = 24000;
    symbols[idx].level1 = 0;
    symbols[idx].duration1 = 0;
    idx++;
    
    *num_symbols = idx;
}

void led_set(led_controller_t* led, int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= LED_NUM_LEDS) return;
    led->leds[index].r = r;
    led->leds[index].g = g;
    led->leds[index].b = b;
}

void led_set_all(led_controller_t* led, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_NUM_LEDS; i++) {
        led->leds[i].r = r;
        led->leds[i].g = g;
        led->leds[i].b = b;
    }
}

void led_set_pattern(led_controller_t* led, led_pattern_t pattern, led_color_t color, float speed) {
    led->pattern = pattern;
    led->pattern_color = color;
    led->pattern_speed = speed;
    led->pattern_tick = 0;
}

void led_show(led_controller_t* led) {
    if (!led->initialized) return;

    rmt_symbol_word_t symbols[LED_NUM_LEDS * 24 + 1];  // Max: 12*24 + reset
    int num_symbols;
    build_symbols(led, symbols, &num_symbols);

    rmt_tx_channel_enable(led->tx_chan);
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
            .eot_idle_duration_ns = 0,
            .encoding_phase = 0
        }
    };
    rmt_transmit(led->tx_chan, led->encoder, symbols,
                 num_symbols * sizeof(rmt_symbol_word_t), &tx_config);
    rmt_tx_wait_all_done(led->tx_chan, pdMS_TO_TICKS(10));
    rmt_tx_channel_disable(led->tx_chan);
}

bool led_update(led_controller_t* led) {
    if (!led->initialized) return false;

    led->pattern_tick++;
    bool changed = false;

    switch (led->pattern) {
        case LED_PATTERN_OFF:
            // Already off
            return false;

        case LED_PATTERN_SOLID:
            led_set_all(led, led->pattern_color.r, led->pattern_color.g, led->pattern_color.b);
            changed = true;
            break;

        case LED_PATTERN_BLINK: {
            uint32_t period = (uint32_t)(20.0f / (led->pattern_speed * led->pattern_speed + 0.1f));
            if (period < 4) period = 4;
            bool on = (led->pattern_tick % period) < (period / 2);
            led_set_all(led, 
                on ? led->pattern_color.r : 0,
                on ? led->pattern_color.g : 0,
                on ? led->pattern_color.b : 0);
            changed = true;
            break;
        }

        case LED_PATTERN_WAVE: {
            float speed = led->pattern_speed * 0.2f;
            for (int i = 0; i < LED_NUM_LEDS; i++) {
                float phase = (float)i / LED_NUM_LEDS * 6.2832f + (float)led->pattern_tick * speed;
                float intensity = (sinf(phase) + 1.0f) * 0.5f;
                led->leds[i].r = (uint8_t)(led->pattern_color.r * intensity);
                led->leds[i].g = (uint8_t)(led->pattern_color.g * intensity);
                led->leds[i].b = (uint8_t)(led->pattern_color.b * intensity);
            }
            changed = true;
            break;
        }

        case LED_PATTERN_RAINBOW: {
            float speed = led->pattern_speed * 0.1f;
            for (int i = 0; i < LED_NUM_LEDS; i++) {
                float hue = (float)i / LED_NUM_LEDS + (float)led->pattern_tick * speed;
                // HSV to RGB for rainbow
                int h = (int)(hue * 360) % 360;
                float x = 1.0f - fabsf(fmodf(hue * 6.0f, 2.0f) - 1.0f);
                float r = 0, g = 0, b = 0;
                int hi = (int)(hue * 6.0f) % 6;
                switch (hi) {
                    case 0: r = 1; g = x; break;
                    case 1: r = x; g = 1; break;
                    case 2: g = 1; b = x; break;
                    case 3: g = x; b = 1; break;
                    case 4: r = x; b = 1; break;
                    case 5: r = 1; b = x; break;
                }
                led->leds[i].r = (uint8_t)(r * 255);
                led->leds[i].g = (uint8_t)(g * 255);
                led->leds[i].b = (uint8_t)(b * 255);
            }
            changed = true;
            break;
        }
    }

    if (changed) {
        led_show(led);
    }
    return changed;
}

void led_off(led_controller_t* led) {
    led->pattern = LED_PATTERN_OFF;
    memset(led->leds, 0, sizeof(led->leds));
    led_show(led);
}

void led_deinit(led_controller_t* led) {
    if (led->initialized) {
        led_off(led);
        rmt_del_channel(led->tx_chan);
        if (led->encoder) {
            led->encoder->del(led->encoder);
        }
        led->initialized = false;
    }
}
