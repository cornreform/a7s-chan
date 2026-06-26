#include "face_renderer.h"
#include <cmath>
#include <cstdio>

// M5Stack CoreS3 pin definitions for display
#define CORE3_LCD_CS     3   // CS
#define CORE3_LCD_SCK    7   // SCLK (SPI)
#define CORE3_LCD_MOSI   6   // MOSI (SPI)
#define CORE3_LCD_DC     2   // DC
#define CORE3_LCD_RST    1   // RST
#define CORE3_LCD_BL     40  // Backlight
#define CORE3_LCD_MISO   8   // MISO (for reading, optional)

// Touch pins (FT6336 on M5Stack CoreS3)
#define CORE3_TOUCH_SDA  12
#define CORE3_TOUCH_SCL  11
#define CORE3_TOUCH_RST  10
#define CORE3_TOUCH_INT  9

FaceRenderer::FaceRenderer()
    : m_current_id(EXPR_IDLE)
    , m_target_id(EXPR_IDLE)
    , m_tweening(false)
    , m_tween_start_ms(0)
    , m_tween_duration_ms(0)
{
    // Initialize default params from idle expression via API
    const expression_params_t* neutral = get_expression_params(EXPR_IDLE);
    m_current_params = *neutral;
    m_target_params = *neutral;
}

FaceRenderer::~FaceRenderer() {
}

bool FaceRenderer::begin() {
    // LovyanGFX auto-detects M5Stack CoreS3
    m_lcd.begin();
    m_lcd.setRotation(1);
    m_lcd.setBrightness(200);
    m_lcd.fillScreen(TFT_BLACK);

    // Initial render
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
    const expression_params_t* params = get_expression_params(target_id);
    m_target_params = *params;
    m_tween_duration_ms = duration_ms;
    m_tween_start_ms = esp_timer_get_time() / 1000; // convert us to ms
    m_tweening = true;
}

void FaceRenderer::set_custom_params(const expression_params_t& params) {
    m_current_params = params;
    m_target_params = params;
    m_current_id = EXPR_COUNT; // custom, not in DB
    m_target_id = EXPR_COUNT;
    m_tweening = false;
    render();
}

void FaceRenderer::update(uint32_t now_ms) {
    if (!m_tweening) return;

    uint32_t elapsed = now_ms - m_tween_start_ms;
    float t = (m_tween_duration_ms > 0)
        ? static_cast<float>(elapsed) / static_cast<float>(m_tween_duration_ms)
        : 1.0f;

    if (t >= 1.0f) {
        t = 1.0f;
        m_tweening = false;
        m_current_id = m_target_id;
    }

    // Lerp between current and target parameters using expressions.h API
    expression_params_t interpolated;
    lerp_expression(&m_current_params, &m_target_params, t, &interpolated);
    m_current_params = interpolated;

    render();
}

void FaceRenderer::render() {
    m_lcd.startWrite();

    // Clear screen
    m_lcd.fillScreen(TFT_BLACK);

    int cx = center_x();
    int cy = center_y();

    // Draw order: blush -> eyes -> pupils/hearts -> eyebrows -> tears -> mouth
    draw_blush(cx, cy, m_current_params);
    draw_eye(cx, cy, m_current_params, true);   // left eye
    draw_eye(cx, cy, m_current_params, false);  // right eye
    draw_heart_eyes(cx, cy, m_current_params, true);
    draw_heart_eyes(cx, cy, m_current_params, false);
    draw_pupil(cx, cy, m_current_params, true);
    draw_pupil(cx, cy, m_current_params, false);
    draw_eyebrow(cx, cy, m_current_params, true);
    draw_eyebrow(cx, cy, m_current_params, false);
    draw_tears(cx, cy, m_current_params);
    draw_mouth(cx, cy, m_current_params);

    m_lcd.endWrite();
}

void FaceRenderer::clear(uint16_t color) {
    m_lcd.fillScreen(color);
}

// ============================================================
// Drawing implementations
// ============================================================

