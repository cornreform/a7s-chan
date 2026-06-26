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
    : m_current_id(EXPR_NEUTRAL)
    , m_target_id(EXPR_NEUTRAL)
    , m_tweening(false)
    , m_tween_start_ms(0)
    , m_tween_duration_ms(0)
{
    // Initialize default params from neutral expression
    m_current_params = EXPRESSION_DB[EXPR_NEUTRAL];
    m_target_params = EXPRESSION_DB[EXPR_NEUTRAL];
}

FaceRenderer::~FaceRenderer() {
}

bool FaceRenderer::begin() {
    // Initialize LovyanGFX display for M5Stack CoreS3
    // SPI display config
    auto cfg = m_lcd.getPanel()->board().m5stack;
    cfg.backlight_level = 128; // Default brightness
    cfg.pin_cs   = CORE3_LCD_CS;
    cfg.pin_mosi = CORE3_LCD_MOSI;
    cfg.pin_sclk = CORE3_LCD_SCK;
    cfg.pin_dc   = CORE3_LCD_DC;
    cfg.pin_rst  = CORE3_LCD_RST;
    cfg.pin_bl   = CORE3_LCD_BL;

    m_lcd.begin();
    m_lcd.setRotation(1); // Landscape orientation
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
    m_current_params = EXPRESSION_DB[expr_id];
    m_target_params = EXPRESSION_DB[expr_id];
    m_tweening = false;

    render();
}

bool FaceRenderer::set_expression_by_name(const char* name) {
    expression_id_t id = expression_find_by_name(name);
    if (id >= EXPR_COUNT) return false;
    set_expression(id);
    return true;
}

void FaceRenderer::tween_to(expression_id_t target_id, uint32_t duration_ms) {
    if (target_id >= EXPR_COUNT) return;

    m_target_id = target_id;
    m_target_params = EXPRESSION_DB[target_id];
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

    // Lerp between current and target parameters
    expression_params_t interpolated;
    expression_lerp(m_current_params, m_target_params, t, interpolated);
    m_current_params = interpolated;

    render();
}

void FaceRenderer::render() {
    m_lcd.startWrite();

    // Clear screen
    m_lcd.fillScreen(TFT_BLACK);

    int cx = center_x();
    int cy = center_y();

    // Draw order: blush -> eyes -> eyebrows -> pupils -> tears/sweat/sparkle -> mouth
    draw_blush(cx, cy, m_current_params);
    draw_eye(cx, cy, m_current_params, true);   // left eye
    draw_eye(cx, cy, m_current_params, false);  // right eye
    draw_eyebrow(cx, cy, m_current_params, true);
    draw_eyebrow(cx, cy, m_current_params, false);
    draw_pupil(cx, cy, m_current_params, true);
    draw_pupil(cx, cy, m_current_params, false);
    draw_mouth(cx, cy, m_current_params);
    draw_tear(cx, cy, m_current_params);
    draw_sweatdrop(cx, cy, m_current_params);
    draw_sparkle(cx, cy, m_current_params);

    m_lcd.endWrite();
}

void FaceRenderer::clear(uint16_t color) {
    m_lcd.fillScreen(color);
}

// ============================================================
// Drawing implementations
// ============================================================

void FaceRenderer::draw_eye(int cx, int cy, const expression_params_t& p, bool is_left) {
    // Eye dimensions
    int eye_spacing = 60;
    int eye_base_w = 50;
    int eye_base_h = 30;

    float openness = (1.0f - p.eyelid_weight * 0.6f) * p.eye_openness;

    // Position
    int ex = cx + (is_left ? -eye_spacing : eye_spacing) + static_cast<int>(p.eye_offset_x * 15);
    int ey = cy - 20 + static_cast<int>(p.eye_offset_y * 12);

    int ew = static_cast<int>(eye_base_w * p.eye_aspect_ratio);
    int eh = static_cast<int>(eye_base_h * openness);
    if (eh < 2) eh = 2; // minimum eye height

    // Eye rotation
    float rot = p.eye_rotation * (is_left ? 1.0f : -1.0f);

    // Draw eye white (sclera)
    m_lcd.fillEllipse(ex, ey, ew / 2, eh / 2, TFT_WHITE);

    // Draw upper eyelid (covers top part if eyelid_weight > 0)
    if (p.eyelid_weight > 0.01f) {
        int lid_h = static_cast<int>(eh * 0.5f * p.eyelid_weight);
        m_lcd.fillRect(ex - ew / 2, ey - eh / 2, ew, lid_h, TFT_BLACK);
    }

    // Draw lower eyelid / eyebags
    if (p.eyebag_weight > 0.01f) {
        int bag_h = static_cast<int>(8 * p.eyebag_weight);
        uint16_t bag_color = rgb565(180, 150, 150);
        m_lcd.fillEllipse(ex, ey + eh / 2 + bag_h / 2, ew / 2 + 2, bag_h / 2, bag_color);
    }

    // Draw eye outline
    m_lcd.drawEllipse(ex, ey, ew / 2, eh / 2, TFT_DARKGREY);
}

