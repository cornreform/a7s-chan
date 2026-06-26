#include <cstdio>
#include <cstring>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"

#include "udp_client.h"
#include "expressions.h"
#include "face_renderer.h"
#include "servo_control.h"
#include "ir_control.h"
#include "led_control.h"
#include "audio_pipeline.h"

// LovyanGFX display
#include <LovyanGFX.hpp>

static const char* TAG = "a7s";

// ---- Pin definitions (M5Stack CoreS3) ----
// Display (SPI)
#define PIN_LCD_CS      3
#define PIN_LCD_DC      46
#define PIN_LCD_RST     4
#define PIN_LCD_BLK     5
#define PIN_LCD_MOSI    6
#define PIN_LCD_MISO    7
#define PIN_LCD_SCLK    8

// Servos (UART -> we'll use LEDC PWM instead)
#define PIN_SERVO_1     41
#define PIN_SERVO_2     42

// IR LED
#define PIN_IR_LED      45

// WS2812 LEDs
#define PIN_LED_STRIP   38

// I2S Microphone (NS4168 / INMP441)
#define PIN_MIC_BCK     47
#define PIN_MIC_WS      21
#define PIN_MIC_DATA    14

// I2S Speaker (NS4168 / MAX98357)
#define PIN_SPKR_BCK    48
#define PIN_SPKR_WS     45
#define PIN_SPKR_DATA   13
#define PIN_AMP_EN      40

// Other
#define PIN_BTN_A       0   // Boot button
#define PIN_BTN_B       1
#define PIN_BTN_C       2
#define PIN_VIBRATOR    39
#define PIN_BAT_ADC     10

// WiFi config
#define WIFI_SSID       "StackChan"
#define WIFI_PASS       "stackchan123"

// ---- Global state ----
static LGFX display;
static face_renderer_t g_face;
static udp_client_t g_udp;
static servo_t g_servo[2];
static ir_controller_t g_ir;
static led_controller_t g_led;
static audio_pipeline_t g_audio;

static expression_name_t g_current_expr = EXPR_NEUTRAL;

// Forward declarations
static void initialize_display(void);
static void dispatch_command(const command_t* cmd);
static void send_status_response(void);

// ---- Display initialization ----
class LGFX_M5CoreS3 : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_M5CoreS3(void) {
        // SPI bus config
        auto cfg = _bus_instance.config();
        cfg.spi_host = SPI2_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 80000000;
        cfg.freq_read = 20000000;
        cfg.use_lock = true;
        cfg.pin_sclk = PIN_LCD_SCLK;
        cfg.pin_mosi = PIN_LCD_MOSI;
        cfg.pin_miso = PIN_LCD_MISO;
        cfg.pin_dc = PIN_LCD_DC;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        // Panel config
        auto panel_cfg = _panel_instance.config();
        panel_cfg.pin_cs = PIN_LCD_CS;
        panel_cfg.pin_rst = PIN_LCD_RST;
        panel_cfg.pin_busy = -1;
        panel_cfg.memory_width = 320;
        panel_cfg.memory_height = 240;
        panel_cfg.panel_width = 320;
        panel_cfg.panel_height = 240;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = 0;
        panel_cfg.dummy_read_pixel = 8;
        panel_cfg.dummy_read_bits = 1;
        panel_cfg.readable = false;
        panel_cfg.invert = false;
        panel_cfg.rgb_order = false;
        panel_cfg.dlen_16bit = false;
        panel_cfg.bus_shared = true;
        _panel_instance.config(panel_cfg);

        // Backlight
        auto light_cfg = _light_instance.config();
        light_cfg.pin_bl = PIN_LCD_BLK;
        light_cfg.invert = false;
        light_cfg.freq = 44100;
        light_cfg.pwm_channel = 0;
        _light_instance.config(light_cfg);
        _panel_instance.setLight(&_light_instance);

        setPanel(&_panel_instance);
    }
};

static void initialize_display(void) {
    // Initialize SPI display
    display.init();
    display.setRotation(1);  // Landscape
    display.setBrightness(128);
    display.fillScreen(TFT_WHITE);
    display.setTextColor(TFT_BLACK);
    display.setTextSize(2);
    display.drawString("Stack-chan A7S", 80, 100);
    display.display();
    
    ESP_LOGI(TAG, "Display initialized");
}

