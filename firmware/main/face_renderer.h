#pragma once

#include <cstdint>
#include "expressions.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"

// M5Stack CoreS3 LCD pins
#define CORE3_LCD_CS    GPIO_NUM_5
#define CORE3_LCD_MOSI  GPIO_NUM_6
#define CORE3_LCD_SCK   GPIO_NUM_7
#define CORE3_LCD_DC    GPIO_NUM_8
#define CORE3_LCD_RST   GPIO_NUM_9
#define CORE3_LCD_BL    GPIO_NUM_38

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

class FaceRenderer {
public:
    FaceRenderer();
    ~FaceRenderer();

    bool begin();
    void set_expression(expression_id_t expr_id);
    bool set_expression_by_name(const char* name);
    void tween_to(expression_id_t target_id, uint32_t duration_ms);
    void update(uint32_t current_time_ms);
    void render();
    void clear(uint16_t color = 0x0000);

    expression_id_t current_expression() const { return m_current_id; }
    bool is_tweening() const { return m_tweening; }

private:
    expression_id_t m_current_id;
    expression_id_t m_target_id;
    expression_params_t m_current_params;
    expression_params_t m_target_params;
    bool m_tweening;
    uint32_t m_tween_start;
    uint32_t m_tween_duration;
    uint32_t m_last_blink;
    bool m_eye_state;

    // Line buffer (640 bytes per line, DMA-safe)
    uint16_t m_line_buf[LCD_WIDTH];

    void draw_eye_line(int line_y, int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_pupil(int px, int py, int cx, int cy, const expression_params_t& p, bool is_left, int line_y);
    void draw_eyebrow_line(int line_y, int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_mouth_line(int line_y, int cx, int cy, const expression_params_t& p);
    void draw_blush_line(int line_y, int cx, int cy, const expression_params_t& p);
    void draw_tears_line(int line_y, int cx, int cy, const expression_params_t& p);
    void draw_heart_eyes_line(int line_y, int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_face_outline(int y);

    // Low-level LCD
    void lcd_init();
    void lcd_write_cmd(uint8_t cmd);
    void lcd_write_data(const uint8_t* data, size_t len);
    void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
    void send_line(int y);

    // SPI handle
    spi_device_handle_t m_spi;

    // Face center
    int m_cx, m_cy;

    // Skin tone
    uint16_t m_skin_color;
};
