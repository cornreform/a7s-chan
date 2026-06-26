#ifndef A7S_UDP_CLIENT_H
#define A7S_UDP_CLIENT_H

#include <cstdint>
#include "expressions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Network config
#define UDP_PORT            5000
#define UDP_RX_BUFFER_SIZE  2048
#define UDP_TX_BUFFER_SIZE  2048
#define WIFI_MAX_RETRIES    5

// Command types (18 types total)
typedef enum {
    CMD_NONE = 0,
    CMD_SET_EXPRESSION,       // Set face expression by preset name
    CMD_SET_EXPRESSION_RAW,   // Set face expression by raw parameters
    CMD_SET_SERVO,            // Set servo position
    CMD_SET_SERVO_SMOOTH,     // Set servo position (smooth)
    CMD_SEND_IR,              // Send IR NEC command
    CMD_SET_LEDS,             // Set LED colors
    CMD_SET_LED_PATTERN,      // Set LED pattern
    CMD_PLAY_TONE,            // Play a tone on speaker
    CMD_PLAY_AUDIO,           // Play audio data
    CMD_SET_VOLUME,           // Set speaker volume
    CMD_SET_BRIGHTNESS,       // Set display brightness
    CMD_GET_STATUS,           // Request status report
    CMD_REBOOT,               // Reboot the device
    CMD_OTA_UPDATE,           // Start OTA update
    CMD_SET_WIFI,             // Set WiFi credentials
    CMD_CALIBRATE,            // Start calibration
    CMD_PING,                 // Ping/pong keepalive
    CMD_UNKNOWN
} command_type_t;

// Command structure parsed from JSON
typedef struct {
    command_type_t type;
    char cmd_name[32];
    
    // Expression
    expression_t expression;
    char expression_name[32];
    
    // Servo
    int servo_index;
    float servo_angle;
    bool servo_smooth;
    
    // IR
    uint16_t ir_address;
    uint16_t ir_command;
    int ir_repeat;
    
    // LEDs
    struct {
        uint8_t r, g, b;
    } led_colors[12];
    int led_count;
    char led_pattern[16];
    float led_speed;
    
    // Audio
    int tone_freq;
    int tone_duration;
    int volume;
    
    // Other
    int brightness;
    char wifi_ssid[64];
    char wifi_pass[64];
    
    // Generic
    int seq_id;
} command_t;

// Status sent to server
typedef struct {
    float servo_angle[2];
    expression_name_t expression;
    int led_pattern;
    int wifi_rssi;
    uint32_t uptime_ms;
    uint32_t heap_free;
    bool mic_active;
    bool spkr_active;
    int seq_id;
} status_t;

// UDP client context
typedef struct {
    // Socket
    int sock;
    struct sockaddr_in server_addr;
    bool connected;
    
    // WiFi
    bool wifi_connected;
    char wifi_ssid[32];
    
    // Command queue
    QueueHandle_t cmd_queue;
    
    // IP of server (set after first received packet)
    uint32_t server_ip;
    uint16_t server_port;
    
    // Receive buffer
    uint8_t rx_buf[UDP_RX_BUFFER_SIZE];
    
    bool initialized;
} udp_client_t;

// Initialize UDP client (creates socket, starts WiFi)
int udp_client_init(udp_client_t* client, const char* ssid, const char* password);

// Start listening for UDP commands (creates receive task)
int udp_client_start(udp_client_t* client);

// Send status to the server that last sent us data
int udp_client_send_status(udp_client_t* client, const status_t* status);

// Send raw data to server
int udp_client_send(udp_client_t* client, const uint8_t* data, size_t len);

// Send audio data to server
int udp_client_send_audio(udp_client_t* client, const int16_t* audio, size_t num_samples);

// Pop a command from the queue (non-blocking)
// Returns true if a command was received
bool udp_client_receive(udp_client_t* client, command_t* cmd, TickType_t timeout_ticks);

// Parse a JSON command string into a command_t structure
// Returns CMD_NONE on parse failure
command_type_t udp_client_parse_json(const char* json_str, command_t* cmd);

// Convert command type to string
const char* udp_client_cmd_to_str(command_type_t type);

// Stop and cleanup
void udp_client_deinit(udp_client_t* client);

#ifdef __cplusplus
}
#endif

#endif // A7S_UDP_CLIENT_H