// ---- Command dispatch ----
static void dispatch_command(const command_t* cmd) {
    if (!cmd) return;
    
    ESP_LOGI(TAG, "Dispatching command: %s (seq=%d)", 
             udp_client_cmd_to_str(cmd->type), cmd->seq_id);

    switch (cmd->type) {
        case CMD_SET_EXPRESSION: {
            // Find expression by name
            for (int i = 0; i < EXPR_COUNT; i++) {
                // Simple lookup by comparing with expression_get() names
                // For now, try direct match with known names
                if (strcasecmp(cmd->expression_name, "neutral") == 0) {
                    g_current_expr = EXPR_NEUTRAL;
                    break;
                } else if (strcasecmp(cmd->expression_name, "happy") == 0) {
                    g_current_expr = EXPR_HAPPY;
                    break;
                } else if (strcasecmp(cmd->expression_name, "sad") == 0) {
                    g_current_expr = EXPR_SAD;
                    break;
                } else if (strcasecmp(cmd->expression_name, "angry") == 0) {
                    g_current_expr = EXPR_ANGRY;
                    break;
                } else if (strcasecmp(cmd->expression_name, "surprised") == 0) {
                    g_current_expr = EXPR_SURPRISED;
                    break;
                } else if (strcasecmp(cmd->expression_name, "fearful") == 0) {
                    g_current_expr = EXPR_FEARFUL;
                    break;
                } else if (strcasecmp(cmd->expression_name, "disgusted") == 0) {
                    g_current_expr = EXPR_DISGUSTED;
                    break;
                } else if (strcasecmp(cmd->expression_name, "love") == 0) {
                    g_current_expr = EXPR_LOVE;
                    break;
                } else if (strcasecmp(cmd->expression_name, "crying") == 0) {
                    g_current_expr = EXPR_CRYING;
                    break;
                } else if (strcasecmp(cmd->expression_name, "annoyed") == 0) {
                    g_current_expr = EXPR_ANNOYED;
                    break;
                } else if (strcasecmp(cmd->expression_name, "sleepy") == 0) {
                    g_current_expr = EXPR_SLEEPY;
                    break;
                } else if (strcasecmp(cmd->expression_name, "confused") == 0) {
                    g_current_expr = EXPR_CONFUSED;
                    break;
                } else if (strcasecmp(cmd->expression_name, "excited") == 0) {
                    g_current_expr = EXPR_EXCITED;
                    break;
                } else if (strcasecmp(cmd->expression_name, "embarrassed") == 0) {
                    g_current_expr = EXPR_EMBARRASSED;
                    break;
                } else if (strcasecmp(cmd->expression_name, "laughing") == 0) {
                    g_current_expr = EXPR_LAUGHING;
                    break;
                } else if (strcasecmp(cmd->expression_name, "wink") == 0) {
                    g_current_expr = EXPR_WINK;
                    break;
                } else if (strcasecmp(cmd->expression_name, "shock") == 0) {
                    g_current_expr = EXPR_SHOCK;
                    break;
                } else if (strcasecmp(cmd->expression_name, "thinking") == 0) {
                    g_current_expr = EXPR_THINKING;
                    break;
                }
            }
            face_renderer_set_preset(&g_face, g_current_expr);
            break;
        }
        
        case CMD_SET_EXPRESSION_RAW: {
            face_renderer_set_expression(&g_face, cmd->expression);
            break;
        }
        
        case CMD_SET_SERVO:
        case CMD_SET_SERVO_SMOOTH: {
            int idx = cmd->servo_index;
            if (idx >= 0 && idx < 2) {
                if (cmd->servo_smooth) {
                    servo_move(&g_servo[idx], cmd->servo_angle);
                } else {
                    servo_set_angle(&g_servo[idx], cmd->servo_angle);
                }
            }
            break;
        }
        
        case CMD_SEND_IR: {
            ir_send_nec(&g_ir, cmd->ir_address, cmd->ir_command, cmd->ir_repeat);
            break;
        }
        
        case CMD_SET_LEDS: {
            if (cmd->led_count > 0) {
                for (int i = 0; i < cmd->led_count && i < 12; i++) {
                    led_set(&g_led, i, cmd->led_colors[i].r, 
                            cmd->led_colors[i].g, cmd->led_colors[i].b);
                }
                led_show(&g_led);
            }
            break;
        }
        
        case CMD_SET_LED_PATTERN: {
            led_pattern_t pattern = LED_PATTERN_SOLID;
            if (strcasecmp(cmd->led_pattern, "blink") == 0) {
                pattern = LED_PATTERN_BLINK;
            } else if (strcasecmp(cmd->led_pattern, "wave") == 0) {
                pattern = LED_PATTERN_WAVE;
            } else if (strcasecmp(cmd->led_pattern, "rainbow") == 0) {
                pattern = LED_PATTERN_RAINBOW;
            } else if (strcasecmp(cmd->led_pattern, "off") == 0) {
                pattern = LED_PATTERN_OFF;
            }
            
            led_color_t color = {
                .r = cmd->led_colors[0].r,
                .g = cmd->led_colors[0].g,
                .b = cmd->led_colors[0].b
            };
            led_set_pattern(&g_led, pattern, color, cmd->led_speed);
            break;
        }
        
        case CMD_SET_VOLUME: {
            if (cmd->volume >= 0 && cmd->volume <= 100) {
                // Stub: volume control
                ESP_LOGI(TAG, "Volume set to %d", cmd->volume);
            }
            break;
        }
        
        case CMD_GET_STATUS: {
            send_status_response();
            break;
        }
        
        case CMD_REBOOT: {
            ESP_LOGI(TAG, "Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
            break;
        }
        
        case CMD_PING: {
            // Send pong
            status_t status = {};
            status.seq_id = cmd->seq_id;
            udp_client_send_status(&g_udp, &status);
            break;
        }
        
        default:
            ESP_LOGW(TAG, "Unhandled command type: %d", cmd->type);
            break;
    }
}

static void send_status_response(void) {
    status_t status = {};
    status.servo_angle[0] = servo_get_angle(&g_servo[0]);
    status.servo_angle[1] = servo_get_angle(&g_servo[1]);
    status.expression = g_current_expr;
    status.wifi_rssi = 0;
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        status.wifi_rssi = ap_info.rssi;
    }
    
    status.uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    status.heap_free = (uint32_t)esp_get_free_heap_size();
    status.mic_active = audio_is_mic_enabled(&g_audio);
    status.spkr_active = audio_is_spkr_enabled(&g_audio);
    
    udp_client_send_status(&g_udp, &status);
}