void FaceRenderer::draw_pupil(int cx, int cy, const expression_params_t& p, bool is_left) {
    int eye_spacing = 60;
    int eye_base_w = 50;
    int eye_base_h = 30;

    float openness = (1.0f - p.eyelid_weight * 0.6f) * p.eye_openness;

    int ex = cx + (is_left ? -eye_spacing : eye_spacing) + static_cast<int>(p.eye_offset_x * 15);
    int ey = cy - 20 + static_cast<int>(p.eye_offset_y * 12);

    int ew = static_cast<int>(eye_base_w * p.eye_aspect_ratio);
    int eh = static_cast<int>(eye_base_h * openness);
    if (eh < 2) eh = 2;

    // Pupil position within eye
    int pupil_r = static_cast<int>(8 * p.eye_pupil_size);
    if (pupil_r < 2) pupil_r = 2;

    int px = ex + static_cast<int>(p.eye_pupil_x * (ew / 2 - pupil_r - 2));
    int py = ey + static_cast<int>(p.eye_pupil_y * (eh / 2 - pupil_r - 2));

    if (p.eye_openness > 0.05f) {
        // Draw pupil
        m_lcd.fillCircle(px, py, pupil_r, TFT_BLACK);

        // Pupil highlight (small white dot for liveliness)
        int hl_r = pupil_r / 3;
        if (hl_r < 1) hl_r = 1;
        m_lcd.fillCircle(px + pupil_r / 3, py - pupil_r / 3, hl_r, TFT_WHITE);

        // Iris ring (slightly larger, dark grey)
        m_lcd.drawCircle(px, py, pupil_r + 2, TFT_DARKGREY);
    }
}

void FaceRenderer::draw_eyebrow(int cx, int cy, const expression_params_t& p, bool is_left) {
    int eye_spacing = 60;
    int brow_w = 32;
    int brow_h = 6;

    int ex = cx + (is_left ? -eye_spacing : eye_spacing);
    int ey = cy - 55; // above eyes

    float height = is_left ? p.brow_height_left : p.brow_height_right;
    float angle = is_left ? p.brow_angle_left : p.brow_angle_right;

    // Apply height offset
    ey += static_cast<int>(height * 15);

    // Rotation: angle in degrees, convert to radians
    float rad = angle * 3.14159f / 180.0f;

    // Draw brow as a thick angled line
    int x1 = ex - brow_w / 2;
    int y1 = ey;
    int x2 = ex + brow_w / 2;
    int y2 = ey + static_cast<int>(tan(rad) * brow_w);

    m_lcd.drawLine(x1, y1, x2, y2, TFT_WHITE);
    m_lcd.drawLine(x1, y1 + 1, x2, y2 + 1, TFT_WHITE);
    m_lcd.drawLine(x1, y1 + 2, x2, y2 + 2, TFT_WHITE);
}

