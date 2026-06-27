#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/gpio.h"

#include "udp_client.h"
#include "face_renderer.h"
#include "servo_control.h"
#include "ir_control.h"
#include "led_control.h"
#include "audio_pipeline.h"
#include "expressions.h"

static const char* TAG = "StackChan";

// WiFi configuration (hardcoded)
#define WIFI_SSID           "SC"
#define WIFI_PASS           "8447Ce8086"

// Additional WiFi networks (will try in order)
#define WIFI_SSID2          "NextE"
#define WIFI_PASS2          "NXE5w526"
#define WIFI_MAX_RETRY      5

// Free heap threshold warning
#define HEAP_WARN_THRESHOLD 32768

// CoreS3 GPIO definitions
#define GPIO_BUTTON_A       41  // Left button
#define GPIO_BUTTON_B       47  // Middle button (usually reset on CoreS3)
#define GPIO_BUTTON_C       48  // Right button
#define GPIO_PWR_EN         3   // Power enable (for peripherals)
#define GPIO_BAT_ADC        7   // Battery voltage ADC

// Battery measurement
#define BAT_ADC_CHANNEL     ADC1_CHANNEL_7   // GPIO7
#define BAT_ADC_ATTEN       ADC_ATTEN_DB_12
#define BAT_FULL_MV         4200
#define BAT_EMPTY_MV        3200

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
static void wifi_init_sta(void);
static void command_handler(const udp_command_t& cmd, void* user_arg);
static void buttons_init(void);
static int read_battery_mv(void);
static void read_imu_data(status_data_t* status);
static void read_touch_data(status_data_t* status);

// Global objects
FaceRenderer   g_face_renderer;
ServoControl   g_servo_control;
IRControl      g_ir_control;
LEDControl     g_led_control;
AudioPipeline  g_audio_pipeline;
UDPClient      g_udp_client;

// System state
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static bool s_wifi_connected = false;
static expression_id_t s_current_expr = EXPR_IDLE;
static uint32_t s_uptime_ms = 0;

