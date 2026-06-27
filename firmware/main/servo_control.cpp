#include "servo_control.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char* TAG = "ServoControl";

// PY32 IO Expander on Stack-chan body module
#define PY32_I2C_ADDR       0x6F
#define PY32_I2C_NUM        0
#define PY32_I2C_SCL        11
#define PY32_I2C_SDA        12
#define PY32_I2C_SPEED      400000

// PWM registers
#define REG_PWM_FREQ_L      0x25
#define REG_PWM_FREQ_H      0x26
#define REG_PWM1_DUTY_L     0x1B  // Channel 0 = Pan
#define REG_PWM1_DUTY_H     0x1C
#define REG_PWM2_DUTY_L     0x1D  // Channel 1 = Tilt
#define REG_PWM2_DUTY_H     0x1E

// Servo parameters
#define SERVO_MIN_DUTY      8     // ~500µs at 50Hz (0°)
#define SERVO_MID_DUTY      20    // ~1500µs (90°)
#define SERVO_MAX_DUTY      32    // ~2500µs (180°)

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_py32_dev = NULL;

ServoControl::ServoControl() : m_initialized(false), m_pan_duty(SERVO_MID_DUTY), m_tilt_duty(SERVO_MID_DUTY) {}
ServoControl::~ServoControl() {}

bool ServoControl::begin() {
    m_initialized = init_py32();
    if (!m_initialized) {
        ESP_LOGW(TAG, "PY32 init failed - servos not available");
        return false;
    }
    
    // Set PWM frequency to 50Hz (servo standard)
    uint16_t freq = 50; // 50Hz
    uint8_t freq_data[] = {REG_PWM_FREQ_L, (uint8_t)(freq & 0xFF)};
    uint8_t freq_data_h[] = {REG_PWM_FREQ_H, (uint8_t)((freq >> 8) & 0xFF)};
    i2c_master_transmit(s_py32_dev, freq_data, sizeof(freq_data), 100);
    i2c_master_transmit(s_py32_dev, freq_data_h, sizeof(freq_data_h), 100);
    
    // Center servos
    set_pan(90);
    set_tilt(90);
    
    ESP_LOGI(TAG, "Servo init OK");
    return true;
}

bool ServoControl::init_py32() {
    // Initialize I2C bus
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = PY32_I2C_NUM,
        .sda_io_num = (gpio_num_t)PY32_I2C_SDA,
        .scl_io_num = (gpio_num_t)PY32_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags = { .enable_internal_pullup = true },
    };
    
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_STATE) {
            // Bus already initialized
            ESP_LOGI(TAG, "I2C bus already initialized");
        } else {
            ESP_LOGE(TAG, "I2C bus init failed: %d", err);
            return false;
        }
    }
    
    // Add PY32 device
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PY32_I2C_ADDR,
        .scl_speed_hz = PY32_I2C_SPEED,
    };
    
    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_py32_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PY32 device add failed: %d (is body module connected?)", err);
        return false;
    }
    
    // Probe PY32
    err = i2c_master_probe(s_i2c_bus, PY32_I2C_ADDR, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PY32 not responding at 0x%02X - servo body module may be absent", PY32_I2C_ADDR);
        return false;
    }
    
    ESP_LOGI(TAG, "PY32 found at 0x%02X", PY32_I2C_ADDR);
    return true;
}

void ServoControl::set_pwm_duty(uint8_t channel, uint8_t duty) {
    if (!s_py32_dev) return;
    uint8_t reg = REG_PWM1_DUTY_L + (channel * 2);
    uint8_t data[] = {reg, duty};
    i2c_master_transmit(s_py32_dev, data, sizeof(data), 100);
}

uint8_t ServoControl::angle_to_duty(float degrees) {
    if (degrees < 0) degrees = 0;
    if (degrees > 180) degrees = 180;
    // Map 0-180° → SERVO_MIN_DUTY to SERVO_MAX_DUTY
    return (uint8_t)(SERVO_MIN_DUTY + (degrees / 180.0f) * (SERVO_MAX_DUTY - SERVO_MIN_DUTY));
}

void ServoControl::set_pan(float degrees) {
    if (!m_initialized) return;
    m_pan_duty = angle_to_duty(degrees);
    set_pwm_duty(0, m_pan_duty); // Channel 0 = Pan
    ESP_LOGD(TAG, "Pan: %.0f° → duty %d", degrees, m_pan_duty);
}

void ServoControl::set_tilt(float degrees) {
    if (!m_initialized) return;
    m_tilt_duty = angle_to_duty(degrees);
    set_pwm_duty(1, m_tilt_duty); // Channel 1 = Tilt
    ESP_LOGD(TAG, "Tilt: %.0f° → duty %d", degrees, m_tilt_duty);
}
