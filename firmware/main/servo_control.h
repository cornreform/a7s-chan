#pragma once

#include <cstdint>
#include "driver/uart.h"

// ServoControl - Controls 2x SCS0009 feedback servos via UART
// SCS0009 is a serial bus servo (half-duplex UART, 1Mbps, 3.3V logic)
// Pan servo: 0-360 degrees continuous
// Tilt servo: 5-85 degrees (mechanical limit)

// SCS0009 protocol constants
#define SCS_BROADCAST_ID   0xFE
#define SCS_SYNC_WRITE     0xFF  // not standard but used for sync writes
#define SCS_CMD_WRITE      0x03
#define SCS_CMD_READ       0x02
#define SCS_CMD_PING       0x01
#define SCS_CMD_RESET      0x06
#define SCS_CMD_MOVE_WRITE 0xFD  // custom write command

// Register addresses for SCS0009
#define SCS_REG_ID                5
#define SCS_REG_BAUD_RATE         6
#define SCS_REG_TORQUE_ENABLE     40
#define SCS_REG_GOAL_POSITION     42
#define SCS_REG_GOAL_SPEED        46
#define SCS_REG_PRESENT_POSITION  56
#define SCS_REG_PRESENT_SPEED     60
#define SCS_REG_PRESENT_VOLTAGE   62
#define SCS_REG_PRESENT_TEMPERATURE 63
#define SCS_REG_MOVING            66
#define SCS_REG_PUNCH             48
#define SCS_REG_CW_ANGLE_LIMIT    6
#define SCS_REG_CCW_ANGLE_LIMIT   8
#define SCS_REG_MAX_TORQUE        34

// Servo IDs
#define SERVO_PAN_ID      1
#define SERVO_TILT_ID     2

// Servo range mapping
// SCS0009: 0-4095 for 0-360 degrees (12-bit position)
#define SERVO_POSITION_MIN    0
#define SERVO_POSITION_MAX    4095
#define SERVO_DEGREES_MAX     360.0f
#define SERVO_SPEED_DEFAULT   1000   // ~50 rpm at 12V

typedef struct {
    uint8_t id;
    int32_t current_position;    // last read position (0-4095)
    int32_t target_position;     // target position
    uint16_t speed;              // movement speed
    uint16_t load;               // current load
    uint16_t voltage;            // mV * 10
    uint8_t temperature;         // Celsius
    bool moving;
    bool torque_enabled;
} servo_state_t;

class ServoControl {
public:
    ServoControl();
    ~ServoControl();

    // Initialize UART for servo bus
    bool begin(uart_port_t uart_num = UART_NUM_1, int tx_pin = 6, int rx_pin = 7);

    // Set pan angle (0-360 degrees)
    void set_pan(float degrees);

    // Set tilt angle (5-85 degrees)
    void set_tilt(float degrees);

    // Set position directly (0-4095)
    void set_position(uint8_t servo_id, int32_t position, uint16_t speed = SERVO_SPEED_DEFAULT);

    // Get current position (0-4095)
    int32_t get_position(uint8_t servo_id);

    // Enable/disable torque
    void set_torque(uint8_t servo_id, bool enable);

    // Ping servo to check if alive
    bool ping(uint8_t servo_id);

    // Read all servo state
    void read_state(uint8_t servo_id);
    servo_state_t get_state(uint8_t servo_id) const;

    // Send both servos positions atomically (sync write)
    void sync_write(int32_t pan_pos, int32_t tilt_pos, uint16_t speed = SERVO_SPEED_DEFAULT);

    // Convert degrees to raw position value
    static int32_t degrees_to_raw(float degrees);
    static float raw_to_degrees(int32_t raw);

private:
    uart_port_t m_uart_num;
    bool m_initialized;
    servo_state_t m_servos[2]; // index 0 = pan (ID 1), index 1 = tilt (ID 2)

    // UART direction control for half-duplex
    void set_direction_tx();
    void set_direction_rx();

    // Calculate and verify checksum
    uint8_t calc_checksum(const uint8_t* packet, int len);
    bool verify_checksum(const uint8_t* packet, int len);

    // Send raw packet over UART
    void send_packet(const uint8_t* data, int len);

    // Read response packet
    int read_packet(uint8_t* buffer, int max_len, int timeout_ms = 100);

    // Build a SCS protocol write command
    int build_write_packet(uint8_t* buffer, uint8_t id, uint8_t reg, const uint8_t* data, int data_len);

    // Build a SCS protocol read command
    int build_read_packet(uint8_t* buffer, uint8_t id, uint8_t reg, uint8_t len);

    // Get servo index from ID
    int id_to_index(uint8_t id) const;
};