void FaceRenderer::draw_eye(int cx, int cy, const expression_params_t& p, bool is_left) {
    // Eye dimensions driven by expression fields:
    //   eye_open:  0.0=closed, 1.0=fully open
    //   eye_width: 0.0=narrow slit, 1.0=wide
    //   eye_height: 0.0=flat, 1.0=tall oval
    int eye_spacing = 60;
    int base_w = 50;
    int base_h = 30;

    // Compute actual eye dimensions from parameters
    float openness = p.eye_open;           // 0..1 controls how open
    float width_factor = 0.3f + 0.7f * p.eye_width;   // 0.3..1.0
    float height_factor = 0.2f + 0.8f * p.eye_height; // 0.2..1.0

    // When eye_open is near 0, the eye collapses to a slit
    float open_factor = (openness < 0.01f) ? 0.01f : openness;

    int ew = static_cast<int>(base_w * width_factor);
    int eh = static_cast<int>(base_h * height_factor * open_factor);
    if (eh < 2) eh = 2; // minimum height

    // Eye position
    int ex = cx + (is_left ? -eye_spacing : eye_spacing);
    int ey = cy - 20;

    // Draw eye white (sclera) — only if reasonably open
    if (openness > 0.05f) {
        m_lcd.fillEllipse(ex, ey, ew / 2, eh / 2, TFT_WHITE);
    }

    // Draw eye outline
    m_lcd.drawEllipse(ex, ey, ew / 2, eh / 2, TFT_DARKGREY);

    // If nearly closed, draw a horizontal line to indicate closed eye
    if (openness < 0.1f) {
        m_lcd.drawLine(ex - ew / 2, ey, ex + ew / 2, ey, TFT_WHITE);
    }
}

void FaceRenderer::draw_pupil(int cx, int cy, const expression_params_t& p, bool is_left) {
    // If heart_eyes is active, skip normal pupils (hearts drawn separately)
    if (p.heart_eyes > 0.5f) return;

    int eye_spacing = 60;
    int base_w = 50;
    int base_h = 30;

    float openness = p.eye_open;
    if (openness < 0.05f) return;

    float width_factor = 0.3f + 0.7f * p.eye_width;
    float height_factor = 0.2f + 0.8f * p.eye_height;
    float open_factor = (openness < 0.01f) ? 0.01f : openness;

    int ew = static_cast<int>(base_w * width_factor);
    int eh = static_cast<int>(base_h * height_factor * open_factor);
    if (eh < 2) eh = 2;

    int ex = cx + (is_left ? -eye_spacing : eye_spacing);
    int ey = cy - 20;

    // Pupil: fixed size relative to eye, centered
    int pupil_r = static_cast<int>(7.0f * (0.8f + 0.2f * openness));
    if (pupil_r < 2) pupil_r = 2;

    // Centre pupil in the eye
    int px = ex;
    int py = ey;

    // Draw iris ring
    m_lcd.drawCircle(px, py, pupil_r + 3, TFT_DARKGREY);

    // Draw pupil
    m_lcd.fillCircle(px, py, pupil_r, TFT_BLACK);

    // Pupil highlight for liveliness
    int hl_r = pupil_r / 3;
    if (hl_r < 1) hl_r = 1;
    m_lcd.fillCircle(px + pupil_r / 3, py - pupil_r / 3, hl_r, TFT_WHITE);
}

