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
    , m_panel(nullptr)
{
    const auto* n = get_expression_params(EXPR_IDLE);
    m_current_params = *n; m_target_params = *n;
}

bool FaceRenderer::begin() {
    const bsp_display_config_t cfg = { .max_transfer_sz = LCD_WIDTH * 80 * 2 };
    esp_lcd_panel_io_handle_t io = NULL;
    esp_err_t err = bsp_display_new(&cfg, &m_panel, &io);
    if (err != ESP_OK) { ESP_LOGE(TAG, "bsp_display_new failed: %d", err); return false; }
    bsp_display_brightness_init();
    bsp_display_backlight_on();
    esp_lcd_panel_disp_on_off(m_panel, true);
    // Orientation: swap XY + mirror Y for landscape ILI9342C
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

void FaceRenderer::render() {
    if (!m_panel) return;
    const int W = 240;  // after swap_xy
    const int H = 320;
    int cx = W/2, cy = H/2 - 10;
    uint16_t skin = 0xFFDC, black = 0x0000, red = 0xF800;
    auto& p = m_current_params;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) m_line_buf[x] = black;
        int fy = y - cy;

        // Face oval
        if (fy >= -130 && fy <= 130) {
            for (int x = 0; x < W; x++) {
                float rx = (x-cx)/95.0f, ry = fy/125.0f;
                if (rx*rx + ry*ry <= 1.0f) m_line_buf[x] = skin;
            }
        }

        // Robot eyes (big circles)
        int eye_y = -35, eye_r = 18, spacing = 35;
        float open = p.eye_open * (m_eye_state ? 1.0f : 0.15f);
        if (open < 0.1f) open = 0.1f;
        draw_circle_eye(fy, cx-spacing, cy+eye_y, eye_r, open, p);
        draw_circle_eye(fy, cx+spacing, cy+eye_y, eye_r, open, p);

        // Mouth (line)
        int my = cy + 55, mw = 25 + (int)(p.mouth_open * 20);
        int mh = 2 + (int)(p.mouth_open * 4);
        int curve = (int)((p.mouth_curve - 0.5f) * 8);
        if (abs(fy - my) <= mh) {
            for (int x = cx-mw; x <= cx+mw; x++) {
                if (x < 0 || x >= W) continue;
                int ly = fy - my + curve;
                if (abs(ly) <= mh/2)
                    m_line_buf[x] = (p.mouth_open > 0.3f) ? red : black;
            }
        }

        // Blush
        if (p.blush > 0.05f) {
            int br = 6 + (int)(p.blush * 6);
            int bx[2] = {cx-55, cx+55};
            for (int i = 0; i < 2; i++) {
                int bx_ = bx[i];
                if (abs(fy - (cy+15)) <= br) {
                    int ry = fy - (cy+15);
                    for (int x = bx_-br; x <= bx_+br; x++) {
                        if (x < 0 || x >= W) continue;
                        if (((x-bx_)*(x-bx_)+ry*ry) <= br*br)
                            m_line_buf[x] = 0xFD20;
                    }
                }
            }
        }

        esp_lcd_panel_draw_bitmap(m_panel, 0, y, W, y+1, m_line_buf);
    }
}

void FaceRenderer::draw_circle_eye(int fy, int ex, int ey, int r, float open, const expression_params_t& p) {
    int rel_y = fy - ey;
    int vis_r = (int)(r * open);
    if (abs(rel_y) > vis_r + 2) return;

    for (int x = ex-r-1; x <= ex+r+1; x++) {
        if (x < 0 || x >= 240) continue;
        float d = (x-ex)*(x-ex) + rel_y*rel_y;
        if (d <= r*r) {
            m_line_buf[x] = 0xFFFF;
            int px = ex + (int)(p.eye_width * 4 - 1);
            int py = ey + (int)(p.eye_height * 4 - 1);
            if ((x-px)*(x-px) + (rel_y-(py-ey))*(rel_y-(py-ey)) <= 25)
                m_line_buf[x] = 0x0000;
        }
    }
    // Eyelid
    if (open < 0.8f) {
        int lid_h = (int)(r * (1.0f - open) * 0.6f);
        if (abs(rel_y) <= lid_h)
            for (int x = ex-r-1; x <= ex+r+1; x++)
                if (x >= 0 && x < 240) m_line_buf[x] = 0xFFDC;
    }
}
