#include "servo_control.h"
#include <cstring>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "ServoControl";

// UART config for SCS bus (1Mbps, half-duplex)
#define SCS_UART_BAUD    1000000
#define SCS_UART_TX_BUF  256
#define SCS_UART_RX_BUF  256
#define SCS_PACKET_TIMEOUT_MS 50

// Direction control GPIO (for RS485-style half-duplex)
// SCS bus uses a single wire for TX/RX, direction pin controls transceiver
// On M5Stack CoreS3, we can use RTS pin of UART for direction control
// If not using a transceiver, we manage via software GPIO
#define SCS_DIR_PIN_GPIO 15  // GPIO for direction control (if needed)

ServoControl::ServoControl()
    : m_uart_num(UART_NUM_1)
    , m_initialized(false)
{
    memset(m_servos, 0, sizeof(m_servos));
    m_servos[0].id = SERVO_PAN_ID;
    m_servos[1].id = SERVO_TILT_ID;
}

ServoControl::~ServoControl() {
    if (m_initialized) {
        uart_driver_delete(m_uart_num);
    }
}

bool ServoControl::begin(uart_port_t uart_num, int tx_pin, int rx_pin) {
    m_uart_num = uart_num;

    // Configure UART for SCS bus (1Mbps, 8N1, half-duplex)
    uart_config_t uart_config = {
        .baud_rate = SCS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(m_uart_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %d", err);
        return false;
    }

    err = uart_set_pin(m_uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %d", err);
        return false;
    }

    err = uart_driver_install(m_uart_num, SCS_UART_RX_BUF, SCS_UART_TX_BUF, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %d", err);
        return false;
    }

    m_initialized = true;

    // Set direction to receive initially
    set_direction_rx();

    // Ping both servos to verify connection
    bool pan_ok = ping(SERVO_PAN_ID);
    bool tilt_ok = ping(SERVO_TILT_ID);

    ESP_LOGI(TAG, "Servo init - Pan(ID=%d): %s, Tilt(ID=%d): %s",
             SERVO_PAN_ID, pan_ok ? "OK" : "FAIL",
             SERVO_TILT_ID, tilt_ok ? "OK" : "FAIL");

    if (!pan_ok && !tilt_ok) {
        ESP_LOGW(TAG, "Both servos not responding - check wiring");
    }

    // Enable torque on both
    set_torque(SERVO_PAN_ID, true);
    set_torque(SERVO_TILT_ID, true);

    return true;
}

void ServoControl::set_pan(float degrees) {
    if (degrees < 0.0f) degrees = 0.0f;
    if (degrees > 360.0f) degrees = 360.0f;
    int32_t pos = degrees_to_raw(degrees);
    set_position(SERVO_PAN_ID, pos);
}

void ServoControl::set_tilt(float degrees) {
    if (degrees < 5.0f) degrees = 5.0f;
    if (degrees > 85.0f) degrees = 85.0f;
    int32_t pos = degrees_to_raw(degrees);
    set_position(SERVO_TILT_ID, pos);
}

void ServoControl::set_position(uint8_t servo_id, int32_t position, uint16_t speed) {
    if (!m_initialized) return;

    // Clamp position
    if (position < SERVO_POSITION_MIN) position = SERVO_POSITION_MIN;
    if (position > SERVO_POSITION_MAX) position = SERVO_POSITION_MAX;

    uint8_t packet[32];
    uint8_t data[6];

    // Goal position (4 bytes: low, high, low, high for SCS protocol)
    data[0] = position & 0xFF;
    data[1] = (position >> 8) & 0xFF;
    // Goal speed (2 bytes)
    data[2] = speed & 0xFF;
    data[3] = (speed >> 8) & 0xFF;

    int len = build_write_packet(packet, servo_id, SCS_REG_GOAL_POSITION, data, 4);

    // Send, wait for response
    send_packet(packet, len);

    // Update state
    int idx = id_to_index(servo_id);
    if (idx >= 0) {
        m_servos[idx].target_position = position;
        m_servos[idx].moving = true;
    }

    vTaskDelay(pdMS_TO_TICKS(2)); // Small delay for bus settling
}

int32_t ServoControl::get_position(uint8_t servo_id) {
    if (!m_initialized) return -1;

    uint8_t packet[16];
    int len = build_read_packet(packet, servo_id, SCS_REG_PRESENT_POSITION, 2);

    send_packet(packet, len);

    uint8_t response[32];
    int rlen = read_packet(response, sizeof(response));

    if (rlen > 0 && response[0] == 0xFF && response[1] == 0xFF) {
        int data_len = response[3];
        if (data_len >= 2) {
            int32_t pos = response[5] | (response[6] << 8);
            int idx = id_to_index(servo_id);
            if (idx >= 0) {
                m_servos[idx].current_position = pos;
            }
            return pos;
        }
    }

    return -1;
}

void ServoControl::set_torque(uint8_t servo_id, bool enable) {
    if (!m_initialized) return;

    uint8_t packet[16];
    uint8_t data = enable ? 1 : 0;
    int len = build_write_packet(packet, servo_id, SCS_REG_TORQUE_ENABLE, &data, 1);

    send_packet(packet, len);

    int idx = id_to_index(servo_id);
    if (idx >= 0) {
        m_servos[idx].torque_enabled = enable;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
}

bool ServoControl::ping(uint8_t servo_id) {
    if (!m_initialized) return false;

    uint8_t packet[6];
    packet[0] = 0xFF;
    packet[1] = 0xFF;
    packet[2] = servo_id;
    packet[3] = 2; // length (excluding header)
    packet[4] = SCS_CMD_PING;
    packet[5] = calc_checksum(packet, 5);

    send_packet(packet, 6);

    uint8_t response[16];
    int rlen = read_packet(response, sizeof(response), 50);

    // Status packet: FF FF ID LEN ERROR ...
    if (rlen >= 4 && response[0] == 0xFF && response[1] == 0xFF) {
        uint8_t resp_id = response[2];
        uint8_t error = response[4];
        return (resp_id == servo_id && error == 0);
    }

    return false;
}

void ServoControl::read_state(uint8_t servo_id) {
    if (!m_initialized) return;

    // Read position, speed, load, voltage, temperature, moving (reg 56-66)
    uint8_t packet[16];
    int len = build_read_packet(packet, servo_id, SCS_REG_PRESENT_POSITION, 12);

    send_packet(packet, len);

    uint8_t response[32];
    int rlen = read_packet(response, sizeof(response));

    if (rlen >= 17 && response[0] == 0xFF && response[1] == 0xFF) {
        int data_len = response[3];
        if (data_len >= 12) {
            int idx = id_to_index(servo_id);
            if (idx >= 0) {
                m_servos[idx].current_position = response[5] | (response[6] << 8);
                // Speed (regs 60-61)
                m_servos[idx].speed = response[9] | (response[10] << 8);
                // Load (regs 62-63) - not fully implemented
                // Voltage (reg 62)
                m_servos[idx].voltage = response[11];
                // Temperature (reg 63)
                m_servos[idx].temperature = response[12];
                // Moving (reg 66)
                m_servos[idx].moving = response[15] != 0;
            }
        }
    }
}

servo_state_t ServoControl::get_state(uint8_t servo_id) const {
    servo_state_t empty = {0};
    int idx = id_to_index(servo_id);
    if (idx >= 0) return m_servos[idx];
    return empty;
}

void ServoControl::sync_write(int32_t pan_pos, int32_t tilt_pos, uint16_t speed) {
    if (!m_initialized) return;

    // Clamp both positions
    if (pan_pos < SERVO_POSITION_MIN) pan_pos = SERVO_POSITION_MIN;
    if (pan_pos > SERVO_POSITION_MAX) pan_pos = SERVO_POSITION_MAX;
    if (tilt_pos < SERVO_POSITION_MIN) tilt_pos = SERVO_POSITION_MIN;
    if (tilt_pos > SERVO_POSITION_MAX) tilt_pos = SERVO_POSITION_MAX;

    // SCS0009 supports sync write: FF FF FE LEN CMD reg_len reg_start ID1 data1 ID2 data2 ...
    // But simpler: just write each servo individually
    set_position(SERVO_PAN_ID, pan_pos, speed);
    set_position(SERVO_TILT_ID, tilt_pos, speed);
}

int32_t ServoControl::degrees_to_raw(float degrees) {
    float normalized = degrees / SERVO_DEGREES_MAX;
    return static_cast<int32_t>(normalized * (SERVO_POSITION_MAX - SERVO_POSITION_MIN)) + SERVO_POSITION_MIN;
}

float ServoControl::raw_to_degrees(int32_t raw) {
    float normalized = static_cast<float>(raw - SERVO_POSITION_MIN) /
                       static_cast<float>(SERVO_POSITION_MAX - SERVO_POSITION_MIN);
    return normalized * SERVO_DEGREES_MAX;
}

// ============================================================
// Private helper methods
// ============================================================

void ServoControl::set_direction_tx() {
    // For half-duplex UART, enable transmitter, disable receiver
    // Use GPIO to control direction if needed
    uart_set_mode(m_uart_num, UART_MODE_TX_ONLY);
    gpio_set_level((gpio_num_t)SCS_DIR_PIN_GPIO, 1);
}

void ServoControl::set_direction_rx() {
    uart_set_mode(m_uart_num, UART_MODE_RX_ONLY);
    gpio_set_level((gpio_num_t)SCS_DIR_PIN_GPIO, 0);
}

uint8_t ServoControl::calc_checksum(const uint8_t* packet, int len) {
    // SCS checksum: ~(ID + length + instruction + params...) & 0xFF
    uint8_t sum = 0;
    for (int i = 2; i < len; i++) { // skip FF FF header
        sum += packet[i];
    }
    return ~sum;
}

bool ServoControl::verify_checksum(const uint8_t* packet, int len) {
    if (len < 6) return false;
    uint8_t expected = calc_checksum(packet, len - 1);
    return expected == packet[len - 1];
}

void ServoControl::send_packet(const uint8_t* data, int len) {
    set_direction_tx();
    uart_write_bytes(m_uart_num, data, len);
    uart_wait_tx_done(m_uart_num, pdMS_TO_TICKS(10));
    set_direction_rx();
}

int ServoControl::read_packet(uint8_t* buffer, int max_len, int timeout_ms) {
    // Flush any stale data first
    uart_flush(m_uart_num);

    int total = 0;
    int timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    TickType_t start = xTaskGetTickCount();

    // Wait for header 0xFF 0xFF
    while (total < 2 && (xTaskGetTickCount() - start) < timeout_ticks) {
        int n = uart_read_bytes(m_uart_num, buffer + total, 2 - total, pdMS_TO_TICKS(5));
        total += n;
    }

    if (total < 2) return 0; // timeout

    // Read length byte
    uint8_t len_byte;
    int n = uart_read_bytes(m_uart_num, &len_byte, 1, pdMS_TO_TICKS(5));
    if (n != 1) return total;
    buffer[total++] = len_byte;

    // Read remaining: ID(1) + LEN(1 was already read) + ERROR(1) + PARAMS + CHECKSUM(1)
    int remaining = len_byte + 1; // includes ID, instruction/error, params, checksum
    if (remaining > max_len - total) remaining = max_len - total;

    while (total < (remaining + 3) && (xTaskGetTickCount() - start) < timeout_ticks) {
        n = uart_read_bytes(m_uart_num, buffer + total, remaining + 3 - total, pdMS_TO_TICKS(5));
        total += n;
    }

    return total;
}

int ServoControl::build_write_packet(uint8_t* buffer, uint8_t id, uint8_t reg, const uint8_t* data, int data_len) {
    int idx = 0;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    buffer[idx++] = id;
    buffer[idx++] = 4 + data_len; // length (instruction + reg + data + checksum)
    buffer[idx++] = SCS_CMD_WRITE;
    buffer[idx++] = reg;
    memcpy(buffer + idx, data, data_len);
    idx += data_len;
    buffer[idx] = calc_checksum(buffer, idx);
    idx++;
    return idx;
}

int ServoControl::build_read_packet(uint8_t* buffer, uint8_t id, uint8_t reg, uint8_t len) {
    int idx = 0;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    buffer[idx++] = id;
    buffer[idx++] = 4; // length (instruction + reg + read_len + checksum)
    buffer[idx++] = SCS_CMD_READ;
    buffer[idx++] = reg;
    buffer[idx++] = len;
    buffer[idx] = calc_checksum(buffer, idx);
    idx++;
    return idx;
}

int ServoControl::id_to_index(uint8_t id) const {
    if (id == SERVO_PAN_ID) return 0;
    if (id == SERVO_TILT_ID) return 1;
    return -1;
}