void FaceRenderer::draw_heart_eyes(int cx, int cy, const expression_params_t& p, bool is_left) {
    // heart_eyes: 0.0=normal, 1.0=full heart eyes
    if (p.heart_eyes < 0.5f) return;

    int eye_spacing = 60;
    float openness = p.eye_open;
    if (openness < 0.05f) return;

    int ex = cx + (is_left ? -eye_spacing : eye_spacing);
    int ey = cy - 20;

    int heart_size = static_cast<int>(10 + 8 * p.heart_eyes);

    // Draw a simple heart shape using two circles + triangle
    uint16_t heart_color = rgb565(255, 60, 80);

    // Left lobe
    m_lcd.fillCircle(ex - heart_size / 3, ey - heart_size / 4, heart_size / 3, heart_color);
    // Right lobe
    m_lcd.fillCircle(ex + heart_size / 3, ey - heart_size / 4, heart_size / 3, heart_color);
    // Bottom triangle
    m_lcd.fillTriangle(
        ex - heart_size / 3 - 1, ey - heart_size / 6,
        ex + heart_size / 3 + 1, ey - heart_size / 6,
        ex, ey + heart_size / 2,
        heart_color
    );
}

void FaceRenderer::draw_eyebrow(int cx, int cy, const expression_params_t& p, bool is_left) {
    int eye_spacing = 60;
    int brow_w = 34;
    int brow_h = 5;

    int ex = cx + (is_left ? -eye_spacing : eye_spacing);
    int ey = cy - 55; // above eyes

    // brow_angle: 0.0=neutral, 0.5=up, 1.0=down
    // brow_height: 0.0=low, 1.0=high
    // Map angle: positive = raised (happy), negative = lowered (angry)
    float angle_map = (p.brow_angle - 0.5f) * 2.0f; // -1.0 to +1.0
    // For left eye, mirror the angle direction for expression symmetry
    float angle_deg = angle_map * 20.0f; // ±20 degrees max tilt
    if (is_left) angle_deg = -angle_deg;

    // Height offset: higher brow_height → brows raised
    float height_offset = (p.brow_height - 0.5f) * 20.0f;

    ey += static_cast<int>(height_offset);

    float rad = angle_deg * 3.14159f / 180.0f;

    int x1 = ex - brow_w / 2;
    int y1 = ey;
    int x2 = ex + brow_w / 2;
    int y2 = ey + static_cast<int>(tan(rad) * brow_w);

    // Draw brow as a thick line
    m_lcd.drawLine(x1, y1, x2, y2, TFT_WHITE);
    m_lcd.drawLine(x1, y1 + 1, x2, y2 + 1, TFT_WHITE);
    m_lcd.drawLine(x1, y1 + 2, x2, y2 + 2, TFT_WHITE);
}

void FaceRenderer::draw_mouth(int cx, int cy, const expression_params_t& p) {
    // Mouth centered below eyes
    int mx = cx;
    int my = cy + 25;

    // mouth_open: 0.0=closed, 1.0=wide open
    // mouth_curve: 0.0=frown, 0.5=neutral, 1.0=smile
    float openness = p.mouth_open;
    float curve = p.mouth_curve; // 0..1, 0.5 is neutral

    int mw = 44;
    int mh = static_cast<int>(8 + 24 * openness); // grows with openness
    if (mh < 2) mh = 2;

    // Map curve to ± amplitude: 0→frown, 0.5→flat, 1→smile
    float curve_amp = (curve - 0.5f) * 2.0f * 8.0f; // ±8 pixels

    if (openness > 0.3f) {
        // Open mouth — draw as ellipse
        m_lcd.fillEllipse(mx, my, mw / 2, mh / 2, TFT_WHITE);
        m_lcd.drawEllipse(mx, my, mw / 2, mh / 2, TFT_WHITE);

        // Inner mouth (dark)
        if (openness > 0.4f) {
            int inner_h = static_cast<int>(mh * 0.45f);
            m_lcd.fillEllipse(mx, my + 1, mw / 2 - 3, inner_h / 2, rgb565(80, 20, 20));
        }

        // Slight curve at corners
        int corner_y = my + static_cast<int>(curve_amp * 0.3f);
        m_lcd.fillCircle(mx - mw / 2, corner_y, 2, TFT_WHITE);
        m_lcd.fillCircle(mx + mw / 2, corner_y, 2, TFT_WHITE);
    } else {
        // Closed or slightly open mouth — draw as an arc
        int segments = 16;
        int prev_x = mx - mw / 2;
        int prev_y = my + static_cast<int>(curve_amp * 0.3f);

        for (int i = 1; i <= segments; i++) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float angle = t * 3.14159f; // 0 to PI
            float x_offset = (mw / 2) * cos(angle - 3.14159f / 2);
            // Arc shape: curve_amp at center, flat at edges
            float y_offset = curve_amp * sin(angle);

            int sx = mx + static_cast<int>(x_offset);
            int sy = my + static_cast<int>(y_offset);

            m_lcd.drawLine(prev_x, prev_y, sx, sy, TFT_WHITE);
            prev_x = sx;
            prev_y = sy;
        }

        // Horizontal base line for more definition
        m_lcd.drawLine(mx - mw / 2, my, mx + mw / 2, my, TFT_WHITE);

        // Corners
        int corner_y = my + static_cast<int>(curve_amp * 0.3f);
        m_lcd.fillCircle(mx - mw / 2, corner_y, 2, TFT_WHITE);
        m_lcd.fillCircle(mx + mw / 2, corner_y, 2, TFT_WHITE);
    }
}

