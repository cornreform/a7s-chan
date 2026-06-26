#include "udp_client.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"

static const char* TAG = "UDPClient";

// JSON parsing buffer
#define JSON_TOKEN_MAX 128

UDPClient::UDPClient()
    : m_initialized(false)
    , m_cmd_sock(-1)
    , m_status_sock(-1)
    , m_audio_sock(-1)
    , m_addr_resolved(false)
    , m_cmd_queue(nullptr)
    , m_cmd_callback(nullptr)
    , m_cmd_callback_arg(nullptr)
    , m_recv_task(nullptr)
    , m_status_task(nullptr)
{
    strncpy(m_a7s_host, "a7.local", sizeof(m_a7s_host) - 1);
    memset(&m_status_addr, 0, sizeof(m_status_addr));
    memset(&m_audio_addr, 0, sizeof(m_audio_addr));
}

UDPClient::~UDPClient() {
    m_initialized = false;

    if (m_recv_task) {
        vTaskDelete(m_recv_task);
        m_recv_task = nullptr;
    }

    if (m_status_task) {
        vTaskDelete(m_status_task);
        m_status_task = nullptr;
    }

    if (m_cmd_sock >= 0) {
        close(m_cmd_sock);
    }

    if (m_status_sock >= 0) {
        close(m_status_sock);
    }

    if (m_audio_sock >= 0) {
        close(m_audio_sock);
    }

    if (m_cmd_queue) {
        vQueueDelete(m_cmd_queue);
    }
}

bool UDPClient::begin() {
    // Create command queue
    m_cmd_queue = xQueueCreate(10, sizeof(udp_command_t));
    if (!m_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return false;
    }

    // Create command listening socket
    m_cmd_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_cmd_sock < 0) {
        ESP_LOGE(TAG, "Failed to create command socket: %d", errno);
        return false;
    }

    // Set socket timeout
    struct timeval tv = {
        .tv_sec = 1,
        .tv_usec = 0,
    };
    setsockopt(m_cmd_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Bind to command port
    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(UDP_CMD_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(m_cmd_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind command socket: %d", errno);
        close(m_cmd_sock);
        m_cmd_sock = -1;
        return false;
    }

    // Create status send socket
    m_status_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_status_sock < 0) {
        ESP_LOGE(TAG, "Failed to create status socket");
        close(m_cmd_sock);
        m_cmd_sock = -1;
        return false;
    }

    // Create audio send socket
    m_audio_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_audio_sock < 0) {
        ESP_LOGE(TAG, "Failed to create audio socket");
        close(m_cmd_sock);
        close(m_status_sock);
        m_cmd_sock = -1;
        m_status_sock = -1;
        return false;
    }

    // Resolve A7S host address
    resolve_host();

    // Start command receive task
    BaseType_t ret = xTaskCreatePinnedToCore(
        recv_task_func,
        "udp_recv",
        8192,
        this,
        5,
        &m_recv_task,
        0
    );

    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create recv task");
        return false;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "UDP client initialized - listening on port %d", UDP_CMD_PORT);
    return true;
}

void UDPClient::send_status(const status_data_t& status) {
    if (!m_initialized || !m_addr_resolved) {
        // Re-resolve
        if (!resolve_host()) return;
    }

    // Build JSON status string
    char json_buf[1024];
    int len = snprintf(json_buf, sizeof(json_buf),
        "{"
        "\"type\":\"status\","
        "\"bat_v\":%.2f,"
        "\"bat_pct\":%.1f,"
        "\"accel\":[%.2f,%.2f,%.2f],"
        "\"gyro\":[%.2f,%.2f,%.2f],"
        "\"temp\":%.1f,"
        "\"touch\":%s,"
        "\"touch_xy\":[%u,%u],"
        "\"uptime\":%lu,"
        "\"servo_pan\":%d,"
        "\"servo_tilt\":%d,"
        "\"pan_temp\":%u,"
        "\"tilt_temp\":%u,"
        "\"expression\":\"%s\","
        "\"rssi\":%d,"
        "\"heap\":%lu,"
        "\"psram\":%lu"
        "}",
        status.battery_voltage,
        status.battery_percent,
        status.imu_accel_x, status.imu_accel_y, status.imu_accel_z,
        status.imu_gyro_x, status.imu_gyro_y, status.imu_gyro_z,
        status.imu_temp,
        status.touch_touched ? "true" : "false",
        status.touch_x, status.touch_y,
        (unsigned long)status.uptime_ms,
        status.servo_pan_pos,
        status.servo_tilt_pos,
        status.servo_pan_temp,
        status.servo_tilt_temp,
        status.current_expression,
        status.wifi_rssi,
        (unsigned long)status.free_heap,
        (unsigned long)status.free_psram
    );

    sendto(m_status_sock, json_buf, len, 0,
           (struct sockaddr*)&m_status_addr, sizeof(m_status_addr));
}

void UDPClient::send_audio(const int16_t* data, size_t samples) {
    if (!m_initialized || !m_addr_resolved || !data || samples == 0) return;

    // Send raw PCM16 audio data
    // Format: 16-bit signed mono PCM
    size_t bytes = samples * sizeof(int16_t);
    sendto(m_audio_sock, data, bytes, 0,
           (struct sockaddr*)&m_audio_addr, sizeof(m_audio_addr));
}

