#include "face_renderer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include <cstring>
#include <cmath>

static const char* TAG = "FaceRenderer";

// ── Constructor / Destructor ──────────────────────────────────

FaceRenderer::FaceRenderer()
    : m_current_id(EXPR_IDLE)
    , m_target_id(EXPR_IDLE)
    , m_tweening(false)
    , m_tween_start(0)
    , m_tween_duration(0)
    , m_last_blink(0)
    , m_eye_state(true)
    , m_fb(nullptr)
    , m_spi(nullptr)
{
    const expression_params_t* neutral = get_expression_params(EXPR_IDLE);
    m_current_params = *neutral;
    m_target_params = *neutral;
}

FaceRenderer::~FaceRenderer() {
    if (m_fb) free(m_fb);
    if (m_spi) {
        spi_bus_remove_device(m_spi);
    }
}

// ── LCD Init ──────────────────────────────────────────────────

void FaceRenderer::lcd_init() {
    // Initialize SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CORE3_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = CORE3_LCD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Attach LCD device
    spi_device_interface_config_t dev_cfg = {
        .mode = 0,
        .clock_speed_hz = 40 * 1000 * 1000,  // 40MHz
        .spics_io_num = CORE3_LCD_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &m_spi));

    // Reset LCD
    gpio_set_direction(CORE3_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(CORE3_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CORE3_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // ILI9342C init sequence
    lcd_write_cmd(0x01);  // Software reset
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_write_cmd(0x11);  // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_write_cmd(0x36);  // MADCTL
    uint8_t madctl = 0x08;  // RGB order
    lcd_write_data(&madctl, 1);
    lcd_write_cmd(0x3A);  // COLMOD: 16-bit
    uint8_t colmod = 0x55;
    lcd_write_data(&colmod, 1);
    lcd_write_cmd(0x21);  // Display inversion on
    lcd_write_cmd(0x29);  // Display on

    // Backlight
    gpio_set_direction(CORE3_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(CORE3_LCD_BL, 1);

    // Allocate framebuffer (320*240*2 bytes = 150KB)
    m_fb = (uint16_t*)heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_DMA);
    if (!m_fb) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        m_fb = (uint16_t*)malloc(LCD_WIDTH * LCD_HEIGHT * 2);
    }
}

void FaceRenderer::lcd_write_cmd(uint8_t cmd) {
    gpio_set_level(CORE3_LCD_DC, 0);  // Command mode
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_transmit(m_spi, &t);
}

void FaceRenderer::lcd_write_data(const uint8_t* data, size_t len) {
    gpio_set_level(CORE3_LCD_DC, 1);  // Data mode
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_transmit(m_spi, &t);
}

void FaceRenderer::lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    uint8_t col_data[4] = { (uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2 };
    uint8_t row_data[4] = { (uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2 };
    lcd_write_cmd(0x2A);  // CASET
    lcd_write_data(col_data, 4);
    lcd_write_cmd(0x2B);  // RASET
    lcd_write_data(row_data, 4);
    lcd_write_cmd(0x2C);  // RAMWR
}

void FaceRenderer::lcd_flush() {
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    gpio_set_level(CORE3_LCD_DC, 1);

    // Send framebuffer in chunks of 4096 bytes (ESP32-S3 SPI limit)
    const size_t chunk_size = 4096;
    uint8_t* data = (uint8_t*)m_fb;
    size_t remaining = LCD_WIDTH * LCD_HEIGHT * 2;

    while (remaining > 0) {
        size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;
        spi_transaction_t t = {
            .length = to_send * 8,
            .tx_buffer = data,
        };
        spi_device_transmit(m_spi, &t);
        data += to_send;
        remaining -= to_send;
    }
}

void FaceRenderer::draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    m_fb[y * LCD_WIDTH + x] = color;
}

// ── Public API ────────────────────────────────────────────────

