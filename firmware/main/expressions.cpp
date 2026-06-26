#include "expressions.h"
#include <cstring>

// ---- Preset definitions ----

// Neutral
static const expression_t EXPR_NEUTRAL_VAL = {
    .eye_open = 1.0f,
    .eye_height = 0.5f,
    .eye_width = 0.5f,
    .pupil_x = 0.0f,
    .pupil_y = 0.0f,
    .brow_angle = 0.0f,
    .brow_height = 0.0f,
    .mouth_open = 0.0f,
    .mouth_curve = 0.0f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Happy
static const expression_t EXPR_HAPPY_VAL = {
    .eye_open = 0.9f,
    .eye_height = 0.4f,
    .eye_width = 0.6f,
    .pupil_x = 0.0f,
    .pupil_y = 0.2f,
    .brow_angle = -0.3f,
    .brow_height = 0.2f,
    .mouth_open = 0.3f,
    .mouth_curve = 0.8f,
    .blush = 0.2f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Sad
static const expression_t EXPR_SAD_VAL = {
    .eye_open = 0.7f,
    .eye_height = 0.3f,
    .eye_width = 0.4f,
    .pupil_x = 0.0f,
    .pupil_y = -0.3f,
    .brow_angle = 0.5f,
    .brow_height = -0.2f,
    .mouth_open = 0.1f,
    .mouth_curve = -0.7f,
    .blush = 0.0f,
    .tear = 0.3f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Angry
static const expression_t EXPR_ANGRY_VAL = {
    .eye_open = 0.8f,
    .eye_height = 0.3f,
    .eye_width = 0.5f,
    .pupil_x = 0.0f,
    .pupil_y = 0.0f,
    .brow_angle = -0.8f,
    .brow_height = -0.5f,
    .mouth_open = 0.2f,
    .mouth_curve = -0.5f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Surprised
static const expression_t EXPR_SURPRISED_VAL = {
    .eye_open = 1.0f,
    .eye_height = 0.9f,
    .eye_width = 0.9f,
    .pupil_x = 0.0f,
    .pupil_y = 0.0f,
    .brow_angle = 0.0f,
    .brow_height = 0.9f,
    .mouth_open = 0.7f,
    .mouth_curve = -0.3f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.8f,
    .blink_intensity = 0.0f
};

// Fearful
static const expression_t EXPR_FEARFUL_VAL = {
    .eye_open = 0.9f,
    .eye_height = 0.7f,
    .eye_width = 0.7f,
    .pupil_x = 0.0f,
    .pupil_y = 0.2f,
    .brow_angle = 0.7f,
    .brow_height = 0.5f,
    .mouth_open = 0.4f,
    .mouth_curve = -0.4f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.3f,
    .blink_intensity = 0.0f
};

// Disgusted
static const expression_t EXPR_DISGUSTED_VAL = {
    .eye_open = 0.5f,
    .eye_height = 0.3f,
    .eye_width = 0.3f,
    .pupil_x = 0.0f,
    .pupil_y = -0.2f,
    .brow_angle = -0.4f,
    .brow_height = -0.3f,
    .mouth_open = 0.2f,
    .mouth_curve = -0.6f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Love (heart eyes)
static const expression_t EXPR_LOVE_VAL = {
    .eye_open = 0.8f,
    .eye_height = 0.5f,
    .eye_width = 0.5f,
    .pupil_x = 0.0f,
    .pupil_y = 0.1f,
    .brow_angle = -0.2f,
    .brow_height = 0.1f,
    .mouth_open = 0.2f,
    .mouth_curve = 0.9f,
    .blush = 0.8f,
    .tear = 0.0f,
    .heart_eyes = 1.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Crying
static const expression_t EXPR_CRYING_VAL = {
    .eye_open = 0.6f,
    .eye_height = 0.3f,
    .eye_width = 0.4f,
    .pupil_x = 0.0f,
    .pupil_y = -0.2f,
    .brow_angle = 0.4f,
    .brow_height = -0.1f,
    .mouth_open = 0.3f,
    .mouth_curve = -0.8f,
    .blush = 0.0f,
    .tear = 1.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Annoyed
static const expression_t EXPR_ANNOYED_VAL = {
    .eye_open = 0.6f,
    .eye_height = 0.4f,
    .eye_width = 0.4f,
    .pupil_x = -0.3f,
    .pupil_y = 0.0f,
    .brow_angle = -0.3f,
    .brow_height = -0.3f,
    .mouth_open = 0.0f,
    .mouth_curve = -0.3f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Sleepy
static const expression_t EXPR_SLEEPY_VAL = {
    .eye_open = 0.2f,
    .eye_height = 0.2f,
    .eye_width = 0.3f,
    .pupil_x = 0.0f,
    .pupil_y = -0.4f,
    .brow_angle = 0.0f,
    .brow_height = -0.5f,
    .mouth_open = 0.0f,
    .mouth_curve = 0.0f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.6f
};

// Confused
static const expression_t EXPR_CONFUSED_VAL = {
    .eye_open = 0.7f,
    .eye_height = 0.5f,
    .eye_width = 0.5f,
    .pupil_x = 0.2f,
    .pupil_y = 0.1f,
    .brow_angle = 0.6f,   // one brow up
    .brow_height = 0.3f,
    .mouth_open = 0.1f,
    .mouth_curve = -0.2f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Excited
static const expression_t EXPR_EXCITED_VAL = {
    .eye_open = 1.0f,
    .eye_height = 0.8f,
    .eye_width = 0.8f,
    .pupil_x = 0.0f,
    .pupil_y = 0.3f,
    .brow_angle = -0.4f,
    .brow_height = 0.7f,
    .mouth_open = 0.5f,
    .mouth_curve = 0.9f,
    .blush = 0.1f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.5f,
    .blink_intensity = 0.0f
};

// Embarrassed
static const expression_t EXPR_EMBARRASSED_VAL = {
    .eye_open = 0.5f,
    .eye_height = 0.3f,
    .eye_width = 0.3f,
    .pupil_x = -0.2f,
    .pupil_y = -0.3f,
    .brow_angle = 0.2f,
    .brow_height = -0.1f,
    .mouth_open = 0.2f,
    .mouth_curve = 0.1f,
    .blush = 1.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Laughing
static const expression_t EXPR_LAUGHING_VAL = {
    .eye_open = 0.6f,
    .eye_height = 0.3f,
    .eye_width = 0.3f,
    .pupil_x = 0.0f,
    .pupil_y = 0.0f,
    .brow_angle = -0.3f,
    .brow_height = 0.1f,
    .mouth_open = 0.9f,
    .mouth_curve = 1.0f,
    .blush = 0.3f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Wink
static const expression_t EXPR_WINK_VAL = {
    .eye_open = 0.9f,
    .eye_height = 0.5f,
    .eye_width = 0.5f,
    .pupil_x = 0.0f,
    .pupil_y = 0.0f,
    .brow_angle = 0.1f,
    .brow_height = 0.2f,
    .mouth_open = 0.1f,
    .mouth_curve = 0.6f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Shock
static const expression_t EXPR_SHOCK_VAL = {
    .eye_open = 1.0f,
    .eye_height = 1.0f,
    .eye_width = 1.0f,
    .pupil_x = 0.0f,
    .pupil_y = 0.0f,
    .brow_angle = -0.2f,
    .brow_height = 1.0f,
    .mouth_open = 0.8f,
    .mouth_curve = 0.0f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 1.0f,
    .blink_intensity = 0.0f
};

// Thinking
static const expression_t EXPR_THINKING_VAL = {
    .eye_open = 0.7f,
    .eye_height = 0.5f,
    .eye_width = 0.5f,
    .pupil_x = 0.4f,     // looking to the side
    .pupil_y = 0.2f,
    .brow_angle = 0.5f,
    .brow_height = 0.2f,
    .mouth_open = 0.0f,
    .mouth_curve = -0.2f,
    .blush = 0.0f,
    .tear = 0.0f,
    .heart_eyes = 0.0f,
    .exclamation = 0.0f,
    .blink_intensity = 0.0f
};

// Array of all presets
static const expression_t* s_presets[EXPR_COUNT] = {
    &EXPR_NEUTRAL_VAL,
    &EXPR_HAPPY_VAL,
    &EXPR_SAD_VAL,
    &EXPR_ANGRY_VAL,
    &EXPR_SURPRISED_VAL,
    &EXPR_FEARFUL_VAL,
    &EXPR_DISGUSTED_VAL,
    &EXPR_LOVE_VAL,
    &EXPR_CRYING_VAL,
    &EXPR_ANNOYED_VAL,
    &EXPR_SLEEPY_VAL,
    &EXPR_CONFUSED_VAL,
    &EXPR_EXCITED_VAL,
    &EXPR_EMBARRASSED_VAL,
    &EXPR_LAUGHING_VAL,
    &EXPR_WINK_VAL,
    &EXPR_SHOCK_VAL,
    &EXPR_THINKING_VAL
};

const expression_t& expression_get(expression_name_t name) {
    if (name >= EXPR_COUNT) name = EXPR_NEUTRAL;
    return *s_presets[name];
}

expression_t expression_lerp(const expression_t& a, const expression_t& b, float t) {
    // Clamp t
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float inv = 1.0f - t;

    expression_t result;
    result.eye_open       = a.eye_open       * inv + b.eye_open       * t;
    result.eye_height     = a.eye_height     * inv + b.eye_height     * t;
    result.eye_width      = a.eye_width      * inv + b.eye_width      * t;
    result.pupil_x        = a.pupil_x        * inv + b.pupil_x        * t;
    result.pupil_y        = a.pupil_y        * inv + b.pupil_y        * t;
    result.brow_angle     = a.brow_angle     * inv + b.brow_angle     * t;
    result.brow_height    = a.brow_height    * inv + b.brow_height    * t;
    result.mouth_open     = a.mouth_open     * inv + b.mouth_open     * t;
    result.mouth_curve    = a.mouth_curve    * inv + b.mouth_curve    * t;
    result.blush          = a.blush          * inv + b.blush          * t;
    result.tear           = a.tear           * inv + b.tear           * t;
    result.heart_eyes     = a.heart_eyes     * inv + b.heart_eyes     * t;
    result.exclamation    = a.exclamation    * inv + b.exclamation    * t;
    result.blink_intensity = a.blink_intensity * inv + b.blink_intensity * t;
    return result;
}

void expression_set_all(expression_t& expr, float value) {
    expr.eye_open = value;
    expr.eye_height = value;
    expr.eye_width = value;
    expr.pupil_x = value;
    expr.pupil_y = value;
    expr.brow_angle = value;
    expr.brow_height = value;
    expr.mouth_open = value;
    expr.mouth_curve = value;
    expr.blush = value;
    expr.tear = value;
    expr.heart_eyes = value;
    expr.exclamation = value;
    expr.blink_intensity = value;
}

void expression_reset(expression_t& expr) {
    expr = EXPR_NEUTRAL_VAL;
}
