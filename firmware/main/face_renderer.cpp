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
    , m_panel(nullptr), m_cx(LCD_WIDTH/2), m_cy(LCD_HEIGHT/2)
{
    const auto* n = get_expression_params(EXPR_IDLE);
    m_current_params = *n; m_target_params = *n;
}

bool FaceRenderer::begin() {
    const bsp_display_config_t cfg = { .max_transfer_sz = LCD_WIDTH * 80 * 2 };
    esp_lcd_panel_io_handle_t io = NULL;
    esp_err_t err = bsp_display_new(&cfg, &m_panel, &io);
    if (err != ESP_OK) { ESP_LOGE(TAG, "bsp_display_new failed: %d", err); return false; }
    bsp_display_backlight_on();
    err = esp_lcd_panel_disp_on_off(m_panel, true);
    if (err != ESP_OK) ESP_LOGE(TAG, "disp_on_off failed: %d", err);
    ESP_LOGI(TAG, "Display ready via BSP");
    // Fill screen RED using line buffer
    for (int i = 0; i < LCD_WIDTH; i++) m_line_buf[i] = 0xF800;
    for (int y = 0; y < LCD_HEIGHT; y++) {
        err = esp_lcd_panel_draw_bitmap(m_panel, 0, y, LCD_WIDTH, y+1, m_line_buf);
        if (err != ESP_OK) { ESP_LOGE(TAG, "red line %d failed: %d", y, err); break; }
    }
    if (err == ESP_OK) ESP_LOGI(TAG, "RED fill OK");
    vTaskDelay(pdMS_TO_TICKS(1000));
    set_expression(EXPR_IDLE);
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
    for (int i = 0; i < LCD_WIDTH; i++) m_line_buf[i] = color;
    for (int y = 0; y < LCD_HEIGHT; y++)
        esp_lcd_panel_draw_bitmap(m_panel, 0, y, LCD_WIDTH, y+1, m_line_buf);
}

void FaceRenderer::render() {
    if (!m_panel) return;
    esp_err_t err;
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) m_line_buf[x] = 0;
        int fy = y - m_cy;
        if (fy >= -95 && fy <= 95) {
            for (int x = 0; x < LCD_WIDTH; x++) {
                float rx = (x-m_cx)/85.0f, ry = fy/95.0f;
                if (rx*rx + ry*ry <= 1.0f) m_line_buf[x] = 0xFFDC;
            }
        }
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
        err = esp_lcd_panel_draw_bitmap(m_panel, 0, y, LCD_WIDTH, y+1, m_line_buf);
        if (err != ESP_OK) { ESP_LOGE(TAG, "render line %d err %d", y, err); break; }
    }
    if (err == ESP_OK) ESP_LOGI(TAG, "render OK");
}

void FaceRenderer::draw_eye_line(int fy, int cx, int cy, const expression_params_t& p, bool is_left) {
    if (p.heart_eyes > 0.5f) { draw_heart_eyes_line(fy, cx, cy, p, is_left); return; }
    int ew = 18, eh = (int)(12 * p.eye_height * p.eye_open);
    if (eh < 2) {
        if (fy-cy == 0) for (int x = -ew; x <= ew; x++) {
            int px = m_cx+cx+x; if (px>=0&&px<LCD_WIDTH) m_line_buf[px]=0;
        }
        return;
    }
    int ry = fy-cy; if (abs(ry)>eh) return;
    for (int x=-ew; x<=ew; x++) {
        int px=m_cx+cx+x; if(px<0||px>=LCD_WIDTH) continue;
        if((float)(x*x)/(ew*ew)+(float)(ry*ry)/(eh*eh)<=1.0f) m_line_buf[px]=0xFFFF;
    }
    draw_pupil(m_cx+cx+(int)(p.eye_width*3-1.5f), cy+(int)(p.eye_height*3-1.5f), fy);
}

void FaceRenderer::draw_pupil(int px, int py, int fy) {
    int ry=fy-py; if(abs(ry)>5) return;
    for(int x=-5;x<=5;x++) { if(px+x<0||px+x>=LCD_WIDTH) continue;
        if(x*x+ry*ry<=25) m_line_buf[px+x]=0;
        if(x*x+(ry-2)*(ry-2)<=3) m_line_buf[px+x+2]=0xFFFF;
    }
}

void FaceRenderer::draw_eyebrow_line(int fy, int cx, int cy, const expression_params_t& p, bool is_left) {
    float a = ((p.brow_angle-0.5f)*20.0f)*(is_left?1:-1);
    int by=cy+(int)(p.brow_height*8), ry=fy-by;
    if(abs(ry)>1) return;
    for(int x=-15;x<=15;x++) { int px=m_cx+cx+x; if(px<0||px>=LCD_WIDTH) continue;
        if(abs(ry-(int)(x*a/10.0f))<=1) m_line_buf[px]=0;
    }
}

void FaceRenderer::draw_mouth_line(int fy, int cx, int cy, const expression_params_t& p) {
    int mw=15+(int)(p.mouth_open*5), mh=2+(int)(p.mouth_open*8);
    int yo=(int)((p.mouth_curve-0.5f)*6), ry=fy-(cy+yo);
    if(ry<0||ry>=mh) return;
    for(int x=-mw;x<=mw;x++) { int px=m_cx+cx+x; if(px<0||px>=LCD_WIDTH) continue;
        float nx=x/(float)mw, ny=ry/(float)mh-0.5f;
        if(ny*ny+nx*nx*0.5f<=0.5f) m_line_buf[px]=(ry<mh/2)?0x7800:0xF800;
    }
}

void FaceRenderer::draw_blush_line(int fy, int cx, int cy, const expression_params_t& p) {
    if(p.blush<0.05f) return;
    int br=8+(int)(p.blush*4), ry=fy-cy;
    if(abs(ry)>br) return;
    for(int x=-br;x<=br;x++) { int px=m_cx+cx+x; if(px<0||px>=LCD_WIDTH) continue;
        float d=(x*x+ry*ry)/(float)(br*br);
        if(d<=1.0f) m_line_buf[px]=0xFD20;
    }
}

void FaceRenderer::draw_tears_line(int fy, int cx, int cy, const expression_params_t& p) {
    if(p.tears<0.05f) return;
    int n=(int)(p.tears*4);
    for(int t=0;t<n;t++) { int ty=cy+10+t*6; if(abs(fy-ty)>3) continue;
        int tx=cx+(t-n/2)*3;
        for(int x=-1;x<=1;x++) { int px=m_cx+tx+x; if(px<0||px>=LCD_WIDTH) continue;
            if(abs(fy-ty)<=3) m_line_buf[px]=0x7BEF;
        }
    }
}

void FaceRenderer::draw_heart_eyes_line(int fy, int cx, int cy, const expression_params_t& p, bool is_left) {
    (void)p;(void)is_left; int r=7, ry=fy-cy; if(abs(ry)>r) return;
    for(int x=-r;x<=r;x++) { int px=m_cx+cx+x; if(px<0||px>=LCD_WIDTH) continue;
        float dx=x/(float)r, dy=ry/(float)r, v=(dx*dx+dy*dy-1);
        if(v*v*v-dx*dx*dy*dy*dy<0) m_line_buf[px]=0xF800;
    }
}