// WiFi event bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Stack-chan Firmware v1.0 - M5Stack CoreS3");
    ESP_LOGI(TAG, "ESP-IDF v5.5, Chip: %s", CONFIG_IDF_TARGET);

    // Enable peripheral power (GPIO 3 = PWR_EN on CoreS3)
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize peripherals
    buttons_init();

    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    if (!g_face_renderer.begin()) {
        ESP_LOGE(TAG, "Display init failed!");
    } else {
        g_face_renderer.set_expression(EXPR_IDLE);
        ESP_LOGI(TAG, "Display ready - idle face shown");
    }

    // Initialize servos
    ESP_LOGI(TAG, "Initializing servos...");
    if (!g_servo_control.begin()) {
        ESP_LOGW(TAG, "Servo init failed - check wiring");
    }

    // Initialize IR blaster
    ESP_LOGI(TAG, "Initializing IR blaster...");
    if (!g_ir_control.begin()) {
        ESP_LOGW(TAG, "IR blaster init failed");
    }

    // Initialize RGB LEDs
    ESP_LOGI(TAG, "Initializing RGB LEDs...");
    if (!g_led_control.begin()) {
        ESP_LOGW(TAG, "LED init failed");
    } else {
        // Startup animation: brief green flash
        g_led_control.set_all(0, 50, 0);
        g_led_control.show();
        vTaskDelay(pdMS_TO_TICKS(200));
        g_led_control.clear();
    }

    // Initialize audio pipeline
    ESP_LOGI(TAG, "Initializing audio...");
    if (!g_audio_pipeline.begin()) {
        ESP_LOGW(TAG, "Audio init failed");
    }

    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_init_sta();

    // WiFi connection result (multi-SSID handled in wifi_init_sta)
    if (s_wifi_connected) {
        ESP_LOGI(TAG, "WiFi connected! Starting UDP server...");
        if (!g_udp_client.begin()) {
            ESP_LOGE(TAG, "UDP client init failed!");
        } else {
            g_udp_client.set_command_callback(command_handler, nullptr);
            g_face_renderer.set_expression(EXPR_HAPPY);
            vTaskDelay(pdMS_TO_TICKS(500));
            g_face_renderer.set_expression(EXPR_IDLE);
        }
    } else {
        ESP_LOGE(TAG, "WiFi connection failed!");
        g_face_renderer.set_expression(EXPR_SAD);
    }

    // Startup LED effect
    g_led_control.set_mode(LED_MODE_BREATHING);
    g_led_control.set_speed(64);

    // Main loop
    uint32_t last_status_ms = 0;
    uint32_t last_face_update_ms = 0;
    uint32_t last_led_update_ms = 0;
    uint32_t last_bat_read_ms = 0;

    while (1) {
        uint32_t now_ms = esp_timer_get_time() / 1000;
        TickType_t tick_now = xTaskGetTickCount();

        // Update uptime
        s_uptime_ms = now_ms;

        // Process UDP commands
        udp_command_t cmd;
        while (g_udp_client.get_command(&cmd)) {
            command_handler(cmd, nullptr);
        }

        // Update face animation (tween)
        if (g_face_renderer.is_tweening()) {
            g_face_renderer.update(now_ms);
        }

        // Update LEDs
        g_led_control.update(now_ms);

        uint32_t last_servo_poll_ms = 0;
        if (s_wifi_connected && (now_ms - last_status_ms > STATUS_INTERVAL_MS)) {
            last_status_ms = now_ms;

            status_data_t status = {};
            status.uptime_ms = s_uptime_ms;

            // Battery
            if (now_ms - last_bat_read_ms > 5000) {
                last_bat_read_ms = now_ms;
                int bat_mv = read_battery_mv();
                status.battery_voltage = bat_mv / 1000.0f;
                status.battery_percent = ((float)(bat_mv - BAT_EMPTY_MV) /
                                          (float)(BAT_FULL_MV - BAT_EMPTY_MV)) * 100.0f;
                if (status.battery_percent < 0) status.battery_percent = 0;
                if (status.battery_percent > 100) status.battery_percent = 100;
            }

            // Touch
            read_touch_data(&status);

            // IMU
            read_imu_data(&status);

            // Servo state
            status.servo_pan_pos = pan_state.current_position;
            status.servo_tilt_pos = tilt_state.current_position;
            status.servo_pan_temp = pan_state.temperature;
            status.servo_tilt_temp = tilt_state.temperature;

            // Expression
            strncpy(status.current_expression,
                    get_expression_name(s_current_expr),
                    sizeof(status.current_expression) - 1);

            // WiFi
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                status.wifi_rssi = ap_info.rssi;
            }

            // Memory
            status.free_heap = esp_get_free_heap_size();
            status.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

            // Send
            g_udp_client.send_status(status);

            // Memory warning
            if (status.free_heap < HEAP_WARN_THRESHOLD) {
                ESP_LOGW(TAG, "Low heap: %lu bytes", (unsigned long)status.free_heap);
            }
        }

        // Read microphone and send audio
        if (g_audio_pipeline.is_playing()) {
            // Wait for speaker to finish before reading mic
        }

        // Small delay to prevent watchdog starvation
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ============================================================
// Command handler
// ============================================================