// ---- Main application task ----
static void app_main_task(void* arg) {
    ESP_LOGI(TAG, "Application task started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10);  // ~100Hz
    
    command_t cmd;
    uint32_t frame_count = 0;
    
    while (1) {
        vTaskDelayUntil(&last_wake_time, period);
        frame_count++;
        
        // Process any pending commands (non-blocking)
        while (udp_client_receive(&g_udp, &cmd, 0)) {
            dispatch_command(&cmd);
        }
        
        // Update servo positions (smooth movement)
        servo_update(&g_servo[0]);
        servo_update(&g_servo[1]);
        
        // Update LED animation
        if (frame_count % 3 == 0) {  // Every ~30ms
            led_update(&g_led);
        }
        
        // Render face (every frame ~100Hz)
        face_renderer_update(&g_face);
    }
}

// ---- Entry point ----
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "A7S-Firmware starting...");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip: %s", ESP_CHIP_NAME);
    
    // Initialize subsystems
    ESP_LOGI(TAG, "Initializing subsystems...");
    
    // Display
    initialize_display();
    face_renderer_init(&g_face, &display);
    face_renderer_set_preset(&g_face, EXPR_NEUTRAL);
    
    // Servos (LEDC PWM at 50Hz)
    if (servo_init(&g_servo[0], LEDC_TIMER_0, LEDC_CHANNEL_0, PIN_SERVO_1) != 0) {
        ESP_LOGE(TAG, "Failed to init servo 0");
    }
    if (servo_init(&g_servo[1], LEDC_TIMER_0, LEDC_CHANNEL_1, PIN_SERVO_2) != 0) {
        ESP_LOGE(TAG, "Failed to init servo 1");
    }
    servo_set_angle(&g_servo[0], 90.0f);
    servo_set_angle(&g_servo[1], 90.0f);
    
    // IR LED
    if (ir_init(&g_ir, PIN_IR_LED) != 0) {
        ESP_LOGE(TAG, "Failed to init IR");
    }
    
    // WS2812 LED strip
    if (led_init(&g_led, PIN_LED_STRIP) != 0) {
        ESP_LOGE(TAG, "Failed to init LEDs");
    }
    led_set_all(&g_led, 0, 0, 0);
    led_show(&g_led);
    
    // Audio pipeline
    if (audio_init(&g_audio, 
                    PIN_MIC_BCK, PIN_MIC_WS, PIN_MIC_DATA,
                    PIN_SPKR_BCK, PIN_SPKR_WS, PIN_SPKR_DATA,
                    PIN_AMP_EN) != 0) {
        ESP_LOGE(TAG, "Failed to init audio");
    }
    
    // WiFi + UDP
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    if (udp_client_init(&g_udp, WIFI_SSID, WIFI_PASS) != 0) {
        ESP_LOGE(TAG, "Failed to init UDP client");
    }
    udp_client_start(&g_udp);
    
    ESP_LOGI(TAG, "All subsystems initialized. Starting main loop...");
    
    // Create main application task
    xTaskCreatePinnedToCore(app_main_task, "app_main", 8192, NULL, 5, NULL, 1);
}
