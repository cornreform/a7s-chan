#ifndef A7S_EXPRESSIONS_H
#define A7S_EXPRESSIONS_H

#include <cstdint>
#include <cstring>
#include <cmath>

#ifdef __cplusplus
extern "C" {
#endif

// Expression parameters for the robot face
typedef struct {
    // Eyes
    float eye_open;       // 0.0 (closed) to 1.0 (fully open)
    float eye_height;     // 0.0 (narrow) to 1.0 (tall), overall eye aspect ratio
    float eye_width;      // 0.0 (narrow) to 1.0 (wide), overall eye width

    // Pupils
    float pupil_x;        // -1.0 (left) to 1.0 (right)
    float pupil_y;        // -1.0 (down) to 1.0 (up)

    // Brows
    float brow_angle;     // -1.0 (angry inward) to 1.0 (worried raised outer)
    float brow_height;    // -1.0 (lowered) to 1.0 (raised)

    // Mouth
    float mouth_open;     // 0.0 (closed) to 1.0 (fully open)
    float mouth_curve;    // -1.0 (frown) to 1.0 (smile)

    // Effects
    float blush;          // 0.0 (none) to 1.0 (max blush)
    float tear;           // 0.0 (none) to 1.0 (crying)
    float heart_eyes;     // 0.0 (normal) to 1.0 (heart eyes)
    float exclamation;    // 0.0 (none) to 1.0 (shock/emphasis mark)

    // Meta
    float blink_intensity; // 0.0 (normal) to 1.0 (forced blink override)
} expression_t;

// Named expression presets
typedef enum {
    EXPR_NEUTRAL,
    EXPR_HAPPY,
    EXPR_SAD,
    EXPR_ANGRY,
    EXPR_SURPRISED,
    EXPR_FEARFUL,
    EXPR_DISGUSTED,
    EXPR_LOVE,
    EXPR_CRYING,
    EXPR_ANNOYED,
    EXPR_SLEEPY,
    EXPR_CONFUSED,
    EXPR_EXCITED,
    EXPR_EMBARRASSED,
    EXPR_LAUGHING,
    EXPR_WINK,
    EXPR_SHOCK,
    EXPR_THINKING,
    EXPR_COUNT
} expression_name_t;

// Get a named expression preset
const expression_t& expression_get(expression_name_t name);

// Linearly interpolate between two expressions by factor t (0.0 = a, 1.0 = b)
expression_t expression_lerp(const expression_t& a, const expression_t& b, float t);

// Set all fields to a single value
void expression_set_all(expression_t& expr, float value);

// Reset to neutral defaults
void expression_reset(expression_t& expr);

#ifdef __cplusplus
}
#endif

#endif // A7S_EXPRESSIONS_H
