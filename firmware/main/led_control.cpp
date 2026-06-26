#include "led_control.h"
#include <cstring>
#include <cmath>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char* TAG = "LEDControl";

// WS2812/SK6812 timing constants (SPI bit patterns)
// SPI at 4MHz, each byte = 0.25us, we use 3 bytes per bit
// Bit 1: 0b11111000 (three high bits, five low) = ~0.75us high, 1.25us low
// Bit 0: 0b11100000 (three high bits, five low, shifted) = ~0.75us high, 1.25us low
// Actually WS2812: 0 = 0.35us high, 0.8us low; 1 = 0.7us high, 0.6us low
// At 4MHz SPI (0.25us/bit):
// Bit 0: 0b10000000 -> 0.25us high, 1.75us low
// Bit 1: 0b11100000 -> 0.75us high, 1.25us low
// Still not perfect but close enough for most strips
// Better: use RMT for more precise timing

#define BITS_PER_LED    24      // 24 bits per LED (GRB order for WS2812)
#define SPI_BYTES_PER_BIT 3    // 3 SPI bytes per WS2812 bit
#define SPI_BUFFER_SIZE  (LED_COUNT * BITS_PER_LED * SPI_BYTES_PER_BIT)
#define LED_RESET_BYTES  120   // Reset pulse (50us+ low)

// Color definitions
const rgb_color_t COLOR_OFF     = {0, 0, 0};
const rgb_color_t COLOR_RED     = {255, 0, 0};
const rgb_color_t COLOR_GREEN   = {0, 255, 0};
const rgb_color_t COLOR_BLUE    = {0, 0, 255};
const rgb_color_t COLOR_WHITE   = {255, 255, 255};
const rgb_color_t COLOR_YELLOW  = {255, 255, 0};
const rgb_color_t COLOR_CYAN    = {0, 255, 255};
const rgb_color_t COLOR_MAGENTA = {255, 0, 255};
const rgb_color_t COLOR_ORANGE  = {255, 165, 0};
const rgb_color_t COLOR_PURPLE  = {128, 0, 128};

LEDControl::LEDControl()
    : m_initialized(false)
    , m_spi_handle(nullptr)
    , m_brightness(255)
    , m_mode(LED_MODE_STATIC)
    , m_speed(128)
    , m_last_update_ms(0)
    , m_phase(0.0f)
{
    for (int i = 0; i < LED_COUNT; i++) {
        m_leds[i] = COLOR_OFF;
        m_target_leds[i] = COLOR_OFF;
    }
}

LEDControl::~LEDControl() {
    if (m_spi_handle) {
        spi_bus_remove_device(m_spi_handle);
    }
    spi_bus_free(LED_SPI_HOST);
}

bool LEDControl::begin() {
    // Configure SPI bus for LED data
    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = LED_GPIO;
    bus_config.miso_io_num = -1;
    bus_config.sclk_io_num = -1;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = SPI_BUFFER_SIZE + LED_RESET_BYTES;

    esp_err_t err = spi_bus_initialize(LED_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %d", err);
        return false;
    }

    // Attach LED device (SPI device with custom settings)
    spi_device_interface_config_t dev_config = {};
    dev_config.mode = 0;
    dev_config.clock_speed_hz = LED_SPI_CLK_SPEED;
    dev_config.spics_io_num = -1; // No CS
    dev_config.flags = SPI_DEVICE_HALFDUPLEX;
    dev_config.queue_size = 1;

    err = spi_bus_add_device(LED_SPI_HOST, &dev_config, &m_spi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %d", err);
        spi_bus_free(LED_SPI_HOST);
        return false;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "LED control initialized, %d LEDs on GPIO %d", LED_COUNT, LED_GPIO);

    // Clear all LEDs
    clear();
    show();

    return true;
}

void LEDControl::set_led(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= LED_COUNT) return;
    m_leds[index].r = r;
    m_leds[index].g = g;
    m_leds[index].b = b;
}

void LEDControl::set_led(int index, const rgb_color_t& color) {
    set_led(index, color.r, color.g, color.b);
}

void LEDControl::set_all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_COUNT; i++) {
        m_leds[i].r = r;
        m_leds[i].g = g;
        m_leds[i].b = b;
    }
}

void LEDControl::set_all(const rgb_color_t& color) {
    set_all(color.r, color.g, color.b);
}

void LEDControl::fill(int start, int count, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = start; i < start + count && i < LED_COUNT; i++) {
        m_leds[i].r = r;
        m_leds[i].g = g;
        m_leds[i].b = b;
    }
}

void LEDControl::set_brightness(uint8_t brightness) {
    m_brightness = brightness;
}

void LEDControl::show() {
    if (!m_initialized) return;

    uint8_t buffer[SPI_BUFFER_SIZE + LED_RESET_BYTES];
    memset(buffer, 0, sizeof(buffer));

    // Convert RGB data to SPI bit patterns
    int buf_idx = 0;
    for (int led = 0; led < LED_COUNT; led++) {
        // Apply brightness
        uint8_t r = (m_leds[led].r * m_brightness) / 255;
        uint8_t g = (m_leds[led].g * m_brightness) / 255;
        uint8_t b = (m_leds[led].b * m_brightness) / 255;

        // WS2812 uses GRB order
        uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

        for (int bit = 23; bit >= 0; bit--) {
            bool is_one = (grb >> bit) & 1;
            // Bit 1: 0b11111000 (0xF8), Bit 0: 0b10000000 (0x80)
            uint8_t pattern = is_one ? 0xF8 : 0x80;
            for (int b = 0; b < SPI_BYTES_PER_BIT; b++) {
                buffer[buf_idx++] = pattern;
            }
        }
    }

    // Reset pulse
    memset(buffer + buf_idx, 0, LED_RESET_BYTES);

    // Send via SPI
    spi_transaction_t trans = {};
    trans.length = (SPI_BUFFER_SIZE + LED_RESET_BYTES) * 8; // bits
    trans.tx_buffer = buffer;

    spi_device_transmit(m_spi_handle, &trans);
}

