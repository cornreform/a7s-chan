#pragma once
#include <cstdint>
#include "expressions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

class FaceRenderer {
public:
    FaceRenderer();
    ~FaceRenderer() = default;
    bool begin();
    void set_expression(expression_id_t);
    bool set_expression_by_name(const char*);
    void tween_to(expression_id_t, uint32_t ms);
    void update(uint32_t now_ms);
    void render();
    void clear(uint16_t color=0);
    expression_id_t current_expression() const { return m_current_id; }
    bool is_tweening() const { return m_tweening; }
private:
    expression_id_t m_current_id, m_target_id;
    expression_params_t m_current_params, m_target_params;
    bool m_tweening, m_eye_state;
    uint32_t m_tween_start, m_tween_duration, m_last_blink;
    uint16_t m_line_buf[320];
    esp_lcd_panel_handle_t m_panel;
    int m_cx, m_cy;
};
