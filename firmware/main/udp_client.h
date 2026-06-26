#pragma once

#include <cstdint>
#include <cstddef>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "expressions.h"

// UDP protocol ports
#define UDP_CMD_PORT        9002    // Incoming command port
#define UDP_STATUS_PORT     9000    // Status telemetry out
#define UDP_AUDIO_PORT      9001    // Audio streaming out

// Maximum UDP packet sizes
#define UDP_MAX_PACKET_SIZE   2048
#define UDP_CMD_BUFFER_SIZE   512

// Status message interval (ms)
#define STATUS_INTERVAL_MS   100

// A7S host configuration
#define A7S_HOST             "a7.local"   // mDNS hostname or IP
#define A7S_STATUS_PORT      9000
#define A7S_AUDIO_PORT       9001

// Command types
typedef enum {
    CMD_NONE = 0,
    CMD_HEAD,           // head: {"pan": 180, "tilt": 45, "speed": 1000}
    CMD_FACE,           // face: {"expression": "happy", "tween_ms": 300}
    CMD_LED,            // led: {"mode": "static", "r": 255, "g": 0, "b": 0, "leds": [0,1,2]}
    CMD_IR,             // ir: {"address": 0, "command": 0x45, "repeat": 0}
    CMD_SCREEN,         // screen: {"brightness": 200, "clear": true}
    CMD_EMOTE,          // emote: {"expression": "wave", "repeat": 1}
    CMD_SPEAK_START,    // speak_start: {"sample_rate": 16000}
    CMD_SPEAK_DATA,     // speak_data: base64 audio data
    CMD_SPEAK_STOP,     // speak_stop: {}
    CMD_CUSTOM,         // custom params: all face params
} cmd_type_t;

// Parsed command structure
typedef struct {
    cmd_type_t type;
    char raw_json[UDP_CMD_BUFFER_SIZE];

    // Parsed fields
    union {
        struct {        // CMD_HEAD
            float pan;
            float tilt;
            uint16_t speed;
            bool has_pan;
            bool has_tilt;
        } head;

        struct {        // CMD_FACE
            char expression[32];
            uint32_t tween_ms;
            bool has_tween;
        } face;

        struct {        // CMD_LED
            char mode[16];
            uint8_t r, g, b;
            uint8_t brightness;
            int leds[12];
            int led_count;
        } led;

        struct {        // CMD_IR
            uint16_t address;
            uint16_t command;
            int repeat;
        } ir;

        struct {        // CMD_SCREEN
            uint8_t brightness;
            bool clear;
        } screen;

        struct {        // CMD_EMOTE
            char expression[32];
            int repeat;
        } emote;

        struct {        // CMD_SPEAK_START
            int sample_rate;
        } speak_start;

        struct {        // CMD_CUSTOM - all expression params
            expression_params_t expr_params;
        } custom;

        char raw_text[512]; // Generic raw text for debug
    };

    int error_line;     // JSON parse error line (0 = no error)
} udp_command_t;

// Status data sent to A7S
typedef struct {
    float battery_voltage;
    float battery_percent;
    float imu_accel_x, imu_accel_y, imu_accel_z;
    float imu_gyro_x, imu_gyro_y, imu_gyro_z;
    float imu_temp;
    bool touch_touched;
    uint16_t touch_x, touch_y;
    uint32_t uptime_ms;
    int16_t servo_pan_pos;
    int16_t servo_tilt_pos;
    uint8_t servo_pan_temp;
    uint8_t servo_tilt_temp;
    char current_expression[32];
    int wifi_rssi;
    uint32_t free_heap;
    uint32_t free_psram;
} status_data_t;

// UDP command callback type
typedef void (*udp_cmd_callback_t)(const udp_command_t& cmd, void* user_arg);

class UDPClient {
public:
    UDPClient();
    ~UDPClient();

    // Initialize UDP sockets and start tasks
    bool begin();

    // Send status data to A7S
    void send_status(const status_data_t& status);

    // Send audio data to A7S
    void send_audio(const int16_t* data, size_t samples);

    // Send arbitrary data (JSON string) to status port
    void send_json(const char* json_str);

    // Register command callback
    void set_command_callback(udp_cmd_callback_t callback, void* user_arg);

    // Get next command from queue (non-blocking)
    bool get_command(udp_command_t* cmd);

    // Is connected / initialized
    bool is_connected() const { return m_initialized; }

    // Set A7S host address
    void set_a7s_host(const char* hostname);

private:
    bool m_initialized;
    char m_a7s_host[128];

    // Socket handles
    int m_cmd_sock;     // UDP listen socket for commands
    int m_status_sock;  // UDP send socket for status
    int m_audio_sock;   // UDP send socket for audio

    // A7S addresses (resolved once or periodically)
    struct sockaddr_in m_status_addr;
    struct sockaddr_in m_audio_addr;
    bool m_addr_resolved;

    // Command queue
    QueueHandle_t m_cmd_queue;

    // Command callback
    udp_cmd_callback_t m_cmd_callback;
    void* m_cmd_callback_arg;

    // Task handles
    TaskHandle_t m_recv_task;
    TaskHandle_t m_status_task;

    // Task functions (static)
    static void recv_task_func(void* arg);
    static void status_task_func(void* arg);

    // Internal helpers
    bool resolve_host();
    bool parse_json(const char* json, udp_command_t* cmd);
    bool parse_json_number(const char* json, const char* key, float* value);
    bool parse_json_int(const char* json, const char* key, int* value);
    bool parse_json_string(const char* json, const char* key, char* value, int max_len);
    bool parse_json_bool(const char* json, const char* key, bool* value);
    char* find_json_key(const char* json, const char* key);
};
