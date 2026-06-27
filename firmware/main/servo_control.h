#pragma once

#include <cstdint>
#include <cmath>

class ServoControl {
public:
    ServoControl();
    ~ServoControl();

    // Initialize I2C to PY32 servo controller (on body module)
    bool begin();
    
    // Set pan angle (degrees, 0-180)
    void set_pan(float degrees);
    
    // Set tilt angle (degrees, 0-180)
    void set_tilt(float degrees);

private:
    bool m_initialized;
    int m_pan_duty;
    int m_tilt_duty;
    
    bool init_py32();
    void set_pwm_duty(uint8_t channel, uint8_t duty);
    uint8_t angle_to_duty(float degrees);
};
