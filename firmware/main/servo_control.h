#ifndef A7S_SERVO_CONTROL_H
#define A7S_SERVO_CONTROL_H

#include <cstdint>
#include <driver/ledc.h>

#ifdef __cplusplus
extern "C" {
#endif

// Servo configuration
#define SERVO_PWM_FREQ      50      // 50 Hz standard servo frequency
#define SERVO_PWM_RESOLUTION LEDC_TIMER_16_BIT
#define SERVO_MIN_PULSE_US  500     // 0 degrees
#define SERVO_MAX_PULSE_US  2500    // 180 degrees
#define SERVO_MIN_ANGLE     0.0f
#define SERVO_MAX_ANGLE     180.0f

// Smooth movement parameters
#define SERVO_SMOOTH_STEPS  50      // Number of interpolation steps
#define SERVO_STEP_DELAY_MS 10      // Delay between steps (ms)

typedef struct {
    ledc_channel_config_t ledc_config;
    float current_angle;
    float target_angle;
    bool  enabled;
} servo_t;

// Initialize servo on given LEDC timer and channel
// Returns 0 on success
int servo_init(servo_t* servo, ledc_timer_t timer, ledc_channel_t channel, int gpio_pin);

// Set target angle (will be clamped 0-180)
void servo_set_angle(servo_t* servo, float angle_deg);

// Get current angle
float servo_get_angle(const servo_t* servo);

// Move smoothly to target angle (blocking)
void servo_move(servo_t* servo, float angle_deg);

// Update servo to reach target (non-blocking, call in loop)
// Returns true when target reached
bool servo_update(servo_t* servo);

// Disable servo output
void servo_disable(servo_t* servo);

// Enable servo output
void servo_enable(servo_t* servo);

#ifdef __cplusplus
}
#endif

#endif // A7S_SERVO_CONTROL_H