void LEDControl::clear() {
    for (int i = 0; i < LED_COUNT; i++) {
        m_leds[i] = COLOR_OFF;
        m_target_leds[i] = COLOR_OFF;
    }
    show();
}

void LEDControl::set_mode(led_mode_t mode) {
    m_mode = mode;
    m_phase = 0.0f;
}

void LEDControl::set_speed(uint8_t speed) {
    m_speed = speed;
}

rgb_color_t LEDControl::get_led(int index) const {
    if (index < 0 || index >= LED_COUNT) return COLOR_OFF;
    return m_leds[index];
}

void LEDControl::update(uint32_t now_ms) {
    if (now_ms - m_last_update_ms < 20) return; // 50fps max
    m_last_update_ms = now_ms;

    switch (m_mode) {
        case LED_MODE_STATIC:
            // Nothing to animate
            break;
        case LED_MODE_BREATHING:
            update_breathing(now_ms);
            break;
        case LED_MODE_RAINBOW:
            update_rainbow(now_ms);
            break;
        case LED_MODE_CHASE:
            update_chase(now_ms);
            break;
        case LED_MODE_BLINK:
            update_blink(now_ms);
            break;
        case LED_MODE_WAVE:
            update_wave(now_ms);
            break;
        default:
            break;
    }
}

void LEDControl::update_breathing(uint32_t now_ms) {
    float freq = 0.5f + (m_speed / 255.0f) * 2.0f;
    m_phase += 0.05f * freq;
    float breath = (sinf(m_phase) + 1.0f) / 2.0f; // 0-1
    uint8_t bright = static_cast<uint8_t>(breath * 255.0f);

    for (int i = 0; i < LED_COUNT; i++) {
        m_target_leds[i] = m_leds[i];
    }

    // Apply breathing brightness
    uint8_t saved = m_brightness;
    m_brightness = bright;
    show();
    m_brightness = saved;
}

void LEDControl::update_rainbow(uint32_t now_ms) {
    float freq = (m_speed / 255.0f) * 0.1f;
    m_phase += freq;

    for (int i = 0; i < LED_COUNT; i++) {
        float hue = fmodf(m_phase + (float)i / LED_COUNT, 1.0f);
        // HSV to RGB (simplified)
        float h = hue * 6.0f;
        int h_int = static_cast<int>(h);
        float f = h - h_int;
        float q = 1.0f - f;
        float t = f;

        uint8_t r, g, b;
        switch (h_int % 6) {
            case 0: r = 255; g = static_cast<uint8_t>(t * 255); b = 0; break;
            case 1: r = static_cast<uint8_t>(q * 255); g = 255; b = 0; break;
            case 2: r = 0; g = 255; b = static_cast<uint8_t>(t * 255); break;
            case 3: r = 0; g = static_cast<uint8_t>(q * 255); b = 255; break;
            case 4: r = static_cast<uint8_t>(t * 255); g = 0; b = 255; break;
            case 5: r = 255; g = 0; b = static_cast<uint8_t>(q * 255); break;
            default: r = 0; g = 0; b = 0; break;
        }

        set_led(i, r, g, b);
    }

    show();
}

void LEDControl::update_chase(uint32_t now_ms) {
    float speed_factor = (m_speed / 255.0f) * 0.2f;
    m_phase += speed_factor;

    int pos = static_cast<int>(m_phase * LED_COUNT) % LED_COUNT;

    clear();
    set_led(pos, 255, 255, 255);
    set_led((pos + 1) % LED_COUNT, 100, 100, 100);

    show();
}

void LEDControl::update_blink(uint32_t now_ms) {
    float freq = 1.0f + (m_speed / 255.0f) * 4.0f;
    m_phase += 0.05f * freq;

    bool on = (static_cast<int>(m_phase * 10) % 2) == 0;

    if (on) {
        for (int i = 0; i < LED_COUNT; i++) {
            m_target_leds[i] = m_leds[i];
        }
        show();
    } else {
        uint8_t saved = m_brightness;
        m_brightness = 0;
        show();
        m_brightness = saved;
    }
}

void LEDControl::update_wave(uint32_t now_ms) {
    float speed_factor = (m_speed / 255.0f) * 0.15f;
    m_phase += speed_factor;

    for (int i = 0; i < LED_COUNT; i++) {
        float wave = (sinf(m_phase + (float)i * 2.0f * 3.14159f / LED_COUNT) + 1.0f) / 2.0f;
        uint8_t val = static_cast<uint8_t>(wave * 255.0f);

        // Apply wave to current target color
        set_led(i,
            (m_target_leds[i].r * val) / 255,
            (m_target_leds[i].g * val) / 255,
            (m_target_leds[i].b * val) / 255
        );
    }

    show();
}
