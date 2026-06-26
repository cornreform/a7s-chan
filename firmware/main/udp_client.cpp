#include "udp_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static const char* TAG = "udp";

// Simple JSON parser (no external dependency)
// Supports: {"key": value, "key2": "string"} structures
// Returns NULL on failure

static const char* json_skip_whitespace(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char* json_find_key(const char* json, const char* key) {
    if (!json || !key) return NULL;
    
    const char* p = json;
    int key_len = strlen(key);
    
    while (*p) {
        p = strchr(p, '"');
        if (!p) return NULL;
        p++; // Skip opening quote
        
        // Check if this key matches
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '"') {
            p += key_len + 1; // Skip key and closing quote
            p = json_skip_whitespace(p);
            if (*p == ':') {
                p++;
                return json_skip_whitespace(p);
            }
        }
        
        // Skip to next quote
        p = strchr(p, '"');
        if (!p) return NULL;
        p++;
    }
    return NULL;
}

static const char* json_extract_string(const char* p, char* out, int max_len) {
    if (!p || *p != '"') return NULL;
    p++; // Skip opening quote
    
    int i = 0;
    while (*p && *p != '"' && i < max_len - 1) {
        if (*p == '\\') {
            p++;
            if (*p == '"') { out[i++] = '"'; p++; continue; }
            if (*p == 'n') { out[i++] = '\n'; p++; continue; }
            if (*p == '\\') { out[i++] = '\\'; p++; continue; }
        }
        out[i++] = *p;
        p++;
    }
    out[i] = '\0';
    
    if (*p == '"') p++;
    return p;
}

static float json_extract_number(const char* p, bool* success) {
    if (success) *success = false;
    if (!p) return 0.0f;
    
    char* end = NULL;
    float val = strtof(p, &end);
    if (end != p) {
        if (success) *success = true;
    }
    return val;
}

static int json_extract_int(const char* p, bool* success) {
    return (int)json_extract_number(p, success);
}

static bool json_extract_bool(const char* p, bool* success) {
    if (success) *success = false;
    if (!p) return false;
    
    if (strncmp(p, "true", 4) == 0) {
        if (success) *success = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        if (success) *success = true;
        return false;
    }
    return false;
}

// Command name lookup table
typedef struct {
    const char* name;
    command_type_t type;
} cmd_lookup_t;

static const cmd_lookup_t s_cmd_lookup[] = {
    {"set_expression",      CMD_SET_EXPRESSION},
    {"set_expression_raw",  CMD_SET_EXPRESSION_RAW},
    {"set_servo",           CMD_SET_SERVO},
    {"set_servo_smooth",    CMD_SET_SERVO_SMOOTH},
    {"send_ir",             CMD_SEND_IR},
    {"set_leds",            CMD_SET_LEDS},
    {"set_led_pattern",     CMD_SET_LED_PATTERN},
    {"play_tone",           CMD_PLAY_TONE},
    {"play_audio",          CMD_PLAY_AUDIO},
    {"set_volume",          CMD_SET_VOLUME},
    {"set_brightness",      CMD_SET_BRIGHTNESS},
    {"get_status",          CMD_GET_STATUS},
    {"reboot",              CMD_REBOOT},
    {"ota_update",          CMD_OTA_UPDATE},
    {"set_wifi",            CMD_SET_WIFI},
    {"calibrate",           CMD_CALIBRATE},
    {"ping",                CMD_PING},
    {NULL,                  CMD_NONE}
};

static command_type_t lookup_command_type(const char* name) {
    for (int i = 0; s_cmd_lookup[i].name != NULL; i++) {
        if (strcmp(name, s_cmd_lookup[i].name) == 0) {
            return s_cmd_lookup[i].type;
        }
    }
    return CMD_UNKNOWN;
}

const char* udp_client_cmd_to_str(command_type_t type) {
    for (int i = 0; s_cmd_lookup[i].name != NULL; i++) {
        if (s_cmd_lookup[i].type == type) {
            return s_cmd_lookup[i].name;
        }
    }
    return "unknown";
}

