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
    // MADCTL: MV=1 (swap), MX=1, MY=1, BGR=1 (all three mirrors)
    esp_lcd_panel_swap_xy(m_panel, true);
    esp_lcd_panel_mirror(m_panel, true, true);
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
void FaceRenderer::render() {
    if (!m_panel) return;
    auto& p = m_current_params;
    
    // Eye parameters
    int eye_r = 32;         // Eye radius (>50% of screen width 240)
    int eye_spacing = 34;   // Distance from center
    int eye_y = -15;        // Vertical position
    int eye_outline_r = 2;  // Outline thickness
    
    // Mouth
    int mouth_y = 65;
    
    float eye_open = p.eye_open * (m_eye_state ? 1.0f : 0.15f);
    if (eye_open < 0.05f) eye_open = 0.05f;
    
    uint16_t white = 0xFFFF;
    uint16_t black = 0x0000;
    uint16_t pupil_c = 0x2104;  // dark gray pupil
    uint16_t accent = 0x7BEF;   // light gray outline
    
    int W = 240, H = 320;
    
    for (int y = 0; y < H; y++) {
        // Clear line
        for (int x = 0; x < W; x++) m_line_buf[x] = black;
        int fy = y - m_cy;  // -160 to +159
        
        // === EYES (two big circles) ===
        int cx[2] = {m_cx - eye_spacing, m_cx + eye_spacing};
        int ey = m_cy + eye_y;
        int vis_r = (int)(eye_r * eye_open);
        
        for (int ei = 0; ei < 2; ei++) {
            int ex = cx[ei];
            int rel_y = fy - ey;
            if (abs(rel_y) > vis_r + eye_outline_r + 1) continue;
            
            for (int x = ex - vis_r - eye_outline_r - 1; x <= ex + vis_r + eye_outline_r + 1; x++) {
                if (x < 0 || x >= W) continue;
                int dx = x - ex;
                float d = dx*dx + rel_y*rel_y;
                
                // Eye outline (slightly larger circle)
                if (d <= (vis_r + eye_outline_r) * (vis_r + eye_outline_r) &&
                    d > vis_r * vis_r) {
                    m_line_buf[x] = accent;
                    continue;
                }
                
                // Eye white (main eye)
                if (d <= vis_r * vis_r) {
                    m_line_buf[x] = white;
                    
                    // Pupil position moves with eye_width/eye_height
                    int px_off = (int)(p.eye_width * 12 - 6);  // -6 to +6 range
                    int py_off = (int)(p.eye_height * 12 - 6);
                    int px = ex + px_off;
                    int py = ey + py_off;
                    int pupil_r = (int)(vis_r * 0.4);  // 40% of eye size
                    
                    if (dx*dx + rel_y*rel_y <= pupil_r * pupil_r) {
                        m_line_buf[x] = pupil_c;
                    }
                    
                    // Highlight
                    int hx = px - (int)(pupil_r * 0.4);
                    int hy = py - (int)(pupil_r * 0.4);
                    if ((x-hx)*(x-hx) + (fy-hy)*(fy-hy) <= 4*4) {
                        m_line_buf[x] = white;
                    }
                }
            }
            
            // Eyelid (closing eye)
            if (eye_open < 0.8f) {
                int lid_h = (int)(vis_r * (1.0f - eye_open) * 0.5f);
                if (abs(rel_y) <= lid_h) {
                    for (int x = ex - vis_r - 2; x <= ex + vis_r + 2; x++)
                        if (x >= 0 && x < W) m_line_buf[x] = black;
                }
            }
        }
        
        // === MOUTH (thin horizontal line) ===
        int mouth_ey = m_cy + mouth_y;
        int mw = 15 + (int)(p.mouth_open * 12);    // mouth width varies
        int mh = 1 + (int)(p.mouth_open * 3);       // height varies
        int curve = (int)((p.mouth_curve - 0.5f) * 8);  // -4 to +4
        
        if (abs(fy - mouth_ey) <= mh) {
            int ly = fy - mouth_ey + curve;
            if (abs(ly) <= mh) {
                for (int x = m_cx - mw; x <= m_cx + mw; x++) {
                    if (x >= 0 && x < W) {
                        m_line_buf[x] = white;
                    }
                }
            }
        }
        
        // Draw row
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, W, y+1, m_line_buf);
    }
}
