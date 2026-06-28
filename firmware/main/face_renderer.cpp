#include "face_renderer.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "bsp/m5stack_core_s3.h"
#include "bsp/display.h"
static const char* TAG = "FaceRenderer";

FaceRenderer::FaceRenderer()
    : m_current_id(EXPR_IDLE), m_target_id(EXPR_IDLE)
    , m_tweening(false), m_eye_state(true)
    , m_tween_start(0), m_tween_duration(0), m_last_blink(0)
    , m_panel(nullptr), m_cx(160), m_cy(120)
{
    const auto* n = get_expression_params(EXPR_IDLE);
    m_current_params = *n; m_target_params = *n;
}

bool FaceRenderer::begin() {
    const bsp_display_config_t cfg = { .max_transfer_sz = 240 * 80 * 2 };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(bsp_display_new(&cfg, &m_panel, &io));
    bsp_display_brightness_init();
    bsp_display_backlight_on();
    esp_lcd_panel_disp_on_off(m_panel, true);
    esp_lcd_panel_swap_xy(m_panel, true);
    esp_lcd_panel_invert_color(m_panel, false);
    // BSP invert_color(true) is active - we draw inverted colors
    esp_lcd_panel_swap_xy(m_panel, true);
    ESP_LOGI(TAG, "Display ready v2.0");
    render();
    return true;
}

void FaceRenderer::set_expression(expression_id_t id) {
    if (id >= EXPR_COUNT) return;
    m_current_id = m_target_id = id;
    m_current_params = m_target_params = *get_expression_params(id);
    m_tweening = false;
    render();
}

bool FaceRenderer::set_expression_by_name(const char* name) {
    auto id = lookup_expression(name);
    if (id >= EXPR_COUNT) return false;
    set_expression(id);
    return true;
}

void FaceRenderer::tween_to(expression_id_t id, uint32_t ms) {
    m_target_id = id;
    m_target_params = *get_expression_params(id);
    m_tween_start = (uint32_t)(esp_timer_get_time()/1000);
    m_tween_duration = ms;
    m_tweening = true;
}

void FaceRenderer::update(uint32_t now) {
    bool need = false;
    if (m_tweening) {
        uint32_t e = now - m_tween_start;
        if (e >= m_tween_duration) {
            m_tweening = false;
            m_current_params = m_target_params;
            m_current_id = m_target_id;
        } else {
            lerp_expression(&m_current_params, &m_target_params, (float)e/m_tween_duration, &m_current_params);
        }
        need = true;
    }
    if (now - m_last_blink > 3000) {
        m_last_blink = now; m_eye_state = !m_eye_state; need = true;
    }
    if (need) render();
}

// Render: 240x320 logical space (after swap_xy)
// Only BIG eyes + small mouth line (robot style)
void FaceRenderer::clear(uint16_t color) {
    if (!m_panel) return;
    for (int i = 0; i < 320; i++) m_line_buf[i] = color;
    for (int y = 0; y < 240; y++)
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, 320, y+1, m_line_buf);
}

void FaceRenderer::render() {
    if (!m_panel) return;
    auto& p = m_current_params;
    
    // Eye parameters
    int eye_size = 35;       // Half-size of square eye (each ~30% of screen)
    int eye_spacing = 55;   // Distance from center (no overlap!)
    int eye_ypos_off = -20;  // move up 20px   // Vertical position relative to m_cx
    int eye_outline_r = 2;  // Outline thickness
    
    // Mouth
    int mouth_y = 55;
    
    float eye_open = p.eye_open * (m_eye_state ? 1.0f : 0.15f);
    if (eye_open < 0.05f) eye_open = 0.05f;
    
    uint16_t white = 0xFFFF;
    uint16_t black = 0x0000;
    uint16_t pupil_c = 0x2104;
    uint16_t accent = 0x7BEF;
    
    // After swap_xy: driver swaps x/y internally, MADCTL MV=1 swaps again
    // Double swap = logical/physical match → use 320x240
    int W = 320, H = 240;
    for (int y = 0; y < H; y++) {
        // Clear line
        for (int x = 0; x < W; x++) m_line_buf[x] = black;
        int fy = y - m_cy;  // -160 to +159
        
        // === EYES (two big circles) ===
        // === EYES (two white squares at official positions) ===
        int es = eye_size;
        // Right eye center (90,93), Left eye center (230,96), size 70x70
        int er = 90, el = 230;
        // Right eye
        if (y >= 93 - es && y <= 93 + es) {
            for (int x = er - es; x <= er + es; x++) {
                if (x >= 0 && x < W) m_line_buf[x] = white;
            }
            if (abs(y - 93) <= es - 6) {  // pupil
                int ps = es - 6;
                for (int x = er - ps; x <= er + ps; x++)
                    if (x >= 0 && x < W) m_line_buf[x] = pupil_c;
            }
        }
        // Left eye
        if (y >= 96 - es && y <= 96 + es) {
            for (int x = el - es; x <= el + es; x++) {
                if (x >= 0 && x < W) m_line_buf[x] = white;
            }
            if (abs(y - 96) <= es - 6) {  // pupil
                int ps = es - 6;
                for (int x = el - ps; x <= el + ps; x++)
                    if (x >= 0 && x < W) m_line_buf[x] = pupil_c;
            }
        }
        // === MOUTH (horizontal line at Y=190, X=160) ===
        if (y >= 189 && y <= 191) {
            for (int x = 160 - 25; x <= 160 + 25; x++) {
                if (x >= 0 && x < W) m_line_buf[x] = white;
            }
        }
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, W, y+1, m_line_buf);
    }
}
