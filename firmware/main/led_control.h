#ifndef A7S_LED_CONTROL_H
#define A7S_LED_CONTROL_H

#include <cstdint>
#include <driver/rmt_tx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LED_NUM_LEDS     12

// RGB color structure (8-bit per channel)
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

// LED pattern types
typedef enum {
    LED_PATTERN_SOLID,   // All LEDs same color
    LED_PATTERN_BLINK,   // All LEDs blink on/off
    LED_PATTERN_WAVE,    // Wave animation across LEDs
    LED_PATTERN_RAINBOW, // Rainbow cycle
    LED_PATTERN_OFF      // All off
} led_pattern_t;

typedef struct {
    rmt_channel_handle_t tx_chan;
    rmt_encoder_handle_t encoder;
    int gpio_pin;
    led_color_t leds[LED_NUM_LEDS];
    
    // Pattern state
    led_pattern_t pattern;
    led_color_t pattern_color;
    float pattern_speed;  // 0.0 - 1.0
    uint32_t pattern_tick;
    
    bool initialized;
} led_controller_t;

// Initialize WS2812 LED strip on given GPIO using RMT
// Returns 0 on success
int led_init(led_controller_t* led, int gpio_pin);

// Set a single LED color (0-indexed)
void led_set(led_controller_t* led, int index, uint8_t r, uint8_t g, uint8_t b);

// Set all LEDs to same color
void led_set_all(led_controller_t* led, uint8_t r, uint8_t g, uint8_t b);

// Set pattern
void led_set_pattern(led_controller_t* led, led_pattern_t pattern, led_color_t color, float speed);

// Update LEDs (call periodically for animations)
// Returns true if display was refreshed
bool led_update(led_controller_t* led);

// Refresh display (send current buffer to LEDs)
void led_show(led_controller_t* led);

// Turn off all LEDs
void led_off(led_controller_t* led);

// Deinitialize
void led_deinit(led_controller_t* led);

#ifdef __cplusplus
}
#endif

#endif // A7S_LED_CONTROL_H