void UDPClient::send_json(const char* json_str) {
    if (!m_initialized || !m_addr_resolved || !json_str) return;

    size_t len = strlen(json_str);
    sendto(m_status_sock, json_str, len, 0,
           (struct sockaddr*)&m_status_addr, sizeof(m_status_addr));
}

void UDPClient::set_command_callback(udp_cmd_callback_t callback, void* user_arg) {
    m_cmd_callback = callback;
    m_cmd_callback_arg = user_arg;
}

bool UDPClient::get_command(udp_command_t* cmd) {
    if (!m_cmd_queue) return false;
    return xQueueReceive(m_cmd_queue, cmd, 0) == pdTRUE;
}

void UDPClient::set_a7s_host(const char* hostname) {
    if (hostname) {
        strncpy(m_a7s_host, hostname, sizeof(m_a7s_host) - 1);
        m_a7s_host[sizeof(m_a7s_host) - 1] = '\0';
        m_addr_resolved = false;
        resolve_host();
    }
}

bool UDPClient::resolve_host() {
    struct addrinfo hints = {};
    struct addrinfo* result = nullptr;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", A7S_STATUS_PORT);

    int err = getaddrinfo(m_a7s_host, port_str, &hints, &result);
    if (err != 0 || !result) {
        ESP_LOGW(TAG, "Failed to resolve %s (will retry)", m_a7s_host);
        return false;
    }

    // Use first resolved address for status
    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
    memcpy(&m_status_addr, addr, sizeof(struct sockaddr_in));
    m_status_addr.sin_port = htons(A7S_STATUS_PORT);

    // Same address, different port for audio
    memcpy(&m_audio_addr, addr, sizeof(struct sockaddr_in));
    m_audio_addr.sin_port = htons(A7S_AUDIO_PORT);

    freeaddrinfo(result);

    m_addr_resolved = true;
    ESP_LOGI(TAG, "Resolved %s to %s:%d", m_a7s_host,
             inet_ntoa(m_status_addr.sin_addr), ntohs(m_status_addr.sin_port));
    return true;
}

// ============================================================
// Receive task
// ============================================================

void UDPClient::recv_task_func(void* arg) {
    UDPClient* self = static_cast<UDPClient*>(arg);

    while (self->m_initialized) {
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);
        char buffer[UDP_MAX_PACKET_SIZE];

        int n = recvfrom(self->m_cmd_sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr*)&sender_addr, &addr_len);

        if (n > 0) {
            buffer[n] = '\0';

            ESP_LOGI(TAG, "Received %d bytes from %s:%d: %s",
                     n, inet_ntoa(sender_addr.sin_addr),
                     ntohs(sender_addr.sin_port), buffer);

            // Parse command JSON
            udp_command_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            size_t copy_len = strlen(buffer);
            if (copy_len >= sizeof(cmd.raw_json)) copy_len = sizeof(cmd.raw_json) - 1;
            memcpy(cmd.raw_json, buffer, copy_len);
            cmd.raw_json[copy_len] = '\0';

            if (self->parse_json(buffer, &cmd)) {
                if (self->m_cmd_callback) {
                    self->m_cmd_callback(cmd, self->m_cmd_callback_arg);
                }

                // Also enqueue for main loop polling
                if (self->m_cmd_queue) {
                    xQueueSend(self->m_cmd_queue, &cmd, 0);
                }
            }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGW(TAG, "Socket error: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // Yield to other tasks
        taskYIELD();
    }

    vTaskDelete(NULL);
}

// ============================================================
// JSON parsing
// ============================================================

