#pragma once

#include <LovyanGFX.hpp>
#include "expressions.h"

// FaceRenderer - LovyanGFX-based expression rendering engine
// Renders Stack-chan's face with 18 expressions and smooth tweening
// Screen: 320x240 IPS display on M5Stack CoreS3

class FaceRenderer {
public:
    FaceRenderer();
    ~FaceRenderer();

    // Initialize display hardware
    bool begin();

    // Set expression by ID (immediate)
    void set_expression(expression_id_t expr_id);

    // Set expression by name
    bool set_expression_by_name(const char* name);

    // Start smooth tween to target expression
    void tween_to(expression_id_t target_id, uint32_t duration_ms);

    // Get current expression ID
    expression_id_t current_expression() const { return m_current_id; }

    // Get target expression ID (during tween)
    expression_id_t target_expression() const { return m_target_id; }

    // Check if currently tweening
    bool is_tweening() const { return m_tweening; }

    // Force re-render with current parameters
    void render();

    // Update tween animation - call from main loop
    void update(uint32_t now_ms);

    // Set custom expression parameters directly
    void set_custom_params(const expression_params_t& params);

    // Get screen dimensions
    int width() const { return 320; }
    int height() const { return 240; }

    // Screen center
    int center_x() const { return 160; }
    int center_y() const { return 120; }

    // Get the LGFX display object
    LGFX_Device* display() { return &m_lcd; }

    // Clear screen
    void clear(uint16_t color = TFT_BLACK);

private:
    LGFX_Device m_lcd;

    // Current and target expression
    expression_id_t m_current_id;
    expression_id_t m_target_id;
    expression_params_t m_current_params;
    expression_params_t m_target_params;

    // Tween state
    bool m_tweening;
    uint32_t m_tween_start_ms;
    uint32_t m_tween_duration_ms;

    // Drawing helper functions
    void draw_eye(int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_eyebrow(int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_mouth(int cx, int cy, const expression_params_t& p);
    void draw_blush(int cx, int cy, const expression_params_t& p);
    void draw_pupil(int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_tears(int cx, int cy, const expression_params_t& p);
    void draw_heart_eyes(int cx, int cy, const expression_params_t& p, bool is_left);

    // Color helpers
    uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) const;
    uint16_t lerp_color(uint16_t c1, uint16_t c2, float t) const;
};
