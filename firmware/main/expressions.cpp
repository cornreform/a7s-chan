#include "expressions.h"
#include <cstring>
#include <cctype>

// ──────────────────────────────────────────────
// Expression parameter table — 18 expressions
// Fields: eye_open, eye_width, eye_height,
//         brow_angle(0=neutral,0.5=up,1=down),
//         brow_height, mouth_open, mouth_curve(0=frown,0.5=neutral,1=smile),
//         blush, tears, heart_eyes
// ──────────────────────────────────────────────

static const expression_params_t s_expressions[EXPR_COUNT] = {
    // EXPR_HAPPY    open  width height browA browH mouthO mouthC blush tears heart
    { 0.85f, 0.80f, 0.70f, 0.30f, 0.65f, 0.20f, 1.00f, 0.10f, 0.00f, 0.00f },
    // EXPR_SAD
    { 0.60f, 0.60f, 0.50f, 0.80f, 0.30f, 0.05f, 0.10f, 0.10f, 0.30f, 0.00f },
    // EXPR_ANGRY
    { 0.70f, 0.50f, 0.50f, 0.90f, 0.40f, 0.10f, 0.15f, 0.00f, 0.00f, 0.00f },
    // EXPR_SURPRISE
    { 0.95f, 0.95f, 0.90f, 0.10f, 0.85f, 0.80f, 0.50f, 0.00f, 0.00f, 0.00f },
    // EXPR_BLINK
    { 0.00f, 0.70f, 0.10f, 0.45f, 0.50f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f },
    // EXPR_IDLE
    { 0.85f, 0.75f, 0.70f, 0.45f, 0.50f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f },
    // EXPR_TALK   (mouth animated dynamically; base params)
    { 0.80f, 0.75f, 0.70f, 0.45f, 0.50f, 0.40f, 0.55f, 0.00f, 0.00f, 0.00f },
    // EXPR_LISTEN
    { 0.85f, 0.80f, 0.75f, 0.40f, 0.55f, 0.05f, 0.50f, 0.00f, 0.00f, 0.00f },
    // EXPR_THINK
    { 0.75f, 0.65f, 0.60f, 0.20f, 0.75f, 0.05f, 0.40f, 0.00f, 0.00f, 0.00f },
    // EXPR_SLEEP
    { 0.00f, 0.60f, 0.10f, 0.45f, 0.45f, 0.00f, 0.45f, 0.00f, 0.00f, 0.00f },
    // EXPR_WAKE   (halfway between sleep and idle)
    { 0.40f, 0.70f, 0.40f, 0.45f, 0.50f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f },
    // EXPR_CONFUSED
    { 0.70f, 0.65f, 0.60f, 0.60f, 0.50f, 0.10f, 0.35f, 0.00f, 0.00f, 0.00f },
    // EXPR_EXCITED
    { 0.95f, 0.90f, 0.85f, 0.15f, 0.80f, 0.50f, 0.95f, 0.20f, 0.00f, 0.00f },
    // EXPR_DANCE
    { 0.85f, 0.80f, 0.70f, 0.30f, 0.65f, 0.30f, 0.90f, 0.10f, 0.00f, 0.00f },
    // EXPR_LOVE
    { 0.70f, 0.70f, 0.70f, 0.30f, 0.60f, 0.15f, 0.90f, 0.60f, 0.00f, 1.00f },
    // EXPR_WINK
    { 0.50f, 0.70f, 0.70f, 0.40f, 0.55f, 0.10f, 0.85f, 0.05f, 0.00f, 0.00f },
    // EXPR_SWEAT
    { 0.70f, 0.65f, 0.60f, 0.50f, 0.55f, 0.10f, 0.40f, 0.00f, 0.00f, 0.00f },
    // EXPR_CRYING
    { 0.40f, 0.50f, 0.40f, 0.75f, 0.35f, 0.15f, 0.10f, 0.30f, 0.80f, 0.00f },
};

// ──────────────────────────────────────────────
// Name lookup table
// ──────────────────────────────────────────────

static const char* s_expression_names[EXPR_COUNT] = {
    "happy",
    "sad",
    "angry",
    "surprise",
    "blink",
    "idle",
    "talk",
    "listen",
    "think",
    "sleep",
    "wake",
    "confused",
    "excited",
    "dance",
    "love",
    "wink",
    "sweat",
    "crying",
};

// ──────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────

const expression_params_t* get_expression_params(expression_id_t id) {
    if (id < 0 || id >= EXPR_COUNT) {
        return &s_expressions[EXPR_IDLE];
    }
    return &s_expressions[id];
}

const char* get_expression_name(expression_id_t id) {
    if (id < 0 || id >= EXPR_COUNT) {
        return "unknown";
    }
    return s_expression_names[id];
}

expression_id_t lookup_expression(const char* name) {
    if (!name) return EXPR_IDLE;

    for (int i = 0; i < EXPR_COUNT; i++) {
        const char* a = name;
        const char* b = s_expression_names[i];
        while (*a && *b) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) break;
            a++; b++;
        }
        if (*a == '\0' && *b == '\0') {
            return (expression_id_t)i;
        }
    }
    return EXPR_IDLE;
}

void lerp_expression(const expression_params_t* from,
                     const expression_params_t* to,
                     float t,
                     expression_params_t* out) {
    if (!from || !to || !out) return;

    // Clamp t
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    out->eye_open    = from->eye_open    + (to->eye_open    - from->eye_open) * t;
    out->eye_width   = from->eye_width   + (to->eye_width   - from->eye_width) * t;
    out->eye_height  = from->eye_height  + (to->eye_height  - from->eye_height) * t;
    out->brow_angle  = from->brow_angle  + (to->brow_angle  - from->brow_angle) * t;
    out->brow_height = from->brow_height + (to->brow_height - from->brow_height) * t;
    out->mouth_open  = from->mouth_open  + (to->mouth_open  - from->mouth_open) * t;
    out->mouth_curve = from->mouth_curve + (to->mouth_curve - from->mouth_curve) * t;
    out->blush       = from->blush       + (to->blush       - from->blush) * t;
    out->tears       = from->tears       + (to->tears       - from->tears) * t;
    out->heart_eyes  = from->heart_eyes  + (to->heart_eyes  - from->heart_eyes) * t;
}
