#include "face_renderer.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <cmath>

static const char* TAG = "FaceRenderer";

FaceRenderer::FaceRenderer()
    : m_current_id(EXPR_IDLE)
    , m_target_id(EXPR_IDLE)
    , m_tweening(false)
    , m_tween_start(0)
    , m_tween_duration(0)
    , m_last_blink(0)
    , m_eye_state(true)
    , m_spi(nullptr)
    , m_cx(LCD_WIDTH / 2)
    , m_cy(LCD_HEIGHT / 2)
    , m_skin_color(0xFFDC)
{
    const expression_params_t* neutral = get_expression_params(EXPR_IDLE);
    m_current_params = *neutral;
    m_target_params = *neutral;
}

FaceRenderer::~FaceRenderer() {
    if (m_spi) spi_bus_remove_device(m_spi);
}

// ── LCD Low-Level ────────────────────────────────────────────

void FaceRenderer::lcd_init() {
    // Initialize SPI bus (only if not already initialized)
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = CORE3_LCD_MOSI;
    bus_cfg.miso_io_num = -1;
    bus_cfg.sclk_io_num = CORE3_LCD_SCK;
    bus_cfg.max_transfer_sz = LCD_WIDTH * 2 + 8;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %d", err);
    }

    // Attach LCD device
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.mode = 0;
    dev_cfg.clock_speed_hz = 40 * 1000 * 1000;
    dev_cfg.spics_io_num = CORE3_LCD_CS;
    dev_cfg.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &m_spi));

    // Reset LCD
    gpio_set_direction(CORE3_LCD_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(CORE3_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(CORE3_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CORE3_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // ILI9342C init
    lcd_write_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(10));
    lcd_write_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    lcd_write_cmd(0x36);  // MADCTL
    uint8_t madctl[] = {0x08};
    lcd_write_data(madctl, 1);
    lcd_write_cmd(0x3A);  // COLMOD: 16-bit 65K
    uint8_t colmod[] = {0x55};
    lcd_write_data(colmod, 1);
    lcd_write_cmd(0x21);  // Display inversion on
    lcd_write_cmd(0x29);  // Display on

    // Backlight
    gpio_set_direction(CORE3_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(CORE3_LCD_BL, 1);
}

void FaceRenderer::lcd_write_cmd(uint8_t cmd) {
    gpio_set_level(CORE3_LCD_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_transmit(m_spi, &t);
}

void FaceRenderer::lcd_write_data(const uint8_t* data, size_t len) {
    gpio_set_level(CORE3_LCD_DC, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    spi_device_transmit(m_spi, &t);
}

void FaceRenderer::lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    uint8_t col[4] = { (uint8_t)(x1>>8), (uint8_t)x1, (uint8_t)(x2>>8), (uint8_t)x2 };
    uint8_t row[4] = { (uint8_t)(y1>>8), (uint8_t)y1, (uint8_t)(y2>>8), (uint8_t)y2 };
    lcd_write_cmd(0x2A); lcd_write_data(col, 4);
    lcd_write_cmd(0x2B); lcd_write_data(row, 4);
    lcd_write_cmd(0x2C);
}

void FaceRenderer::send_line(int y) {
    lcd_set_window(0, y, LCD_WIDTH - 1, y);
    gpio_set_level(CORE3_LCD_DC, 1);
    spi_transaction_t t = {
        .length = LCD_WIDTH * 16,
        .tx_buffer = m_line_buf,
    };
    spi_device_transmit(m_spi, &t);
}

// ── Public API ────────────────────────────────────────────────

bool FaceRenderer::begin() {
    lcd_init();
    
    // Test: fill screen red to verify LCD works
    for (int x = 0; x < LCD_WIDTH; x++) m_line_buf[x] = 0xF800;
    for (int y = 0; y < LCD_HEIGHT; y++) send_line(y);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Test: fill screen green
    for (int x = 0; x < LCD_WIDTH; x++) m_line_buf[x] = 0x07E0;
    for (int y = 0; y < LCD_HEIGHT; y++) send_line(y);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    clear(0x0000);
    render();
    return true;
}

void FaceRenderer::set_expression(expression_id_t expr_id) {
    if (expr_id >= EXPR_COUNT) return;
    m_current_id = expr_id;
    m_target_id = expr_id;
    m_current_params = *get_expression_params(expr_id);
    m_target_params = *get_expression_params(expr_id);
    m_tweening = false;
    render();
}

bool FaceRenderer::set_expression_by_name(const char* name) {
    expression_id_t id = lookup_expression(name);
    if (id >= EXPR_COUNT) return false;
    set_expression(id);
    return true;
}

void FaceRenderer::tween_to(expression_id_t target_id, uint32_t duration_ms) {
    if (target_id >= EXPR_COUNT) return;
    m_target_id = target_id;
    m_target_params = *get_expression_params(target_id);
    m_tween_start = (uint32_t)(esp_timer_get_time() / 1000);
    m_tween_duration = duration_ms;
    m_tweening = true;
}

void FaceRenderer::update(uint32_t current_time_ms) {
    bool needs_render = false;
    if (m_tweening) {
        uint32_t elapsed = current_time_ms - m_tween_start;
        if (elapsed >= m_tween_duration) {
            m_tweening = false;
            m_current_params = m_target_params;
            m_current_id = m_target_id;
        } else {
            float t = (float)elapsed / (float)m_tween_duration;
            lerp_expression(&m_current_params, &m_target_params, t, &m_current_params);
        }
        needs_render = true;
    }
    if (current_time_ms - m_last_blink > 3000) {
        m_last_blink = current_time_ms;
        m_eye_state = !m_eye_state;
        needs_render = true;
    }
    if (needs_render) render();
}

void FaceRenderer::render() {
    // Render face scanline by scanline
    for (int y = 0; y < LCD_HEIGHT; y++) {
        // Clear line
        for (int x = 0; x < LCD_WIDTH; x++) {
            m_line_buf[x] = 0x0000;
        }
        // Draw face outline on this line
        draw_face_outline(y);

        // Calculate face-space coordinates
        int fy = y - m_cy;  // -120 to +119

        // Draw features only on relevant y-range
        if (fy >= -95 && fy <= 95) {
            int eye_cy = -15;
            int eye_cx_left = -35;
            int eye_cx_right = 35;

            // Eyes
            draw_eye_line(fy, eye_cx_left, eye_cy, m_current_params, true);
            draw_eye_line(fy, eye_cx_right, eye_cy, m_current_params, false);

            // Eyebrows
            draw_eyebrow_line(fy, eye_cx_left, eye_cy - 20, m_current_params, true);
            draw_eyebrow_line(fy, eye_cx_right, eye_cy - 20, m_current_params, false);

            // Mouth
            draw_mouth_line(fy, 0, 30, m_current_params);

            // Blush
            draw_blush_line(fy, eye_cx_left - 10, eye_cy + 15, m_current_params);
            draw_blush_line(fy, eye_cx_right + 10, eye_cy + 15, m_current_params);

            // Tears
            if (m_current_params.tears > 0.1f) {
                draw_tears_line(fy, eye_cx_left, eye_cy, m_current_params);
                draw_tears_line(fy, eye_cx_right, eye_cy, m_current_params);
            }
        }
        // Send this line to LCD
        send_line(y);
    }
}

void FaceRenderer::clear(uint16_t color) {
    for (int i = 0; i < LCD_WIDTH; i++) m_line_buf[i] = color;
    for (int y = 0; y < LCD_HEIGHT; y++) send_line(y);
}

// ── Face Drawing (line-based) ────────────────────────────────

void FaceRenderer::draw_face_outline(int y) {
    int fy = y - m_cy;
    if (fy < -95 || fy > 95) return;

    for (int x = 0; x < LCD_WIDTH; x++) {
        int fx = x - m_cx;
        float rx = fx / 85.0f;
        float ry = fy / 95.0f;
        if (rx * rx + ry * ry <= 1.0f) {
            m_line_buf[x] = m_skin_color;
        }
    }
}

static bool is_in_ellipse(int x, int y, int cx, int cy, int rx, int ry) {
    float dx = (float)(x - cx) / rx;
    float dy = (float)(y - cy) / ry;
    return (dx * dx + dy * dy) <= 1.0f;
}

static bool is_point_in_rect(int px, int py, int rx, int ry, int w, int h) {
    return px >= rx && px < rx + w && py >= ry && py < ry + h;
}

void FaceRenderer::draw_eye_line(int line_y, int cx, int cy, const expression_params_t& p, bool is_left) {
    if (p.heart_eyes > 0.5f) {
        draw_heart_eyes_line(line_y, cx, cy, p, is_left);
        return;
    }

    int fy = line_y - cy;
    float open = p.eye_open;
    int eye_w = 18;
    int eye_h = (int)(12 * p.eye_height * open);

    if (eye_h < 2) {
        // Closed eye - single line
        if (fy == 0) {
            for (int x = -eye_w; x <= eye_w; x++)
                if (cx + x >= 0 && cx + x < LCD_WIDTH)
                    m_line_buf[cx + x] = 0x0000;
        }
        return;
    }

    if (abs(fy) > eye_h) return;

    // Draw white of eye for this line
    int y_in_eye = fy + eye_h;
    for (int x = -eye_w; x <= eye_w; x++) {
        if (cx + x < 0 || cx + x >= LCD_WIDTH) continue;
        if (is_in_ellipse(cx + x, cy + fy, cx, cy, eye_w, eye_h)) {
            m_line_buf[cx + x] = 0xFFFF;
        }
    }

    // Draw pupil
    int pupil_r = 5;
    int px = cx + (int)(p.eye_width * 3 - 1.5f);
    int py = cy + (int)(p.eye_height * 3 - 1.5f);
    draw_pupil(px, py, cx, cy, p, is_left, fy);
}

void FaceRenderer::draw_pupil(int px, int py, int cx, int cy, const expression_params_t& p, bool is_left, int line_y) {
    (void)p; (void)is_left;
    int fy = line_y - py;
    if (abs(fy) > 5) return;

    int pupil_r = 5;
    for (int x = -pupil_r; x <= pupil_r; x++) {
        if (px + x < 0 || px + x >= LCD_WIDTH) continue;
        if (x * x + fy * fy <= pupil_r * pupil_r) {
            m_line_buf[px + x] = 0x0000;
        }
    }
    // Highlight
    for (int x = -2; x <= 2; x++) {
        if (px + x + 2 < 0 || px + x + 2 >= LCD_WIDTH) continue;
        if (x * x + (fy - 2) * (fy - 2) <= 3) {
            m_line_buf[px + x + 2] = 0xFFFF;
        }
    }
}

void FaceRenderer::draw_eyebrow_line(int line_y, int cx, int cy, const expression_params_t& p, bool is_left) {
    float brow_up = p.brow_angle;
    float angle_deg = (brow_up - 0.5f) * 20.0f;
    if (!is_left) angle_deg = -angle_deg;

    int brow_h = (int)(p.brow_height * 8);
    int by = cy + brow_h;

    int fy = line_y - by;
    if (abs(fy) > 1) return;

    for (int x = -15; x <= 15; x++) {
        int px = cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        int y_offset = (int)(x * angle_deg / 10.0f);
        if (fy == y_offset || fy == y_offset + 1 || fy == y_offset - 1) {
            m_line_buf[px] = 0x0000;
        }
    }
}

void FaceRenderer::draw_mouth_line(int line_y, int cx, int cy, const expression_params_t& p) {
    float open = p.mouth_open;
    float curve = p.mouth_curve;
    int mw = 15 + (int)(open * 5);
    int mh = 2 + (int)(open * 8);
    int curve_offset = (int)((curve - 0.5f) * 6);

    int my = cy + curve_offset;
    int fy = line_y - my;
    if (fy < 0 || fy >= mh) return;

    for (int x = -mw; x <= mw; x++) {
        int px = cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        float nx = x / (float)mw;
        float ny = fy / (float)mh - 0.5f;
        if (ny * ny + nx * nx * 0.5f <= 0.5f) {
            m_line_buf[px] = (fy < mh / 2) ? 0x7800 : 0xF800;
        }
    }
}

void FaceRenderer::draw_blush_line(int line_y, int cx, int cy, const expression_params_t& p) {
    if (p.blush < 0.05f) return;
    int br = 8 + (int)(p.blush * 4);
    int fy = line_y - cy;
    if (abs(fy) > br) return;

    uint16_t blush_color = 0xFD20;
    for (int x = -br; x <= br; x++) {
        int px = cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        float d = (x * x + fy * fy) / (float)(br * br);
        if (d <= 1.0f) {
            float alpha = (1.0f - d) * p.blush * 80;
            if (alpha > 10) {
                m_line_buf[px] = blush_color;
            }
        }
    }
}

void FaceRenderer::draw_tears_line(int line_y, int cx, int cy, const expression_params_t& p) {
    if (p.tears < 0.05f) return;
    int intensity = (int)(p.tears * 4);

    for (int t = 0; t < intensity; t++) {
        int ty = cy + 10 + t * 6;
        if (abs(line_y - ty) > 3) continue;
        int tx = cx + (t - intensity / 2) * 3;
        for (int x = -1; x <= 1; x++) {
            int px = tx + x;
            if (px < 0 || px >= LCD_WIDTH) continue;
            int dy = abs(line_y - ty);
            if (dy <= 3 && abs(x) <= 1) {
                m_line_buf[px] = 0x7BEF;
            }
        }
    }
}

void FaceRenderer::draw_heart_eyes_line(int line_y, int cx, int cy, const expression_params_t& p, bool is_left) {
    (void)p; (void)is_left;
    int r = 7;
    int fy = line_y - cy;
    if (abs(fy) > r) return;

    for (int x = -r; x <= r; x++) {
        int px = cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        float dx = x / (float)r;
        float dy = fy / (float)r;
        float val = (dx * dx + dy * dy - 1);
        float heart = val * val * val - dx * dx * dy * dy * dy;
        if (heart < 0) {
            m_line_buf[px] = 0xF800;
        }
    }
}
