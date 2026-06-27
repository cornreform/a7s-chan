#include "face_renderer.h"
#include "esp_log.h"
#include "bsp/m5stack_core_s3.h"
#include "bsp/display.h"
#include "esp_lcd_panel_ops.h"
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
    const bsp_display_config_t cfg = { .max_transfer_sz = 320 * 80 * 2 };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(bsp_display_new(&cfg, &m_panel, &io));
    bsp_display_brightness_init();
    bsp_display_backlight_on();
    esp_lcd_panel_disp_on_off(m_panel, true);
    // NO swap_xy, NO mirror — use BSP default orientation
    // The ILI9342C in landscape: 320x240, default MADCTL = correct
    ESP_LOGI(TAG, "Display ready (landscape)");
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

void FaceRenderer::clear(uint16_t color) {
    if (!m_panel) return;
    for (int i = 0; i < 320; i++) m_line_buf[i] = color;
    for (int y = 0; y < 240; y++)
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, 320, y+1, m_line_buf);
}

// Drawing in PHYSICAL space: 320 wide x 240 tall (landscape)
// No swap_xy — BSP default orientation
void FaceRenderer::render() {
    if (!m_panel) return;
    auto& p = m_current_params;
    
    uint16_t bg = 0x0000;
    uint16_t skin = 0xFFDC;
    uint16_t white = 0xFFFF;
    uint16_t black = 0x0000;
    uint16_t pink = 0xFD20;
    uint16_t red = 0xF800;
    
    float eye_open = p.eye_open * (m_eye_state ? 1.0f : 0.15f);
    if (eye_open < 0.1f) eye_open = 0.1f;
    
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) m_line_buf[x] = bg;
        int fy = y - m_cy;  // -120 to +119
        
        // === FACE OVAL ===
        if (fy >= -100 && fy <= 100) {
            for (int x = 0; x < 320; x++) {
                int fx = x - m_cx;
                float rx = fx/130.0f, ry = fy/100.0f;
                if (rx*rx + ry*ry <= 1.05f) m_line_buf[x] = skin;
            }
        }
        
        // === ROBOT EYES (circles) ===
        int eye_y = -20;
        int eye_r = 22;
        int eye_spacing = 50;
        int vis_r = (int)(eye_r * eye_open);
        
        int eye_centers[2] = {m_cx - eye_spacing, m_cx + eye_spacing};
        for (int ei = 0; ei < 2; ei++) {
            int ex = eye_centers[ei];
            int ey = m_cy + eye_y;
            int rel_y = fy - ey;
            if (abs(rel_y) > vis_r + 2) continue;
            
            for (int x = ex - eye_r - 2; x <= ex + eye_r + 2; x++) {
                if (x < 0 || x >= 320) continue;
                float d = (x-ex)*(x-ex) + rel_y*rel_y;
                if (d <= eye_r*eye_r) {
                    m_line_buf[x] = white;
                    
                    // Pupil
                    int px = ex + (int)(p.eye_width * 4);
                    int py = ey + (int)(p.eye_height * 4);
                    int pdx = x - px, pdy = fy - py;
                    if (pdx*pdx + pdy*pdy <= 6*6) m_line_buf[x] = black;
                    
                    // Highlight
                    int hdx = x - (px - 2), hdy = fy - (py - 2);
                    if (hdx*hdx + hdy*hdy <= 2*2) m_line_buf[x] = white;
                }
            }
        }
        
        // === MOUTH (line) ===
        int mouth_y = m_cy + 45;
        int mouth_w = 30 + (int)(p.mouth_open * 15);
        int mouth_h = 2 + (int)(p.mouth_open * 4);
        int curve = (int)((p.mouth_curve - 0.5f) * 6);
        if (abs(fy - mouth_y) <= mouth_h) {
            for (int x = m_cx - mouth_w; x <= m_cx + mouth_w; x++) {
                if (x < 0 || x >= 320) continue;
                int ly = fy - mouth_y + curve;
                if (abs(ly) <= mouth_h/2)
                    m_line_buf[x] = (p.mouth_open > 0.3f) ? red : black;
            }
        }
        
        // Blush
        if (p.blush > 0.05f) {
            int br = 5 + (int)(p.blush * 6);
            int by = m_cy + 5;
            int bx[2] = {m_cx - 68, m_cx + 68};
            for (int bi = 0; bi < 2; bi++) {
                if (abs(fy - by) <= br) {
                    int ry = fy - by;
                    for (int x = bx[bi] - br; x <= bx[bi] + br; x++) {
                        if (x < 0 || x >= 320) continue;
                        if ((x-bx[bi])*(x-bx[bi]) + ry*ry <= br*br)
                            m_line_buf[x] = pink;
                    }
                }
            }
        }
        
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, 320, y+1, m_line_buf);
    }
}