void FaceRenderer::draw_mouth(int cx, int cy, const expression_params_t& p) {
    // Mouth centered below eyes
    int mx = cx + static_cast<int>(p.mouth_offset_y * 20); // same axis for simplicity
    int my = cy + 25 + static_cast<int>(p.mouth_offset_y * 15);

    float width_factor = p.mouth_width;
    float openness = p.mouth_openness;
    float curve = p.mouth_curve;
    float aspect = p.mouth_aspect_ratio;

    int mw = static_cast<int>(40 * width_factor);
    int mh = static_cast<int>(10 * aspect * (0.2f + openness * 0.8f));

    if (mh < 2) mh = 2;

    // Draw mouth shape based on openness and curve
    if (openness > 0.3f) {
        // Open mouth (ellipse with curve distortion)
        m_lcd.fillEllipse(mx, my, mw / 2, mh / 2, TFT_WHITE);
        m_lcd.drawEllipse(mx, my, mw / 2, mh / 2, TFT_WHITE);

        // Inner mouth (dark)
        if (openness > 0.4f) {
            int inner_h = static_cast<int>(mh * 0.4f);
            m_lcd.fillEllipse(mx, my + 1, mw / 2 - 3, inner_h / 2, rgb565(80, 20, 20));
        }
    } else {
        // Closed or slightly open mouth - draw as curve
        int segments = 12;
        float step = 3.14159f / segments;

        // Curve shape: smile (positive) or frown (negative)
        float curve_amp = curve * 6.0f;

        // Draw as a filled shape
        int prev_x = mx - mw / 2;
        int prev_y = my - curve_amp;

        for (int i = 1; i <= segments * 2; i++) {
            float angle = -3.14159f / 2 + step * i;
            float x_offset = (mw / 2) * cos(angle);
            float y_offset = curve_amp * sin(angle) * 0.5f + curve_amp * 0.3f;

            int sx = mx + static_cast<int>(x_offset);
            int sy = my + static_cast<int>(y_offset);

            // Draw line segments forming the mouth line
            m_lcd.drawLine(prev_x, prev_y, sx, sy, TFT_WHITE);
            prev_x = sx;
            prev_y = sy;
        }

        // Draw center line for more definition
        m_lcd.drawLine(mx - mw / 2, my, mx + mw / 2, my, TFT_WHITE);
    }

    // Mouth corners for smile/frown
    int corner_y_offset = static_cast<int>(curve * 5);
    m_lcd.fillCircle(mx - mw / 2, my + corner_y_offset, 2, TFT_WHITE);
    m_lcd.fillCircle(mx + mw / 2, my + corner_y_offset, 2, TFT_WHITE);
}

void FaceRenderer::draw_blush(int cx, int cy, const expression_params_t& p) {
    if (p.blush_intensity < 0.01f) return;

    int intensity = static_cast<int>(p.blush_intensity * 40);
    int alpha = static_cast<int>(p.blush_intensity * 80);
    if (alpha > 80) alpha = 80;

    uint16_t blush_color = rgb565(
        255,
        180 - intensity,
        180 - intensity
    );

    // Blush circles below each eye
    int left_bx = cx - 40;
    int right_bx = cx + 40;
    int by = cy; // below eyes
    int br = 15 + static_cast<int>(p.blush_intensity * 5);

    m_lcd.fillEllipse(left_bx, by, br, br / 2, blush_color);
    m_lcd.fillEllipse(right_bx, by, br, br / 2, blush_color);
}

void FaceRenderer::draw_tear(int cx, int cy, const expression_params_t& p) {
    if (p.tear_intensity < 0.01f) return;

    int intensity = static_cast<int>(p.tear_intensity * 6);
    uint16_t tear_color = rgb565(150, 180, 255);

    int eye_spacing = 60;
    int base_y = cy - 15;

    for (int i = 0; i < intensity && i < 6; i++) {
        // Tears on both sides
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

void FaceRenderer::draw_sweatdrop(int cx, int cy, const expression_params_t& p) {
    if (p.sweatdrop_intensity < 0.01f) return;

    int sd_x = cx + 60;
    int sd_y = cy - 45;
    int size = static_cast<int>(4 + p.sweatdrop_intensity * 4);

    uint16_t sweat_color = rgb565(150, 200, 255);

    // Small sweatdrop shape (circle + triangle)
    m_lcd.fillCircle(sd_x, sd_y + size / 2, size / 2, sweat_color);
    m_lcd.fillTriangle(
        sd_x - size / 2, sd_y + size / 2,
        sd_x + size / 2, sd_y + size / 2,
        sd_x, sd_y - size / 2,
        sweat_color
    );
}

void FaceRenderer::draw_sparkle(int cx, int cy, const expression_params_t& p) {
    if (p.sparkle_intensity < 0.01f) return;

    int intensity = static_cast<int>(p.sparkle_intensity * 5);
    uint16_t sparkle_color = TFT_WHITE;

    int eye_spacing = 60;

    for (int i = 0; i < intensity && i < 4; i++) {
        int sx = cx + (i % 2 == 0 ? -eye_spacing - 15 : eye_spacing + 15);
        int sy = cy - 30 + (i / 2) * 15;

        // Star/sparkle shape: small circle with 4 rays
        m_lcd.fillCircle(sx, sy, 2, sparkle_color);
        m_lcd.drawLine(sx - 4, sy, sx + 4, sy, sparkle_color);
        m_lcd.drawLine(sx, sy - 4, sx, sy + 4, sparkle_color);
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