bool FaceRenderer::begin() {
    lcd_init();
    clear(0x0000);
    lcd_flush();
    render();
    return true;
}

void FaceRenderer::set_expression(expression_id_t expr_id) {
    if (expr_id >= EXPR_COUNT) return;
    m_current_id = expr_id;
    m_target_id = expr_id;
    const expression_params_t* params = get_expression_params(expr_id);
    m_current_params = *params;
    m_target_params = *params;
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
    // Handle tween
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
        render();
    }

    // Auto-blink every 3 seconds
    if (current_time_ms - m_last_blink > 3000) {
        m_last_blink = current_time_ms;
        m_eye_state = !m_eye_state;
        render();
    }
}

void FaceRenderer::render() {
    clear(0x0000);
    draw_face_frame();
    lcd_flush();
}

void FaceRenderer::clear(uint16_t color) {
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        m_fb[i] = color;
    }
}

// ── Face drawing ──────────────────────────────────────────────

void FaceRenderer::draw_face_frame() {
    int cx = LCD_WIDTH / 2;   // 160
    int cy = LCD_HEIGHT / 2;  // 120

    // Draw face outline (large ellipse)
    for (int y = -95; y <= 95; y++) {
        for (int x = -85; x <= 85; x++) {
            float rx = x / 85.0f;
            float ry = y / 95.0f;
            if (rx * rx + ry * ry <= 1.0f) {
                draw_pixel(cx + x, cy + y, 0xFFDC);  // Skin tone
            }
        }
    }

    // Draw features
    int eye_cx_left = cx - 35;
    int eye_cx_right = cx + 35;
    int eye_cy = cy - 15;

    draw_eye(eye_cx_left, eye_cy, m_current_params, true);
    draw_eye(eye_cx_right, eye_cy, m_current_params, false);

    // Eyebrows
    draw_eyebrow(eye_cx_left, eye_cy - 20, m_current_params, true);
    draw_eyebrow(eye_cx_right, eye_cy - 20, m_current_params, false);

    // Mouth
    draw_mouth(cx, cy + 30, m_current_params);

    // Blush
    draw_blush(eye_cx_left - 10, eye_cy + 15, m_current_params);
    draw_blush(eye_cx_right + 10, eye_cy + 15, m_current_params);

    // Tears
    if (m_current_params.tears > 0.1f) {
        draw_tears(eye_cx_left, eye_cy, m_current_params);
        draw_tears(eye_cx_right, eye_cy, m_current_params);
    }
}

void FaceRenderer::draw_eye(int cx, int cy, const expression_params_t& p, bool is_left) {
    if (p.heart_eyes > 0.5f) {
        draw_heart_eyes(cx, cy, p, is_left);
        return;
    }

    float open = p.eye_open;
    int eye_w = 18;
    int eye_h = (int)(12 * p.eye_height * open);

    if (eye_h < 2) {
        // Closed eye (line)
        for (int x = -eye_w; x <= eye_w; x++) {
            draw_pixel(cx + x, cy, 0x0000);
        }
        return;
    }

    // White of eye
    for (int y = -eye_h; y <= eye_h; y++) {
        for (int x = -eye_w; x <= eye_w; x++) {
            float rx = x / (float)eye_w;
            float ry = y / (float)eye_h;
            if (rx * rx + ry * ry <= 1.0f) {
                draw_pixel(cx + x, cy + y, 0xFFFF);  // White
            }
        }
    }

    // Pupil
    draw_pupil(cx, cy, p, is_left);
}