static void command_handler(const udp_command_t& cmd, void* user_arg) {
    (void)user_arg;

    switch (cmd.type) {
        case CMD_HEAD: {
            ESP_LOGI(TAG, "CMD_HEAD: pan=%.1f, tilt=%.1f, speed=%u",
                     cmd.head.pan, cmd.head.tilt, cmd.head.speed);

            if (cmd.head.has_pan) {
                g_servo_control.set_pan(cmd.head.pan);
            }
            if (cmd.head.has_tilt) {
                g_servo_control.set_tilt(cmd.head.tilt);
            }
            break;
        }

        case CMD_FACE: {
            ESP_LOGI(TAG, "CMD_FACE: expression=%s, tween_ms=%lu",
                     cmd.face.expression, (unsigned long)cmd.face.tween_ms);

            expression_id_t expr_id = lookup_expression(cmd.face.expression);
            if (expr_id < EXPR_COUNT) {
                s_current_expr = expr_id;
                if (cmd.face.has_tween && cmd.face.tween_ms > 0) {
                    g_face_renderer.tween_to(expr_id, cmd.face.tween_ms);
                } else {
                    g_face_renderer.set_expression(expr_id);
                }
            } else {
                ESP_LOGW(TAG, "Unknown expression: %s", cmd.face.expression);
            }
            break;
        }

        case CMD_LED: {
            ESP_LOGI(TAG, "CMD_LED: mode=%s, r=%d, g=%d, b=%d",
                     cmd.led.mode, cmd.led.r, cmd.led.g, cmd.led.b);

            if (cmd.led.mode[0] != '\0') {
                if (strcmp(cmd.led.mode, "static") == 0) {
                    g_led_control.set_mode(LED_MODE_STATIC);
                    g_led_control.set_all(cmd.led.r, cmd.led.g, cmd.led.b);
                    g_led_control.show();
                } else if (strcmp(cmd.led.mode, "breathing") == 0) {
                    g_led_control.set_all(cmd.led.r, cmd.led.g, cmd.led.b);
                    g_led_control.set_mode(LED_MODE_BREATHING);
                } else if (strcmp(cmd.led.mode, "rainbow") == 0) {
                    g_led_control.set_mode(LED_MODE_RAINBOW);
                } else if (strcmp(cmd.led.mode, "chase") == 0) {
                    g_led_control.set_mode(LED_MODE_CHASE);
                } else if (strcmp(cmd.led.mode, "blink") == 0) {
                    g_led_control.set_all(cmd.led.r, cmd.led.g, cmd.led.b);
                    g_led_control.set_mode(LED_MODE_BLINK);
                } else if (strcmp(cmd.led.mode, "wave") == 0) {
                    g_led_control.set_all(cmd.led.r, cmd.led.g, cmd.led.b);
                    g_led_control.set_mode(LED_MODE_WAVE);
                } else if (strcmp(cmd.led.mode, "off") == 0) {
                    g_led_control.set_mode(LED_MODE_STATIC);
                    g_led_control.clear();
                }
            }

            if (cmd.led.brightness > 0) {
                g_led_control.set_brightness(cmd.led.brightness);
            }
            break;
        }

        case CMD_IR: {
            ESP_LOGI(TAG, "CMD_IR: addr=0x%04X, cmd=0x%04X, repeat=%d",
                     cmd.ir.address, cmd.ir.command, cmd.ir.repeat);

            g_ir_control.send_nec(cmd.ir.address, cmd.ir.command, cmd.ir.repeat);
            break;
        }

        case CMD_SCREEN: {
            ESP_LOGI(TAG, "CMD_SCREEN: brightness=%d, clear=%d",
                     cmd.screen.brightness, cmd.screen.clear);

            if (cmd.screen.clear) {
                g_face_renderer.clear(0x0000);
            }
            if (cmd.screen.brightness > 0) {
                // Brightness control via backlight GPIO
                gpio_set_level((gpio_num_t)38, cmd.screen.brightness > 128 ? 1 : 0);
            }
            break;
        }

        case CMD_EMOTE: {
            ESP_LOGI(TAG, "CMD_EMOTE: expression=%s, repeat=%d",
                     cmd.emote.expression, cmd.emote.repeat);

            // Emotes are sequences of expressions/animations
            // For now, treat as single face change
            expression_id_t expr_id = lookup_expression(cmd.emote.expression);
            if (expr_id < EXPR_COUNT) {
                s_current_expr = expr_id;
                g_face_renderer.tween_to(expr_id, 300);
            }
            break;
        }

        case CMD_SPEAK_START: {
            ESP_LOGI(TAG, "CMD_SPEAK_START: rate=%d", cmd.speak_start.sample_rate);

            if (!g_audio_pipeline.start_speaker()) {
                ESP_LOGE(TAG, "Failed to start speaker");
            }
            break;
        }

        case CMD_SPEAK_DATA: {
            // Audio data is sent as raw PCM16 following the command
            // The main loop handles the actual audio receive
            ESP_LOGD(TAG, "CMD_SPEAK_DATA");
            break;
        }

        case CMD_SPEAK_STOP: {
            ESP_LOGI(TAG, "CMD_SPEAK_STOP");
            g_audio_pipeline.stop_speaker();
            break;
        }

        case CMD_CUSTOM: {
            ESP_LOGI(TAG, "CMD_CUSTOM: direct expression params");
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown command type: %d", cmd.type);
            break;
    }
}

// ============================================================
// WiFi initialization
// ============================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    // WiFi credentials (try in order)
    const char* ssids[] = {WIFI_SSID, WIFI_SSID2};
    const char* passes[] = {WIFI_PASS, WIFI_PASS2};
    int num_networks = sizeof(ssids) / sizeof(ssids[0]);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handlerwifi_event_handlerwifi_event_handlerwifi_event_handlerwifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handlerwifi_event_handlerwifi_event_handlerwifi_event_handlerwifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    // Try each network
    for (int i = 0; i < num_networks; i++) {
        memset(&wifi_config, 0, sizeof(wifi_config));
        strncpy((char*)wifi_config.sta.ssid, ssids[i], sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, passes[i], sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        ESP_LOGI(TAG, "Connecting to WiFi SSID: %s (attempt %d/%d)", ssids[i], i+1, num_networks);
        
        // Wait for connection with timeout
        s_wifi_connected = false;
        int retry_count = 0;
        while (!s_wifi_connected && retry_count < 20) {
            vTaskDelay(pdMS_TO_TICKS(500));
            retry_count++;
        }
        
        if (s_wifi_connected) {
            ESP_LOGI(TAG, "Connected to %s!", ssids[i]);
            return;
        }
        
        // Disconnect and try next
        esp_wifi_disconnect();
        esp_wifi_stop();
        ESP_LOGW(TAG, "Failed to connect to %s, trying next...", ssids[i]);
    }
    

// ============================================================
// Button initialization
// ============================================================

static void buttons_init(void) {
    // Configure buttons as inputs with pull-ups
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << GPIO_BUTTON_A) |
                           (1ULL << GPIO_BUTTON_C);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
}

// ============================================================
// Battery reading
// ============================================================

static int read_battery_mv(void) {
    // Simplified battery read - returns simulated value since ADC init
    // would require additional ADC configuration
    // In production, use adc1_config_channel_atten + adc1_get_raw
    return 3800; // Default ~3.8V
}

// ============================================================
// IMU data (placeholder)
// ============================================================

static void read_imu_data(status_data_t* status) {
    // M5Stack CoreS3 has BMI270 IMU on I2C
    // For now, return zero/neutral values
    // Full IMU driver would go here
    status->imu_accel_x = 0.0f;
    status->imu_accel_y = 0.0f;
    status->imu_accel_z = 1.0f; // gravity
    status->imu_gyro_x = 0.0f;
    status->imu_gyro_y = 0.0f;
    status->imu_gyro_z = 0.0f;
    status->imu_temp = 25.0f;
}

// ============================================================
// Touch data
// ============================================================

static void read_touch_data(status_data_t* status) {
    // CoreS3 has FT6336 capacitive touch controller on I2C
    // For now, return no touch data
    // Full touch driver would go here
    status->touch_touched = false;
    status->touch_x = 0;
    status->touch_y = 0;
}