void FaceRenderer::draw_blush(int cx, int cy, const expression_params_t& p) {
    // blush: 0.0=none, 1.0=full blush
    if (p.blush < 0.01f) return;

    int intensity = static_cast<int>(p.blush * 40);
    uint16_t blush_color = rgb565(
        255,
        180 - intensity,
        180 - intensity
    );

    // Blush circles below and to the sides of each eye
    int left_bx = cx - 44;
    int right_bx = cx + 44;
    int by = cy + 2;
    int br = 14 + static_cast<int>(p.blush * 6);

    m_lcd.fillEllipse(left_bx, by, br, br / 2, blush_color);
    m_lcd.fillEllipse(right_bx, by, br, br / 2, blush_color);
}

void FaceRenderer::draw_tears(int cx, int cy, const expression_params_t& p) {
    // tears: 0.0=none, 1.0=full tears
    if (p.tears < 0.01f) return;

    // Number of tear drops scales with intensity
    int tear_count = static_cast<int>(p.tears * 5);
    if (tear_count < 1) tear_count = 1;

    uint16_t tear_color = rgb565(150, 180, 255);

    int eye_spacing = 60;
    int base_y = cy - 15;

    for (int i = 0; i < tear_count && i < 6; i++) {
        int tx_left = cx - eye_spacing - 3 + (i * 2);
        int tx_right = cx + eye_spacing - 3 + (i * 2);
        int ty = base_y + 18 + (i * 8);

        // Small teardrop shapes
        m_lcd.fillCircle(tx_left, ty, 2, tear_color);
        m_lcd.fillCircle(tx_right, ty, 2, tear_color);

        // Tear streak
        if (i > 0) {
            m_lcd.drawLine(tx_left, ty - 3, tx_left, ty + 1, tear_color);
            m_lcd.drawLine(tx_right, ty - 3, tx_right, ty + 1, tear_color);
        }
    }
}

// ============================================================
// Color helpers
// ============================================================

uint16_t FaceRenderer::rgb565(uint8_t r, uint8_t g, uint8_t b) const {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

uint16_t FaceRenderer::lerp_color(uint16_t c1, uint16_t c2, float t) const {
    // Extract R,G,B from 565 format
    uint8_t r1 = (c1 >> 11) & 0x1F;
    uint8_t g1 = (c1 >> 5) & 0x3F;
    uint8_t b1 = c1 & 0x1F;

    uint8_t r2 = (c2 >> 11) & 0x1F;
    uint8_t g2 = (c2 >> 5) & 0x3F;
    uint8_t b2 = c2 & 0x1F;

    uint8_t r = static_cast<uint8_t>(r1 + (r2 - r1) * t);
    uint8_t g = static_cast<uint8_t>(g1 + (g2 - g1) * t);
    uint8_t b = static_cast<uint8_t>(b1 + (b2 - b1) * t);

    return (r << 11) | (g << 5) | b;
}