void FaceRenderer::draw_pupil(int cx, int cy, const expression_params_t& p, bool is_left) {
    float open = p.eye_open;
    int eye_h = (int)(12 * p.eye_height * open);
    if (eye_h < 3) return;

    // Pupil position follows gaze
    int pupil_r = 5;
    int px = cx + (int)(p.eye_width * 3 - 1.5f);
    int py = cy + (int)(p.eye_height * 3 - 1.5f);

    // Draw pupil (dark circle)
    for (int y = -pupil_r; y <= pupil_r; y++) {
        for (int x = -pupil_r; x <= pupil_r; x++) {
            if (x * x + y * y <= pupil_r * pupil_r) {
                draw_pixel(px + x, py + y, 0x0000);
            }
        }
    }

    // Highlight
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            if (x * x + y * y <= 3) {
                draw_pixel(px + x + 2, py + y - 2, 0xFFFF);
            }
        }
    }
}

void FaceRenderer::draw_eyebrow(int cx, int cy, const expression_params_t& p, bool is_left) {
    // Brow angle: 0=neutral, 0.5=up, 1.0=down
    float brow_up = p.brow_angle;
    float angle_deg = (brow_up - 0.5f) * 20.0f;  // -10 to +10 degrees
    if (!is_left) angle_deg = -angle_deg;

    int brow_h = (int)(p.brow_height * 8);
    int bx = cx;
    int by = cy + brow_h;

    // Draw brow line
    for (int x = -15; x <= 15; x++) {
        int y_offset = (int)(x * angle_deg / 10.0f);
        draw_pixel(bx + x, by + y_offset, 0x0000);
    }
}

void FaceRenderer::draw_mouth(int cx, int cy, const expression_params_t& p) {
    float open = p.mouth_open;
    float curve = p.mouth_curve;  // 0=frown, 0.5=neutral, 1=smile

    // Mouth shape
    int mw = 15 + (int)(open * 5);
    int mh = 2 + (int)(open * 8);

    // Smile curve offset
    int curve_offset = (int)((curve - 0.5f) * 6);

    for (int y = 0; y < mh; y++) {
        for (int x = -mw; x <= mw; x++) {
            // Parabolic mouth shape
            float nx = x / (float)mw;
            float ny = y / (float)mh - 0.5f + curve_offset * 0.05f;
            if (ny * ny + nx * nx * 0.5f <= 0.5f) {
                draw_pixel(cx + x, cy + y, (y < mh / 2) ? 0x7800 : 0xF800);
            }
        }
    }
}

void FaceRenderer::draw_blush(int cx, int cy, const expression_params_t& p) {
    if (p.blush < 0.05f) return;

    int br = 8 + (int)(p.blush * 4);
    uint16_t blush_color = 0xFD20;  // Pink

    for (int y = -br; y <= br; y++) {
        for (int x = -br; x <= br; x++) {
            float d = (x * x + y * y) / (float)(br * br);
            if (d <= 1.0f) {
                uint8_t alpha = (uint8_t)((1.0f - d) * p.blush * 80);
                if (alpha > 10) {
                    draw_pixel(cx + x, cy + y, blush_color);
                }
            }
        }
    }
}

void FaceRenderer::draw_tears(int cx, int cy, const expression_params_t& p) {
    if (p.tears < 0.05f) return;

    int intensity = (int)(p.tears * 4);
    uint16_t tear_color = 0x7BEF;  // Light blue-gray

    for (int t = 0; t < intensity; t++) {
        int tx = cx + (t - intensity / 2) * 3;
        int ty = cy + 10 + t * 6;
        for (int y = -2; y <= 3; y++) {
            for (int x = -1; x <= 1; x++) {
                draw_pixel(tx + x, ty + y, tear_color);
            }
        }
    }
}

void FaceRenderer::draw_heart_eyes(int cx, int cy, const expression_params_t& p, bool is_left) {
    (void)p;
    uint16_t heart_color = 0xF800;  // Red

    // Simple heart shape
    int r = 7;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            float dx = x / (float)r;
            float dy = y / (float)r;
            // Heart equation
            float val = (dx * dx + dy * dy - 1);
            float heart = val * val * val - dx * dx * dy * dy * dy;
            if (heart < 0) {
                draw_pixel(cx + x, cy + y, heart_color);
            }
        }
    }
}
