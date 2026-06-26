#ifndef EXPRESSIONS_H
#define EXPRESSIONS_H

#include <cstdint>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Expression parameter struct for face rendering.
 *
 * All fields are normalized 0.0–1.0 unless otherwise noted.
 */
typedef struct {
    float eye_open;       // 0.0=closed, 1.0=fully open
    float eye_width;      // 0.0=narrow slit, 1.0=wide
    float eye_height;     // 0.0=flat, 1.0=tall oval
    float brow_angle;     // 0.0=neutral, 0.5=up, 1.0=down (mapped to degrees internally)
    float brow_height;    // 0.0=low, 1.0=high
    float mouth_open;     // 0.0=closed, 1.0=wide open
    float mouth_curve;    // 0.0=frown, 0.5=neutral, 1.0=smile
    float blush;          // 0.0=none, 1.0=full blush
    float tears;          // 0.0=none, 1.0=full tears
    float heart_eyes;     // 0.0=normal eyes, 1.0=heart eyes
} expression_params_t;

/**
 * @brief Enumeration of all supported expressions.
 */
typedef enum {
    EXPR_HAPPY = 0,
    EXPR_SAD,
    EXPR_ANGRY,
    EXPR_SURPRISE,
    EXPR_BLINK,
    EXPR_IDLE,
    EXPR_TALK,
    EXPR_LISTEN,
    EXPR_THINK,
    EXPR_SLEEP,
    EXPR_WAKE,
    EXPR_CONFUSED,
    EXPR_EXCITED,
    EXPR_DANCE,
    EXPR_LOVE,
    EXPR_WINK,
    EXPR_SWEAT,
    EXPR_CRYING,
    EXPR_COUNT
} expression_id_t;

/**
 * @brief Get expression parameters by ID.
 * @param id Expression ID
 * @return Pointer to const expression_params_t
 */
const expression_params_t* get_expression_params(expression_id_t id);

/**
 * @brief Get expression name string.
 * @param id Expression ID
 * @return Null-terminated name string
 */
const char* get_expression_name(expression_id_t id);

/**
 * @brief Look up expression ID from name string.
 * @param name Expression name (case-insensitive)
 * @return Expression ID, or EXPR_IDLE if not found
 */
expression_id_t lookup_expression(const char* name);

/**
 * @brief Linearly interpolate between two expression parameter sets.
 * @param from Starting expression
 * @param to   Target expression
 * @param t    Interpolation factor (0.0=from, 1.0=to)
 * @param out  Output parameter set
 */
void lerp_expression(const expression_params_t* from,
                     const expression_params_t* to,
                     float t,
                     expression_params_t* out);

#ifdef __cplusplus
}
#endif

#endif // EXPRESSIONS_H
