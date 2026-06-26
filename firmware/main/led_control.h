#pragma once

#include <cstdint>
#include "driver/spi_master.h"

// LEDControl - 12x RGB LEDs on M5Stack CoreS3
// CoreS3 has 12 RGB LEDs (SK6812 or WS2812B) driven via a single GPIO
// Uses SPI MOSI as bit-bang for WS2812/SK6812 protocol

// LED strip configuration
#define LED_COUNT          12
#define LED_GPIO           38  // CoreS3 RGB LED data pin
#define LED_SPI_HOST       SPI2_HOST
#define LED_SPI_CLK_SPEED  4000000  // 4MHz SPI for WS2812 timing

// RGB color structure
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

// Predefined colors
extern const rgb_color_t COLOR_OFF;
extern const rgb_color_t COLOR_RED;
extern const rgb_color_t COLOR_GREEN;
extern const rgb_color_t COLOR_BLUE;
extern const rgb_color_t COLOR_WHITE;
extern const rgb_color_t COLOR_YELLOW;
extern const rgb_color_t COLOR_CYAN;
extern const rgb_color_t COLOR_MAGENTA;
extern const rgb_color_t COLOR_ORANGE;
extern const rgb_color_t COLOR_PURPLE;

// Animation modes
typedef enum {
    LED_MODE_STATIC = 0,
    LED_MODE_BREATHING,
    LED_MODE_RAINBOW,
    LED_MODE_CHASE,
    LED_MODE_BLINK,
    LED_MODE_WAVE,
    LED_MODE_COUNT
} led_mode_t;

class LEDControl {
public:
    LEDControl();
    ~LEDControl();

    // Initialize SPI for LED data
    bool begin();

    // Set single LED color
    void set_led(int index, uint8_t r, uint8_t g, uint8_t b);
    void set_led(int index, const rgb_color_t& color);

    // Set all LEDs to same color
    void set_all(uint8_t r, uint8_t g, uint8_t b);
    void set_all(const rgb_color_t& color);

    // Fill range with color
    void fill(int start, int count, uint8_t r, uint8_t g, uint8_t b);

    // Set brightness (0-255)
    void set_brightness(uint8_t brightness);

    // Update LEDs (send data to strip)
    void show();

    // Clear all LEDs
    void clear();

    // Set animation mode
    void set_mode(led_mode_t mode);

    // Set animation speed (0-255)
    void set_speed(uint8_t speed);

    // Update animation - call from main loop
    void update(uint32_t now_ms);

    // Get current LED color
    rgb_color_t get_led(int index) const;

private:
    bool m_initialized;
    spi_device_handle_t m_spi_handle;
    rgb_color_t m_leds[LED_COUNT];
    rgb_color_t m_target_leds[LED_COUNT];
    uint8_t m_brightness;
    led_mode_t m_mode;
    uint8_t m_speed;

    // Animation state
    uint32_t m_last_update_ms;
    float m_phase;

    // Internal update methods
    void update_static(uint32_t now_ms);
    void update_breathing(uint32_t now_ms);
    void update_rainbow(uint32_t now_ms);
    void update_chase(uint32_t now_ms);
    void update_blink(uint32_t now_ms);
    void update_wave(uint32_t now_ms);

    // Convert RGB to SK6812/WS2812 SPI data format
    void rgb_to_spi_buffer(uint8_t* buffer, int buffer_len);
};
