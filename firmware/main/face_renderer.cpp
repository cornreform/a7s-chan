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
    // MADCTL: MV=1 (swap), MX=1 (mirror X), MY=1 (mirror Y), BGR=1
    esp_lcd_panel_swap_xy(m_panel, true);
    esp_lcd_panel_mirror(m_panel, true, true);
    ESP_LOGI(TAG, "Display ready (MV+MX+MY)");
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

// Drawing in LOGICAL space: 240 wide x 320 tall (after swap_xy)
void FaceRenderer::render() {
    if (!m_panel) return;
    auto& p = m_current_params;
    
    uint16_t bg = 0x0000;
    uint16_t skin = 0xFFDC;
    uint16_t white = 0xFFFF;
    uint16_t black = 0x0000;
    uint16_t pink = 0xFD20;
    uint16_t red = 0xF800;
    int W = 240, H = 320;
    int cx = W/2, cy = H/2 - 10;
    
    float eye_open = p.eye_open * (m_eye_state ? 1.0f : 0.15f);
    if (eye_open < 0.1f) eye_open = 0.1f;
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) m_line_buf[x] = bg;
        int fy = y - cy;  // -160 to +159
        
        // === FACE OVAL ===
        if (fy >= -120 && fy <= 120) {
            for (int x = 0; x < W; x++) {
                int fx = x - cx;
                float rx = fx/95.0f, ry = fy/115.0f;
                if (rx*rx + ry*ry <= 1.05f) m_line_buf[x] = skin;
            }
        }
        
        // === ROBOT EYES (circles) ===
        int eye_y = -25;
        int eye_r = 18;
        int eye_spacing = 32;
        int vis_r = (int)(eye_r * eye_open);
        
        int eyes[2] = {cx - eye_spacing, cx + eye_spacing};
        for (int ei = 0; ei < 2; ei++) {
            int ex = eyes[ei], ey = cy + eye_y;
            int rel_y = fy - ey;
            if (abs(rel_y) > vis_r + 2) continue;
            for (int x = ex - eye_r - 2; x <= ex + eye_r + 2; x++) {
                if (x < 0 || x >= W) continue;
                float d = (x-ex)*(x-ex) + rel_y*rel_y;
                if (d <= eye_r*eye_r) {
                    m_line_buf[x] = white;
                    int px = ex + (int)(p.eye_width * 3);
                    int py = ey + (int)(p.eye_height * 3);
                    if ((x-px)*(x-px)+(fy-py)*(fy-py) <= 5*5) m_line_buf[x] = black;
                    if ((x-(px-2))*(x-(px-2))+(fy-(py-2))*(fy-(py-2)) <= 2*2) m_line_buf[x] = white;
                }
            }
        }
        
        // === MOUTH ===
        int mouth_y = cy + 45;
        int mw = 25 + (int)(p.mouth_open * 12);
        int mh = 2 + (int)(p.mouth_open * 4);
        int curve = (int)((p.mouth_curve - 0.5f) * 6);
        if (abs(fy - mouth_y) <= mh) {
            for (int x = cx - mw; x <= cx + mw; x++) {
                if (x < 0 || x >= W) continue;
                int ly = fy - mouth_y + curve;
                if (abs(ly) <= mh/2) m_line_buf[x] = black;
            }
        }
        
        // Blush
        if (p.blush > 0.05f) {
            int br = 5 + (int)(p.blush * 5);
            int by = cy + 5;
            int bx[2] = {cx - 55, cx + 55};
            for (int bi = 0; bi < 2; bi++) {
                if (abs(fy - by) <= br) {
                    int ry = fy - by;
                    for (int x = bx[bi]-br; x <= bx[bi]+br; x++) {
                        if (x < 0 || x >= W) continue;
                        if ((x-bx[bi])*(x-bx[bi])+ry*ry <= br*br) m_line_buf[x] = pink;
                    }
                }
            }
        }
        
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, W, y+1, m_line_buf);
    }
}
