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

    // Drawing helpers
    void clear(uint16_t color = 0x0000);
    void draw_face_frame();

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

    // LCD framebuffer (for double-buffering)
    uint16_t* m_fb;

    void draw_eye(int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_pupil(int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_eyebrow(int cx, int cy, const expression_params_t& p, bool is_left);
    void draw_mouth(int cx, int cy, const expression_params_t& p);
    void draw_blush(int cx, int cy, const expression_params_t& p);
    void draw_tears(int cx, int cy, const expression_params_t& p);
    void draw_heart_eyes(int cx, int cy, const expression_params_t& p, bool is_left);

    // Low-level LCD
    void lcd_init();
    void lcd_write_cmd(uint8_t cmd);
    void lcd_write_data(const uint8_t* data, size_t len);
    void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
    void lcd_flush();
    void draw_pixel(uint16_t x, uint16_t y, uint16_t color);

    // SPI handle
    spi_device_handle_t m_spi;
};
