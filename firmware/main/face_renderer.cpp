#include "face_renderer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <cstring>

static const char* TAG = "face_renderer";

// Face layout constants
#define FACE_CENTER_X    160
#define FACE_CENTER_Y    120
#define EYE_SPACING      50
#define EYE_WIDTH_BASE   30
#define EYE_HEIGHT_BASE  35
#define PUPIL_RADIUS     6
#define EYE_Y_OFFSET     -20
#define MOUTH_Y_OFFSET   40
#define MOUTH_WIDTH_BASE 40
#define BROW_Y_OFFSET    -55
#define BROW_LENGTH      30
#define BLUSH_RADIUS     15
#define BLUSH_OFFSET_X   38
#define BLUSH_OFFSET_Y   5
#define TEAR_OFFSET_X    15
#define TEAR_SIZE        4

// Color definitions (RGB565)
#define COLOR_WHITE      0xFFFF
#define COLOR_BLACK      0x0000
#define COLOR_SKIN       0xFDD0  // Light peach
#define COLOR_EYE_WHITE  0xFFFF
#define COLOR_PUPIL      0x0000
#define COLOR_BROWS      0x0000
#define COLOR_MOUTH      0x0000
#define COLOR_BLUSH      0xFCB8  // Pink
#define COLOR_TEAR       0x6D9F  // Light blue
#define COLOR_HEART      0xF800  // Red
#define COLOR_EXCLAMATION 0xF800

// Draw a filled circle approximation
static void draw_filled_circle(LGFX_Device* disp, int cx, int cy, int r, uint16_t color) {
    disp->fillCircle(cx, cy, r, color);
}

// Draw an ellipse filled
static void draw_filled_ellipse(LGFX_Device* disp, int cx, int cy, int rx, int ry, uint16_t color) {
    disp->fillEllipse(cx, cy, rx, ry, color);
}

// Draw left eye
static void draw_eye(LGFX_Device* disp, int cx, int cy, const expression_t& expr, bool is_left) {
    float open = expr.eye_open;
    float height = expr.eye_height * 2.0f;   // 0 to 2x
    float width = expr.eye_width * 1.5f + 0.5f;  // 0.5 to 2x
    
    int eye_w = (int)(EYE_WIDTH_BASE * width);
    int eye_h = (int)(EYE_HEIGHT_BASE * height * open);
    
    if (eye_h < 1) eye_h = 1;  // At least a line when "closed"
    
    int eye_x = cx + (is_left ? -EYE_SPACING : EYE_SPACING);
    int eye_y = cy + EYE_Y_OFFSET;
    
    // Draw eye white / sclera
    uint16_t sclera_color = COLOR_EYE_WHITE;
    if (expr.heart_eyes > 0.5f) {
        sclera_color = COLOR_HEART;
    }
    
    draw_filled_ellipse(disp, eye_x, eye_y, eye_w, eye_h, sclera_color);
    
    // Draw pupil (skip if eyes are mostly closed or heart eyes)
    if (open > 0.2f && expr.heart_eyes < 0.5f) {
        int pupil_r = PUPIL_RADIUS;
        int pupil_dx = (int)(expr.pupil_x * (eye_w - pupil_r - 2));
        int pupil_dy = (int)(expr.pupil_y * (eye_h - pupil_r - 2));
        
        draw_filled_circle(disp, eye_x + pupil_dx, eye_y + pupil_dy, pupil_r, COLOR_PUPIL);
    }
    
    // Draw tears
    if (expr.tear > 0.1f) {
        int num_tears = (int)(expr.tear * 3) + 1;
        for (int t = 0; t < num_tears; t++) {
            int tear_x = eye_x + (is_left ? -TEAR_OFFSET_X : TEAR_OFFSET_X) + t * 5;
            int tear_y = eye_y + eye_h + 5 + t * 8;
            int tear_r = (int)(TEAR_SIZE * expr.tear);
            if (tear_r < 1) tear_r = 1;
            draw_filled_circle(disp, tear_x, tear_y, tear_r, COLOR_TEAR);
        }
    }
}

// Draw eyebrows
static void draw_brows(LGFX_Device* disp, int cx, int cy, const expression_t& expr) {
    float angle_rad = expr.brow_angle * 0.5f;  // -0.5 to 0.5 radians
    float height_offset = expr.brow_height * 10.0f;
    
    for (int side = 0; side < 2; side++) {
        bool is_left = (side == 0);
        int brow_x = cx + (is_left ? -EYE_SPACING : EYE_SPACING);
        int brow_y = cy + BROW_Y_OFFSET + (int)height_offset;
        
        // Angle: for left brow, positive angle = outer raised
        // For right brow, negative angle = outer raised
        float sign = is_left ? 1.0f : -1.0f;
        float brow_angle = angle_rad * sign;
        
        int x1 = brow_x - BROW_LENGTH / 2;
        int y1 = brow_y - (int)(BROW_LENGTH * sinf(brow_angle) * 0.5f);
        int x2 = brow_x + BROW_LENGTH / 2;
        int y2 = brow_y + (int)(BROW_LENGTH * sinf(brow_angle) * 0.5f);
        
        disp->drawLine(x1, y1, x2, y2, COLOR_BROWS);
        disp->drawLine(x1, y1 + 1, x2, y2 + 1, COLOR_BROWS);  // Slightly thicker
    }
}

