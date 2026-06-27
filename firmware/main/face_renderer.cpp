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
    , m_panel(nullptr), m_cx(120), m_cy(160)
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
    // Orientation: swap XY + mirror corrects ILI9342C landscape mount
    esp_lcd_panel_swap_xy(m_panel, true);
    esp_lcd_panel_mirror(m_panel, false, true);
    ESP_LOGI(TAG, "Display ready");
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
    for (int i = 0; i < 240; i++) m_line_buf[i] = color;
    for (int y = 0; y < 320; y++)
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, 240, y+1, m_line_buf);
}

// After swap_xy: drawing space is 240 wide x 320 tall
// Line buffer = 240 pixels (one row)
// Render iterates y=0..319 (rows), draws features in face-space coordinates

void FaceRenderer::render() {
    if (!m_panel) return;
    auto& p = m_current_params;
    
    // Colors in RGB565 (BSP inverts, so we'll see inverted on screen)
    uint16_t skin = 0xFFDC;  // light peach
    uint16_t white = 0xFFFF;
    uint16_t black = 0x0000;
    uint16_t pink = 0xFD20;
    uint16_t red = 0xF800;
    uint16_t bg = 0x0841;   // dark gray (will invert to light)
    uint16_t eye_white = 0xFFFF;
    uint16_t pupil = 0x0000;
    uint16_t highlight = 0xFFFF;
    uint16_t mouth_c = 0x0000;
    
    float eye_open = p.eye_open * (m_eye_state ? 1.0f : 0.2f);
    if (eye_open < 0.1f) eye_open = 0.1f;
    
    for (int y = 0; y < 320; y++) {
        // Clear line
        for (int x = 0; x < 240; x++) m_line_buf[x] = bg;
        
        int fy = y - m_cy;  // face Y: -160 to +159
        
        // === FACE OUTLINE (round/oval) ===
        if (fy >= -130 && fy <= 130) {
            for (int x = 0; x < 240; x++) {
                int fx = x - m_cx;
                float rx = fx/100.0f, ry = fy/125.0f;
                if (rx*rx + ry*ry <= 1.05f) m_line_buf[x] = skin;
            }
        }
        
        // === EYES ===
        int eye_y = -30;
        int eye_lx = m_cx - 35, eye_rx = m_cx + 35;
        int eye_r = 17;
        int vis_r = (int)(eye_r * eye_open);
        
        // Draw each eye
        int eye_xs[2] = {eye_lx, eye_rx};
        for (int ei = 0; ei < 2; ei++) {
            int ex = eye_xs[ei];
            int rel_y = fy - (m_cy + eye_y);
            if (abs(rel_y) > vis_r + 2) continue;
            
            for (int x = ex - eye_r - 2; x <= ex + eye_r + 2; x++) {
                if (x < 0 || x >= 240) continue;
                float d = (x-ex)*(x-ex) + rel_y*rel_y;
                
                if (d <= eye_r*eye_r) {
                    // Eye white
                    m_line_buf[x] = eye_white;
                    
                    // Pupil
                    int px = ex + (int)(p.eye_width * 3);
                    int py = m_cy + eye_y + (int)(p.eye_height * 3);
                    int pr = 6;
                    int pdx = x - px, pdy = fy - py;
                    if (pdx*pdx + pdy*pdy <= pr*pr) m_line_buf[x] = pupil;
                    
                    // Highlight (top-left of pupil)
                    int hx = px - 2, hy = py - 2;
                    int hdx = x - hx, hdy = fy - hy;
                    if (hdx*hdx + hdy*hdy <= 2*2) m_line_buf[x] = highlight;
                }
            }
            
            // Eyelid (closing)
            if (eye_open < 0.8f) {
                int lid_h = (int)(eye_r * (1.0f - eye_open) * 0.6f);
                if (abs(rel_y) <= lid_h) {
                    for (int x = ex - eye_r - 2; x <= ex + eye_r + 2; x++)
                        if (x >= 0 && x < 240) m_line_buf[x] = skin;
                }
            }
        }
        
        // === MOUTH ===
        int mouth_y = m_cy + 55;
        int mouth_w = 20 + (int)(p.mouth_open * 15);
        int mouth_h = 2 + (int)(p.mouth_open * 5);
        int curve = (int)((p.mouth_curve - 0.5f) * 8);
        
        if (abs(fy - mouth_y) <= mouth_h) {
            for (int x = m_cx - mouth_w; x <= m_cx + mouth_w; x++) {
                if (x < 0 || x >= 240) continue;
                int ly = fy - mouth_y + curve;
                if (abs(ly) <= mouth_h/2)
                    m_line_buf[x] = (p.mouth_open > 0.3f) ? red : mouth_c;
            }
        }
        
        // === BLUSH ===
        if (p.blush > 0.05f) {
            int br = 6 + (int)(p.blush * 5);
            int by = m_cy + 15;
            int blush_xs[2] = {m_cx - 52, m_cx + 52};
            for (int bi = 0; bi < 2; bi++) {
                int bx = blush_xs[bi];
                if (abs(fy - by) <= br) {
                    int ry = fy - by;
                    for (int x = bx - br; x <= bx + br; x++) {
                        if (x < 0 || x >= 240) continue;
                        if ((x-bx)*(x-bx) + ry*ry <= br*br)
                            m_line_buf[x] = pink;
                    }
                }
            }
        }
        
        // Send row to display
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, 240, y+1, m_line_buf);
    }
}
