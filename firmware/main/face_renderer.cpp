#include "face_renderer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
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
    , m_panel(nullptr)
    , m_cx(LCD_WIDTH / 2)
    , m_cy(LCD_HEIGHT / 2)
    , m_skin_color(0xFFDC)
{
    const expression_params_t* neutral = get_expression_params(EXPR_IDLE);
    m_current_params = *neutral;
    m_target_params = *neutral;
}

FaceRenderer::~FaceRenderer() {}

bool FaceRenderer::begin() {
    // Initialize display via CoreS3 BSP
    // This handles PWR_EN, AW9523 reset, GPIO35 DC/MISO, init sequence
    bsp_display_start();
    bsp_display_backlight_on();
    
    // Get the LCD panel handle
    m_panel = bsp_display_get_panel();
    
    ESP_LOGI(TAG, "Display initialized via BSP");
    
    // Test: flash red/green to confirm
    clear(0xF800);
    vTaskDelay(pdMS_TO_TICKS(300));
    clear(0x07E0);
    vTaskDelay(pdMS_TO_TICKS(300));
    
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

void FaceRenderer::clear(uint16_t color) {
    if (m_panel) {
        esp_lcd_panel_draw_bitmap(m_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, (uint16_t*)&color);
    }
}

void FaceRenderer::render() {
    if (!m_panel) return;
    
    // Draw face line by line directly to LCD via BSP
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            m_line_buf[x] = 0x0000;
        }
        
        int fy = y - m_cy;
        draw_face_outline(fy);
        
        if (fy >= -95 && fy <= 95) {
            draw_eye_line(fy, -35, -15, m_current_params, true);
            draw_eye_line(fy, 35, -15, m_current_params, false);
            draw_eyebrow_line(fy, -35, -35, m_current_params, true);
            draw_eyebrow_line(fy, 35, -35, m_current_params, false);
            draw_mouth_line(fy, 0, 30, m_current_params);
            draw_blush_line(fy, -45, 0, m_current_params);
            draw_blush_line(fy, 45, 0, m_current_params);
            if (m_current_params.tears > 0.1f) {
                draw_tears_line(fy, -35, -15, m_current_params);
                draw_tears_line(fy, 35, -15, m_current_params);
            }
        }
        
        // Send this line to display
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, LCD_WIDTH, y + 1, m_line_buf);
    }
}

void FaceRenderer::draw_face_outline(int fy) {
    if (fy < -95 || fy > 95) return;
    for (int x = 0; x < LCD_WIDTH; x++) {
        int fx = x - m_cx;
        float rx = fx / 85.0f, ry = fy / 95.0f;
        if (rx * rx + ry * ry <= 1.0f) m_line_buf[x] = m_skin_color;
    }
}

void FaceRenderer::draw_eye_line(int fy, int cx, int cy, const expression_params_t& p, bool is_left) {
    if (p.heart_eyes > 0.5f) { draw_heart_eyes_line(fy, cx, cy, p, is_left); return; }
    int eye_w = 18, eye_h = (int)(12 * p.eye_height * p.eye_open);
    if (eye_h < 2) {
        if (fy - cy == 0) for (int x = -eye_w; x <= eye_w; x++) {
            int px = m_cx + cx + x;
            if (px >= 0 && px < LCD_WIDTH) m_line_buf[px] = 0x0000;
        }
        return;
    }
    int rel_y = fy - cy;
    if (abs(rel_y) > eye_h) return;
    for (int x = -eye_w; x <= eye_w; x++) {
        int px = m_cx + cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        if ((float)(x*x)/(eye_w*eye_w) + (float)(rel_y*rel_y)/(eye_h*eye_h) <= 1.0f)
            m_line_buf[px] = 0xFFFF;
    }
    int pr = 5;
    int px = m_cx + cx + (int)(p.eye_width * 3 - 1.5f);
    int py = cy + (int)(p.eye_height * 3 - 1.5f);
    draw_pupil(px, py, fy);
}

void FaceRenderer::draw_pupil(int px, int py, int fy) {
    int rel_y = fy - py;
    if (abs(rel_y) > 5) return;
    for (int x = -5; x <= 5; x++) {
        if (px + x < 0 || px + x >= LCD_WIDTH) continue;
        if (x*x + rel_y*rel_y <= 25) m_line_buf[px + x] = 0x0000;
        if (x*x + (rel_y-2)*(rel_y-2) <= 3) m_line_buf[px + x + 2] = 0xFFFF;
    }
}

void FaceRenderer::draw_eyebrow_line(int fy, int cx, int cy, const expression_params_t& p, bool is_left) {
    float angle = ((p.brow_angle - 0.5f) * 20.0f) * (is_left ? 1.0f : -1.0f);
    int by = cy + (int)(p.brow_height * 8);
    int rel_y = fy - by;
    if (abs(rel_y) > 1) return;
    for (int x = -15; x <= 15; x++) {
        int px = m_cx + cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        if (abs(rel_y - (int)(x * angle / 10.0f)) <= 1) m_line_buf[px] = 0x0000;
    }
}

void FaceRenderer::draw_mouth_line(int fy, int cx, int cy, const expression_params_t& p) {
    int mw = 15 + (int)(p.mouth_open * 5);
    int mh = 2 + (int)(p.mouth_open * 8);
    int yo = (int)((p.mouth_curve - 0.5f) * 6);
    int rel_y = fy - (cy + yo);
    if (rel_y < 0 || rel_y >= mh) return;
    for (int x = -mw; x <= mw; x++) {
        int px = m_cx + cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        float nx = x / (float)mw, ny = rel_y / (float)mh - 0.5f;
        if (ny*ny + nx*nx*0.5f <= 0.5f) m_line_buf[px] = (rel_y < mh/2) ? 0x7800 : 0xF800;
    }
}

void FaceRenderer::draw_blush_line(int fy, int cx, int cy, const expression_params_t& p) {
    if (p.blush < 0.05f) return;
    int br = 8 + (int)(p.blush * 4);
    int rel_y = fy - cy;
    if (abs(rel_y) > br) return;
    for (int x = -br; x <= br; x++) {
        int px = m_cx + cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        float d = (x*x + rel_y*rel_y) / (float)(br*br);
        if (d <= 1.0f) m_line_buf[px] = 0xFD20;
    }
}

void FaceRenderer::draw_tears_line(int fy, int cx, int cy, const expression_params_t& p) {
    if (p.tears < 0.05f) return;
    int n = (int)(p.tears * 4);
    for (int t = 0; t < n; t++) {
        int ty = cy + 10 + t * 6;
        if (abs(fy - ty) > 3) continue;
        int tx = cx + (t - n/2) * 3;
        for (int x = -1; x <= 1; x++) {
            int px = m_cx + tx + x;
            if (px < 0 || px >= LCD_WIDTH) continue;
            if (abs(fy - ty) <= 3) m_line_buf[px] = 0x7BEF;
        }
    }
}

void FaceRenderer::draw_heart_eyes_line(int fy, int cx, int cy, const expression_params_t& p, bool is_left) {
    (void)p; (void)is_left;
    int r = 7, rel_y = fy - cy;
    if (abs(rel_y) > r) return;
    for (int x = -r; x <= r; x++) {
        int px = m_cx + cx + x;
        if (px < 0 || px >= LCD_WIDTH) continue;
        float dx = x/(float)r, dy = rel_y/(float)r;
        float v = (dx*dx + dy*dy - 1);
        if (v*v*v - dx*dx*dy*dy*dy < 0) m_line_buf[px] = 0xF800;
    }
}