// Draw mouth
static void draw_mouth(LGFX_Device* disp, int cx, int cy, const expression_t& expr) {
    int mouth_y = cy + MOUTH_Y_OFFSET;
    float open = expr.mouth_open;
    float curve = expr.mouth_curve;
    
    if (open < 0.05f) {
        // Closed mouth: draw a curve (smile or frown)
        int h = (int)(curve * 8.0f);  // -8 to +8 pixels
        int mid_x = cx;
        int left_x = cx - MOUTH_WIDTH_BASE / 2;
        int right_x = cx + MOUTH_WIDTH_BASE / 2;
        
        // Draw arc using short lines
        int segments = 10;
        for (int i = 0; i < segments; i++) {
            float t0 = (float)i / segments;
            float t1 = (float)(i + 1) / segments;
            float x0 = left_x + (right_x - left_x) * t0;
            float x1 = left_x + (right_x - left_x) * t1;
            
            // Quadratic bezier y = 4 * curve * t * (1-t)
            float y0 = mouth_y + h * 4.0f * t0 * (1.0f - t0);
            float y1 = mouth_y + h * 4.0f * t1 * (1.0f - t1);
            
            disp->drawLine((int)x0, (int)y0, (int)x1, (int)y1, COLOR_MOUTH);
        }
    } else {
        // Open mouth: draw an ellipse
        int mw = (int)(MOUTH_WIDTH_BASE * 0.8f);
        int mh = (int)(open * 20.0f + 2);
        if (mh < 2) mh = 2;
        
        uint16_t mouth_color = (open > 0.5f) ? 0x0010 : COLOR_MOUTH;  // Dark red when open
        draw_filled_ellipse(disp, cx, mouth_y + mh / 2, mw, mh, mouth_color);
    }
}

// Draw blush
static void draw_blush(LGFX_Device* disp, int cx, int cy, const expression_t& expr) {
    if (expr.blush < 0.05f) return;
    
    int base_alpha = (int)(expr.blush * 127);
    if (base_alpha < 10) return;
    
    for (int side = 0; side < 2; side++) {
        bool is_left = (side == 0);
        int blush_x = cx + (is_left ? -BLUSH_OFFSET_X : BLUSH_OFFSET_X);
        int blush_y = cy + BLUSH_OFFSET_Y;
        
        disp->fillCircle(blush_x, blush_y, BLUSH_RADIUS, COLOR_BLUSH);
        disp->fillCircle(blush_x - 5, blush_y + 3, BLUSH_RADIUS - 3, COLOR_BLUSH);
    }
}

// Draw exclamation/shock mark
static void draw_exclamation(LGFX_Device* disp, int cx, int cy, const expression_t& expr) {
    if (expr.exclamation < 0.1f) return;
    
    int mark_x = cx;
    int mark_y = cy - 60;
    int size = (int)(expr.exclamation * 15) + 5;
    
    // Exclamation mark: vertical rectangle + circle below
    int rect_w = size / 3;
    if (rect_w < 2) rect_w = 2;
    
    // Top bar
    disp->fillRect(mark_x - rect_w / 2, mark_y - size, rect_w, size * 2 / 3, COLOR_EXCLAMATION);
    
    // Bottom dot
    disp->fillCircle(mark_x, mark_y + size / 3, rect_w, COLOR_EXCLAMATION);
}