command_type_t udp_client_parse_json(const char* json_str, command_t* cmd) {
    if (!json_str || !cmd) return CMD_NONE;
    
    memset(cmd, 0, sizeof(command_t));
    
    // Extract command type
    const char* val = json_find_key(json_str, "cmd");
    if (!val) {
        val = json_find_key(json_str, "command");
    }
    if (!val) return CMD_NONE;
    
    char cmd_name[32];
    const char* end = json_extract_string(val, cmd_name, sizeof(cmd_name));
    if (!end) return CMD_NONE;
    
    cmd->type = lookup_command_type(cmd_name);
    strncpy(cmd->cmd_name, cmd_name, sizeof(cmd->cmd_name) - 1);
    
    if (cmd->type == CMD_NONE) return CMD_NONE;
    
    // Extract sequence ID
    bool success;
    val = json_find_key(json_str, "seq");
    if (val) {
        cmd->seq_id = json_extract_int(val, &success);
    }
    val = json_find_key(json_str, "id");
    if (val && !cmd->seq_id) {
        cmd->seq_id = json_extract_int(val, &success);
    }
    
    // Parse command-specific parameters
    switch (cmd->type) {
        case CMD_SET_EXPRESSION: {
            val = json_find_key(json_str, "expression");
            if (val) {
                json_extract_string(val, cmd->expression_name, sizeof(cmd->expression_name));
            }
            break;
        }
        
        case CMD_SET_EXPRESSION_RAW: {
            #define GET_FLOAT(key, field) do { \
                val = json_find_key(json_str, key); \
                if (val) cmd->expression.field = json_extract_number(val, &success); \
            } while(0)
            
            GET_FLOAT("eye_open", eye_open);
            GET_FLOAT("eye_height", eye_height);
            GET_FLOAT("eye_width", eye_width);
            GET_FLOAT("pupil_x", pupil_x);
            GET_FLOAT("pupil_y", pupil_y);
            GET_FLOAT("brow_angle", brow_angle);
            GET_FLOAT("brow_height", brow_height);
            GET_FLOAT("mouth_open", mouth_open);
            GET_FLOAT("mouth_curve", mouth_curve);
            GET_FLOAT("blush", blush);
            GET_FLOAT("tear", tear);
            GET_FLOAT("heart_eyes", heart_eyes);
            GET_FLOAT("exclamation", exclamation);
            
            #undef GET_FLOAT
            break;
        }
        
        case CMD_SET_SERVO:
        case CMD_SET_SERVO_SMOOTH: {
            val = json_find_key(json_str, "index");
            if (val) cmd->servo_index = json_extract_int(val, &success);
            
            val = json_find_key(json_str, "angle");
            if (val) cmd->servo_angle = json_extract_number(val, &success);
            
            cmd->servo_smooth = (cmd->type == CMD_SET_SERVO_SMOOTH);
            break;
        }
        
        case CMD_SEND_IR: {
            val = json_find_key(json_str, "address");
            if (val) cmd->ir_address = (uint16_t)json_extract_int(val, &success);
            
            val = json_find_key(json_str, "command");
            if (val) cmd->ir_command = (uint16_t)json_extract_int(val, &success);
            
            val = json_find_key(json_str, "repeat");
            if (val) cmd->ir_repeat = json_extract_int(val, &success);
            break;
        }
        
        case CMD_SET_LEDS: {
            // Parse LED array: "leds": [[r,g,b], ...]
            val = json_find_key(json_str, "leds");
            if (val) {
                // Parse the array manually
                // Format: [[r,g,b],[r,g,b],...] or just count + one color
                if (*val == '[') {
                    val++;
                    int led_idx = 0;
                    while (*val && *val != ']' && led_idx < 12) {
                        val = json_skip_whitespace(val);
                        if (*val == '[') {
                            val++;
                            int rgb[3] = {0, 0, 0};
                            for (int c = 0; c < 3; c++) {
                                val = json_skip_whitespace(val);
                                float num = strtof(val, (char**)&val);
                                rgb[c] = (int)num;
                                val = json_skip_whitespace(val);
                                if (*val == ',') val++;
                            }
                            cmd->led_colors[led_idx].r = (uint8_t)rgb[0];
                            cmd->led_colors[led_idx].g = (uint8_t)rgb[1];
                            cmd->led_colors[led_idx].b = (uint8_t)rgb[2];
                            led_idx++;
                            val = json_skip_whitespace(val);
                            if (*val == ']') val++;
                            val = json_skip_whitespace(val);
                            if (*val == ',') val++;
                        }
                    }
                    cmd->led_count = led_idx;
                }
            }
            
            // Also check for single r, g, b fields
            val = json_find_key(json_str, "r");
            if (val && cmd->led_count == 0) {
                uint8_t r = (uint8_t)json_extract_int(val, &success);
                val = json_find_key(json_str, "g");
                uint8_t g = val ? (uint8_t)json_extract_int(val, &success) : 0;
                val = json_find_key(json_str, "b");
                uint8_t b = val ? (uint8_t)json_extract_int(val, &success) : 0;
                for (int i = 0; i < 12; i++) {
                    cmd->led_colors[i].r = r;
                    cmd->led_colors[i].g = g;
                    cmd->led_colors[i].b = b;
                }
                cmd->led_count = 12;
            }
            break;
        }
        
        case CMD_SET_LED_PATTERN: {
            val = json_find_key(json_str, "pattern");
            if (val) {
                json_extract_string(val, cmd->led_pattern, sizeof(cmd->led_pattern));
            }
            
            // Extract color
            val = json_find_key(json_str, "r");
            uint8_t r = val ? (uint8_t)json_extract_int(val, &success) : 255;
            val = json_find_key(json_str, "g");
            uint8_t g = val ? (uint8_t)json_extract_int(val, &success) : 255;
            val = json_find_key(json_str, "b");
            uint8_t b = val ? (uint8_t)json_extract_int(val, &success) : 255;
            
            val = json_find_key(json_str, "speed");
            cmd->led_speed = val ? json_extract_number(val, &success) : 0.5f;
            
            for (int i = 0; i < 12; i++) {
                cmd->led_colors[i].r = r;
                cmd->led_colors[i].g = g;
                cmd->led_colors[i].b = b;
            }
            cmd->led_count = 12;
            break;
        }
        
        case CMD_PLAY_TONE: {
            val = json_find_key(json_str, "freq");
            if (val) cmd->tone_freq = json_extract_int(val, &success);
            
            val = json_find_key(json_str, "duration");
            if (val) cmd->tone_duration = json_extract_int(val, &success);
            break;
        }
        
        case CMD_SET_VOLUME: {
            val = json_find_key(json_str, "volume");
            if (val) cmd->volume = json_extract_int(val, &success);
            break;
        }
        
        case CMD_SET_BRIGHTNESS: {
            val = json_find_key(json_str, "brightness");
            if (val) cmd->brightness = json_extract_int(val, &success);
            break;
        }
        
        case CMD_SET_WIFI: {
            val = json_find_key(json_str, "ssid");
            if (val) json_extract_string(val, cmd->wifi_ssid, sizeof(cmd->wifi_ssid));
            
            val = json_find_key(json_str, "password");
            if (val) json_extract_string(val, cmd->wifi_pass, sizeof(cmd->wifi_pass));
            break;
        }
        
        default:
            break;
    }
    
    return cmd->type;
}