bool UDPClient::parse_json(const char* json, udp_command_t* cmd) {
    if (!json || !cmd) return false;

    // Detect command type from top-level key
    cmd->type = CMD_NONE;

    // Simple linear scan for known keys
    if (strstr(json, "\"head\"")) {
        cmd->type = CMD_HEAD;
        parse_json_number(json, "\"pan\"", &cmd->head.pan);
        parse_json_number(json, "\"tilt\"", &cmd->head.tilt);
        parse_json_int(json, "\"speed\"", (int*)&cmd->head.speed);
        cmd->head.has_pan = true;
        cmd->head.has_tilt = true;
        return true;
    }

    if (strstr(json, "\"face\"")) {
        cmd->type = CMD_FACE;
        parse_json_string(json, "\"expression\"", cmd->face.expression, sizeof(cmd->face.expression));
        int tween = 0;
        if (parse_json_int(json, "\"tween_ms\"", &tween)) {
            cmd->face.tween_ms = tween;
            cmd->face.has_tween = true;
        }
        return true;
    }

    if (strstr(json, "\"led\"")) {
        cmd->type = CMD_LED;
        parse_json_string(json, "\"mode\"", cmd->led.mode, sizeof(cmd->led.mode));
        int val = 0;
        if (parse_json_int(json, "\"r\"", &val)) cmd->led.r = val;
        if (parse_json_int(json, "\"g\"", &val)) cmd->led.g = val;
        if (parse_json_int(json, "\"b\"", &val)) cmd->led.b = val;
        if (parse_json_int(json, "\"brightness\"", &val)) cmd->led.brightness = val;
        return true;
    }

    if (strstr(json, "\"ir\"")) {
        cmd->type = CMD_IR;
        int val = 0;
        if (parse_json_int(json, "\"address\"", &val)) cmd->ir.address = val;
        if (parse_json_int(json, "\"command\"", &val)) cmd->ir.command = val;
        parse_json_int(json, "\"repeat\"", &cmd->ir.repeat);
        return true;
    }

    if (strstr(json, "\"screen\"")) {
        cmd->type = CMD_SCREEN;
        int val = 0;
        if (parse_json_int(json, "\"brightness\"", &val)) cmd->screen.brightness = val;
        parse_json_bool(json, "\"clear\"", &cmd->screen.clear);
        return true;
    }

    if (strstr(json, "\"emote\"")) {
        cmd->type = CMD_EMOTE;
        parse_json_string(json, "\"expression\"", cmd->emote.expression, sizeof(cmd->emote.expression));
        parse_json_int(json, "\"repeat\"", &cmd->emote.repeat);
        return true;
    }

    if (strstr(json, "\"speak_start\"")) {
        cmd->type = CMD_SPEAK_START;
        parse_json_int(json, "\"sample_rate\"", &cmd->speak_start.sample_rate);
        return true;
    }

    if (strstr(json, "\"speak_data\"")) {
        cmd->type = CMD_SPEAK_DATA;
        return true;
    }

    if (strstr(json, "\"speak_stop\"")) {
        cmd->type = CMD_SPEAK_STOP;
        return true;
    }

    // Fallback: check for bare command type key
    if (strstr(json, "\"type\"")) {
        char type_str[32];
        if (parse_json_string(json, "\"type\"", type_str, sizeof(type_str))) {
            if (strcmp(type_str, "head") == 0) {
                cmd->type = CMD_HEAD;
                parse_json_number(json, "\"pan\"", &cmd->head.pan);
                parse_json_number(json, "\"tilt\"", &cmd->head.tilt);
                return true;
            }
            if (strcmp(type_str, "face") == 0) {
                cmd->type = CMD_FACE;
                parse_json_string(json, "\"expression\"", cmd->face.expression, sizeof(cmd->face.expression));
                return true;
            }
            if (strcmp(type_str, "led") == 0) {
                cmd->type = CMD_LED;
                return true;
            }
            if (strcmp(type_str, "ir") == 0) {
                cmd->type = CMD_IR;
                return true;
            }
            if (strcmp(type_str, "screen") == 0) {
                cmd->type = CMD_SCREEN;
                return true;
            }
            if (strcmp(type_str, "emote") == 0) {
                cmd->type = CMD_EMOTE;
                return true;
            }
            if (strcmp(type_str, "speak_start") == 0) {
                cmd->type = CMD_SPEAK_START;
                return true;
            }
            if (strcmp(type_str, "speak_data") == 0) {
                cmd->type = CMD_SPEAK_DATA;
                return true;
            }
            if (strcmp(type_str, "speak_stop") == 0) {
                cmd->type = CMD_SPEAK_STOP;
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "Unknown command type: %s", json);
    return false;
}

char* UDPClient::find_json_key(const char* json, const char* key) {
    if (!json || !key) return nullptr;

    const char* found = strstr(json, key);
    if (!found) return nullptr;

    // Skip past the key and colon
    found += strlen(key);
    while (*found && (*found == ' ' || *found == ':' || *found == '\t')) {
        found++;
    }

    return (char*)found;
}

bool UDPClient::parse_json_number(const char* json, const char* key, float* value) {
    char* val_start = find_json_key(json, key);
    if (!val_start) return false;

    char* end = nullptr;
    float v = strtof(val_start, &end);
    if (end == val_start) return false;

    *value = v;
    return true;
}

bool UDPClient::parse_json_int(const char* json, const char* key, int* value) {
    char* val_start = find_json_key(json, key);
    if (!val_start) return false;

    char* end = nullptr;
    long v = strtol(val_start, &end, 0); // base 0 for hex support
    if (end == val_start) return false;

    *value = static_cast<int>(v);
    return true;
}

bool UDPClient::parse_json_string(const char* json, const char* key, char* value, int max_len) {
    char* val_start = find_json_key(json, key);
    if (!val_start) return false;

    // Expect opening quote
    if (*val_start != '"') return false;
    val_start++;

    // Find closing quote
    const char* val_end = strchr(val_start, '"');
    if (!val_end) return false;

    int len = val_end - val_start;
    if (len >= max_len) len = max_len - 1;

    strncpy(value, val_start, len);
    value[len] = '\0';
    return true;
}

bool UDPClient::parse_json_bool(const char* json, const char* key, bool* value) {
    char* val_start = find_json_key(json, key);
    if (!val_start) return false;

    if (strncmp(val_start, "true", 4) == 0) {
        *value = true;
        return true;
    }
    if (strncmp(val_start, "false", 5) == 0) {
        *value = false;
        return true;
    }

    return false;
}
