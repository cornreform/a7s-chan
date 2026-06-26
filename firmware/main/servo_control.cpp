#include "servo_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "servo";

// Convert angle (0-180) to pulse width in microseconds
static inline uint32_t angle_to_pulse(float angle_deg) {
    if (angle_deg < SERVO_MIN_ANGLE) angle_deg = SERVO_MIN_ANGLE;
    if (angle_deg > SERVO_MAX_ANGLE) angle_deg = SERVO_MAX_ANGLE;
    float ratio = angle_deg / SERVO_MAX_ANGLE;
    return SERVO_MIN_PULSE_US + (uint32_t)(ratio * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US));
}

// Convert pulse width (us) to LEDC duty value
static inline uint32_t pulse_to_duty(uint32_t pulse_us) {
    // duty = (pulse_us / period_us) * (2^resolution_bits)
    // period_us = 1,000,000 / 50 = 20,000 us
    // For 16-bit resolution: max_duty = 65535
    return (uint32_t)((uint64_t)pulse_us * 65535 / 20000);
}

int servo_init(servo_t* servo, ledc_timer_t timer, ledc_channel_t channel, int gpio_pin) {
    // Configure timer
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .timer_num = timer,
        .freq_hz = SERVO_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return -1;
    }

    // Configure channel
    servo->ledc_config = {
        .gpio_num = gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = timer,
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = 0 }
    };
    err = ledc_channel_config(&servo->ledc_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return -1;
    }

    servo->current_angle = 90.0f;  // Start at center
    servo->target_angle = 90.0f;
    servo->enabled = true;

    // Set initial position to 90 degrees
    uint32_t pulse = angle_to_pulse(90.0f);
    uint32_t duty = pulse_to_duty(pulse);
    ledc_set_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel, duty);
    ledc_update_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel);

    return 0;
}

void servo_set_angle(servo_t* servo, float angle_deg) {
    if (angle_deg < SERVO_MIN_ANGLE) angle_deg = SERVO_MIN_ANGLE;
    if (angle_deg > SERVO_MAX_ANGLE) angle_deg = SERVO_MAX_ANGLE;
    servo->target_angle = angle_deg;
}

float servo_get_angle(const servo_t* servo) {
    return servo->current_angle;
}

void servo_move(servo_t* servo, float angle_deg) {
    if (angle_deg < SERVO_MIN_ANGLE) angle_deg = SERVO_MIN_ANGLE;
    if (angle_deg > SERVO_MAX_ANGLE) angle_deg = SERVO_MAX_ANGLE;

    float start_angle = servo->current_angle;
    for (int step = 1; step <= SERVO_SMOOTH_STEPS; step++) {
        float t = (float)step / (float)SERVO_SMOOTH_STEPS;
        float smooth_t = t * t * (3.0f - 2.0f * t);  // Smoothstep
        float angle = start_angle + (angle_deg - start_angle) * smooth_t;
        
        uint32_t pulse = angle_to_pulse(angle);
        uint32_t duty = pulse_to_duty(pulse);
        ledc_set_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel, duty);
        ledc_update_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel);
        
        servo->current_angle = angle;
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_DELAY_MS));
    }
    servo->current_angle = angle_deg;
    servo->target_angle = angle_deg;
}

bool servo_update(servo_t* servo) {
    if (!servo->enabled) return true;

    float diff = servo->target_angle - servo->current_angle;
    if (fabsf(diff) < 0.5f) {
        // Snap to target
        servo->current_angle = servo->target_angle;
        return true;
    }

    // Move at ~60 degrees/sec (50 steps * 10ms = 500ms for 180 degrees)
    float step = diff * 0.1f;
    float new_angle = servo->current_angle + step;

    uint32_t pulse = angle_to_pulse(new_angle);
    uint32_t duty = pulse_to_duty(pulse);
    ledc_set_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel, duty);
    ledc_update_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel);

    servo->current_angle = new_angle;
    return false;
}

void servo_disable(servo_t* servo) {
    servo->enabled = false;
    ledc_stop(servo->ledc_config.speed_mode, servo->ledc_config.channel, 0);
}

void servo_enable(servo_t* servo) {
    servo->enabled = true;
    uint32_t pulse = angle_to_pulse(servo->current_angle);
    uint32_t duty = pulse_to_duty(pulse);
    ledc_set_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel, duty);
    ledc_update_duty(servo->ledc_config.speed_mode, servo->ledc_config.channel);
}