void face_renderer_init(face_renderer_t* renderer, LGFX_Device* display) {
    memset(renderer, 0, sizeof(face_renderer_t));
    
    renderer->display = display;
    renderer->auto_blink = true;
    renderer->tween_enabled = true;
    renderer->tween_speed = TWEEN_SPEED;
    renderer->last_blink_ms = 0;
    renderer->blinking = false;
    renderer->frame_count = 0;
    
    // Initialize expression
    expression_reset(renderer->current_expr);
    expression_reset(renderer->target_expr);
    
    renderer->initialized = true;
    
    ESP_LOGI(TAG, "Face renderer initialized (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void face_renderer_set_expression(face_renderer_t* renderer, const expression_t& expr) {
    renderer->target_expr = expr;
    if (!renderer->tween_enabled) {
        renderer->current_expr = expr;
    }
}

void face_renderer_set_preset(face_renderer_t* renderer, expression_name_t name) {
    face_renderer_set_expression(renderer, expression_get(name));
}

void face_renderer_update(face_renderer_t* renderer) {
    if (!renderer->initialized || !renderer->display) return;
    
    renderer->frame_count++;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    
    LGFX_Device* disp = renderer->display;
    
    // ---- Auto-blink logic ----
    if (renderer->auto_blink && !renderer->blinking) {
        if (now_ms - renderer->last_blink_ms >= AUTO_BLINK_INTERVAL_MS) {
            renderer->blinking = true;
            renderer->blink_progress = 0.0f;
            renderer->last_blink_ms = now_ms;
        }
    }
    
    if (renderer->blinking) {
        float elapsed = (float)(now_ms - renderer->last_blink_ms);
        renderer->blink_progress = elapsed / AUTO_BLINK_DURATION_MS;
        if (renderer->blink_progress >= 1.0f) {
            renderer->blinking = false;
            renderer->blink_progress = 0.0f;
        }
    }
    
    // ---- Expression tweening ----
    if (renderer->tween_enabled) {
        // Check if we need to interpolate
        expression_t diff;
        diff.eye_open = renderer->target_expr.eye_open - renderer->current_expr.eye_open;
        diff.eye_height = renderer->target_expr.eye_height - renderer->current_expr.eye_height;
        diff.eye_width = renderer->target_expr.eye_width - renderer->current_expr.eye_width;
        diff.pupil_x = renderer->target_expr.pupil_x - renderer->current_expr.pupil_x;
        diff.pupil_y = renderer->target_expr.pupil_y - renderer->current_expr.pupil_y;
        diff.brow_angle = renderer->target_expr.brow_angle - renderer->current_expr.brow_angle;
        diff.brow_height = renderer->target_expr.brow_height - renderer->current_expr.brow_height;
        diff.mouth_open = renderer->target_expr.mouth_open - renderer->current_expr.mouth_open;
        diff.mouth_curve = renderer->target_expr.mouth_curve - renderer->current_expr.mouth_curve;
        diff.blush = renderer->target_expr.blush - renderer->current_expr.blush;
        diff.tear = renderer->target_expr.tear - renderer->current_expr.tear;
        diff.heart_eyes = renderer->target_expr.heart_eyes - renderer->current_expr.heart_eyes;
        diff.exclamation = renderer->target_expr.exclamation - renderer->current_expr.exclamation;
        
        // Apply tween speed if difference is significant
        float speed = renderer->tween_speed;
        renderer->current_expr = expression_lerp(renderer->current_expr, renderer->target_expr, speed);
        
        // Snap if very close
        if (fabsf(diff.eye_open) < 0.001f &&
            fabsf(diff.eye_height) < 0.001f &&
            fabsf(diff.eye_width) < 0.001f &&
            fabsf(diff.pupil_x) < 0.001f &&
            fabsf(diff.pupil_y) < 0.001f) {
            renderer->current_expr = renderer->target_expr;
        }
    }
    
    // Apply blink to render expression
    expression_t render_expr = renderer->current_expr;
    if (renderer->blinking) {
        float blink_val;
        if (renderer->blink_progress < 0.5f) {
            blink_val = renderer->blink_progress * 2.0f;  // Closing
        } else {
            blink_val = (1.0f - renderer->blink_progress) * 2.0f;  // Opening
        }
        if (blink_val > 1.0f) blink_val = 1.0f;
        render_expr.eye_open = render_expr.eye_open * (1.0f - blink_val);
        render_expr.eye_height = render_expr.eye_height * (1.0f - blink_val * 0.5f);
    }
    
    // ---- Render face ----
    disp->startWrite();
    
    // Clear screen
    disp->fillScreen(COLOR_SKIN);
    
    // Draw order: blush -> eyes -> brows -> mouth -> tears -> effects
    draw_blush(disp, FACE_CENTER_X, FACE_CENTER_Y, render_expr);
    draw_eye(disp, FACE_CENTER_X, FACE_CENTER_Y, render_expr, true);   // Left eye
    draw_eye(disp, FACE_CENTER_X, FACE_CENTER_Y, render_expr, false);  // Right eye
    draw_brows(disp, FACE_CENTER_X, FACE_CENTER_Y, render_expr);
    draw_mouth(disp, FACE_CENTER_X, FACE_CENTER_Y, render_expr);
    draw_exclamation(disp, FACE_CENTER_X, FACE_CENTER_Y, render_expr);
    
    disp->endWrite();
}

void face_renderer_set_auto_blink(face_renderer_t* renderer, bool enabled) {
    renderer->auto_blink = enabled;
    if (!enabled) {
        renderer->blinking = false;
    }
}

void face_renderer_force_blink(face_renderer_t* renderer) {
    if (renderer->auto_blink) {
        renderer->blinking = true;
        renderer->blink_progress = 0.0f;
        renderer->last_blink_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
}

void face_renderer_set_tween(face_renderer_t* renderer, bool enabled) {
    renderer->tween_enabled = enabled;
    if (!enabled) {
        renderer->current_expr = renderer->target_expr;
    }
}

const expression_t& face_renderer_get_current(const face_renderer_t* renderer) {
    return renderer->current_expr;
}
