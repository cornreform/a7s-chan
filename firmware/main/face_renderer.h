#ifndef A7S_FACE_RENDERER_H
#define A7S_FACE_RENDERER_H

#include <cstdint>
#include "expressions.h"

// LovyanGFX includes - user must add component manually
#include <LovyanGFX.hpp>

#ifdef __cplusplus
extern "C" {
#endif

// Face renderer configuration
#define FACE_RENDER_INTERVAL_MS  10    // 100 fps update target
#define AUTO_BLINK_INTERVAL_MS   3000  // Blink every ~3 seconds
#define AUTO_BLINK_DURATION_MS   100   // Blink takes 100ms
#define TWEEN_SPEED              0.15f // Interpolation speed per frame

// Display dimensions (M5Stack CoreS3)
#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240

typedef struct {
    // Current and target expressions
    expression_t current_expr;
    expression_t target_expr;
    
    // Auto-blink state
    bool auto_blink;
    uint32_t last_blink_ms;
    bool blinking;
    float blink_progress;
    
    // Tweening
    bool tween_enabled;
    float tween_speed;
    
    // Display handle
    LGFX_Device* display;
    
    // Frame counter
    uint32_t frame_count;
    
    bool initialized;
} face_renderer_t;

// Initialize face renderer with LovyanGFX display
// The display must already be initialized before calling this
void face_renderer_init(face_renderer_t* renderer, LGFX_Device* display);

// Set target expression (will tween to it)
void face_renderer_set_expression(face_renderer_t* renderer, const expression_t& expr);

// Set expression by preset name (immediately sets target)
void face_renderer_set_preset(face_renderer_t* renderer, expression_name_t name);

// Update and render one frame (call at ~100Hz)
void face_renderer_update(face_renderer_t* renderer);

// Enable/disable auto-blink
void face_renderer_set_auto_blink(face_renderer_t* renderer, bool enabled);

// Force a blink now
void face_renderer_force_blink(face_renderer_t* renderer);

// Enable/disable tweening
void face_renderer_set_tween(face_renderer_t* renderer, bool enabled);

// Get the current rendered expression (after tweening)
const expression_t& face_renderer_get_current(const face_renderer_t* renderer);

#ifdef __cplusplus
}
#endif

#endif // A7S_FACE_RENDERER_H