// WiFi event handler
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT     = BIT1;

static void event_handler(void* arg, esp_event_base_t event_base,
                           int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_event_group) {
            // Don't set fail immediately, let retry logic handle it
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

int udp_client_init(udp_client_t* client, const char* ssid, const char* password) {
    memset(client, 0, sizeof(udp_client_t));
    client->sock = -1;
    client->server_port = UDP_PORT;
    client->cmd_queue = xQueueCreate(10, sizeof(command_t));
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Initialize network interface
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         &event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         &event_handler, NULL, &instance_got_ip);
    
    // Configure WiFi
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    
    // Wait for connection
    s_wifi_event_group = xEventGroupCreate();
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected to %s", ssid);
        client->wifi_connected = true;
        strncpy(client->wifi_ssid, ssid, sizeof(client->wifi_ssid) - 1);
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        client->wifi_connected = false;
    }
    
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
    
    // Create UDP socket
    client->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client->sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return -1;
    }
    
    // Set socket timeout
    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 100000  // 100ms timeout for recv
    };
    setsockopt(client->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Bind to listen on UDP port
    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(UDP_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(client->sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        close(client->sock);
        client->sock = -1;
        return -1;
    }
    
    client->initialized = true;
    ESP_LOGI(TAG, "UDP client initialized on port %d", UDP_PORT);
    return 0;
}

// UDP receive task
static void udp_recv_task(void* arg) {
    udp_client_t* client = (udp_client_t*)arg;
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);
    
    ESP_LOGI(TAG, "UDP receive task started");
    
    while (true) {
        if (client->sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        int len = recvfrom(client->sock, client->rx_buf, UDP_RX_BUFFER_SIZE - 1, 0,
                           (struct sockaddr*)&source_addr, &addr_len);
        
        if (len > 0) {
            client->rx_buf[len] = '\0';
            
            // Store the source address for reply
            client->server_ip = source_addr.sin_addr.s_addr;
            client->server_port = ntohs(source_addr.sin_port);
            
            ESP_LOGD(TAG, "Received %d bytes from " IPSTR ":%d: %s",
                     len, IP2STR(&source_addr.sin_addr),
                     ntohs(source_addr.sin_port), (char*)client->rx_buf);
            
            // Parse JSON command
            command_t cmd;
            command_type_t type = udp_client_parse_json((const char*)client->rx_buf, &cmd);
            
            if (type != CMD_NONE) {
                // Queue for main loop
                if (xQueueSend(client->cmd_queue, &cmd, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Command queue full, dropping command");
                }
            }
        }
    }
}

int udp_client_start(udp_client_t* client) {
    if (!client->initialized) return -1;
    
    // Create receive task on core 1
    TaskHandle_t task_handle;
    xTaskCreatePinnedToCore(udp_recv_task, "udp_recv", 4096, client, 5, &task_handle, 1);
    
    return 0;
}

int udp_client_send_status(udp_client_t* client, const status_t* status) {
    if (client->sock < 0 || !client->server_ip) return -1;
    
    char json_buf[UDP_TX_BUFFER_SIZE];
    int n = snprintf(json_buf, sizeof(json_buf),
        "{"
        "\"type\":\"status\","
        "\"seq\":%d,"
        "\"servo0\":%.1f,"
        "\"servo1\":%.1f,"
        "\"expr\":%d,"
        "\"led_pattern\":%d,"
        "\"rssi\":%d,"
        "\"uptime\":%lu,"
        "\"heap\":%lu,"
        "\"mic\":%d,"
        "\"spkr\":%d"
        "}",
        status->seq_id,
        status->servo_angle[0],
        status->servo_angle[1],
        (int)status->expression,
        status->led_pattern,
        status->wifi_rssi,
        (unsigned long)status->uptime_ms,
        (unsigned long)status->heap_free,
        status->mic_active ? 1 : 0,
        status->spkr_active ? 1 : 0
    );
    
    if (n <= 0) return -1;
    
    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client->server_port);
    dest_addr.sin_addr.s_addr = client->server_ip;
    
    int ret = sendto(client->sock, json_buf, n, 0,
                     (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (ret < 0) {
        ESP_LOGW(TAG, "sendto failed: %d", ret);
    }
    
    return ret;
}

int udp_client_send(udp_client_t* client, const uint8_t* data, size_t len) {
    if (client->sock < 0 || !client->server_ip) return -1;
    
    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client->server_port);
    dest_addr.sin_addr.s_addr = client->server_ip;
    
    return sendto(client->sock, data, len, 0,
                  (struct sockaddr*)&dest_addr, sizeof(dest_addr));
}

int udp_client_send_audio(udp_client_t* client, const int16_t* audio, size_t num_samples) {
    if (client->sock < 0 || !client->server_ip) return -1;
    
    // Create JSON wrapper for audio data (base64 not implemented - send as binary with header)
    // For now, send as raw PCM data with a small text header
    // Format: {"type":"audio","len":N}\n followed by raw PCM data
    
    char header[64];
    int header_len = snprintf(header, sizeof(header),
                              "{\"type\":\"audio\",\"len\":%u}\n", (unsigned)num_samples);
    
    // Send header first
    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(client->server_port);
    dest_addr.sin_addr.s_addr = client->server_ip;
    
    sendto(client->sock, header, header_len, 0,
           (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    
    // Send PCM data
    return sendto(client->sock, audio, num_samples * sizeof(int16_t), 0,
                  (struct sockaddr*)&dest_addr, sizeof(dest_addr));
}

bool udp_client_receive(udp_client_t* client, command_t* cmd, TickType_t timeout_ticks) {
    if (!client->cmd_queue) return false;
    return xQueueReceive(client->cmd_queue, cmd, timeout_ticks) == pdTRUE;
}

void udp_client_deinit(udp_client_t* client) {
    if (client->sock >= 0) {
        close(client->sock);
        client->sock = -1;
    }
    
    if (client->cmd_queue) {
        vQueueDelete(client->cmd_queue);
        client->cmd_queue = NULL;
    }
    
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_event_loop_delete_default();
    
    client->initialized = false;
    ESP_LOGI(TAG, "UDP client deinitialized");
}
