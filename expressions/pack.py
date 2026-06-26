"""18 facial expressions for Stack-chan — parameterized for tween animation."""

from dataclasses import dataclass
from typing import List, Optional

@dataclass
class Expression:
    """Face rendering parameters."""
    name: str
    label: str
    # Eyes
    eye_open: float        # 0.0 (closed) ~ 1.0 (fully open)
    eye_height: float      # pupil height offset (-1..1)
    eye_width: float       # 0.5~2.0
    pupil_x: float         # -1 (left) ~ 1 (right)
    pupil_y: float         # -1 (down) ~ 1 (up)
    # Eyebrows
    brow_angle: float      # degrees, -30~30
    brow_height: float     # -10~10 pixels
    # Mouth
    mouth_open: float      # 0.0~1.0
    mouth_curve: float     # -1.0 (frown) ~ 1.0 (smile)
    # Effects
    blush: int             # 0~255
    tear: bool
    heart_eyes: bool
    exclamation: bool
    # Timing
    tween_ms: int          # transition time in ms
    hold_ms: int           # hold time (0 = indefinite)
    # Servo hints (optional)
    servo_pan: Optional[float] = None
    servo_tilt: Optional[float] = None
    # LED
    led_r: int = 0
    led_g: int = 0
    led_b: int = 0


# ── Expression pack ───────────────────────────────────────────
# Each expression tweens smoothly from the current face.

EXPRESSIONS: List[Expression] = [
    Expression("idle",      "😐 一般",     0.85, 0.0, 1.0, 0.0, 0.0,  0, 0,  0.05, 0.1,  0, False, False, False, 200, 0,  180, 45, 255, 255, 255),
    Expression("happy",     "😊 開心",     0.90, 0.0, 1.1, 0.0,-0.2,  10, 2,  0.20, 0.8,  30, False, False, False, 250, 0,  180, 60, 255, 200, 50),
    Expression("sad",       "😢 傷心",     0.60, 0.0, 0.8, 0.0, 0.3, -15,-2,  0.30,-0.6,  0,  True, False, False, 350, 0,  180, 30, 80, 100, 255),
    Expression("angry",     "😠 嬲",       0.70, 0.0, 0.9, 0.0,-0.1, -25, 4,  0.15,-0.4,  20, False, False, True,  200, 0,  180, 50, 255, 50, 50),
    Expression("surprised", "😮 驚訝",     0.95, 0.0, 1.2, 0.0,-0.3,  15, 4,  0.70, 0.0,   0, False, False, True,  150, 800, 180, 55, 255, 255, 100),
    Expression("blink",     "👁️ 眨眼",    0.05, 0.0, 1.0, 0.0, 0.0,  0, 0,  0.05, 0.1,   0, False, False, False, 80, 150, 180, 45, 255, 255, 255),
    Expression("talk",      "🗣️ 講嘢",    0.80, 0.0, 1.0, 0.0, 0.0,  0, 0,  0.40, 0.2,   0, False, False, False, 100, 0,  180, 45, 255, 255, 255),
    Expression("listen",    "👂 聽緊",     0.80, 0.2, 1.0, 0.0, 0.0,  5, 1,  0.10, 0.1,   0, False, False, False, 200, 0,  160, 35, 100, 200, 255),
    Expression("think",     "🤔 思考",     0.70, 0.0, 0.9, 0.3,-0.3, -10, 3,  0.10,-0.1,  0, False, False, False, 300, 0,  180, 55, 200, 200, 255),
    Expression("sleep",     "😴 瞓覺",     0.10, 0.0, 0.8, 0.0, 0.5,  0,-2,  0.02, 0.0,   0, False, False, False, 500, 0,  180, 45, 50, 50, 100),
    Expression("wake",      "✨ 醒來",     0.50, 0.0, 1.0, 0.0,-0.1,  5, 1,  0.10, 0.3,   0, False, False, False, 200, 0,  180, 45, 255, 255, 200),
    Expression("confused",  "😕 疑惑",     0.75, 0.0, 0.9, 0.0, 0.1, -5, 0,  0.20,-0.2,  0, False, False, False, 250, 0,  180, 50, 200, 200, 100),
    Expression("excited",   "🤩 興奮",     0.95, 0.0, 1.2, 0.0,-0.3,  15, 3,  0.30, 0.9,  50, False, False, False, 200, 0,  180, 60, 255, 100, 100),
    Expression("dance",     "🕺 跳舞",     0.85, 0.0, 1.1, 0.0, 0.0,  5, 1,  0.25, 0.8,  20, False, False, False, 200, 0,  180, 45, 255, 200, 200),
    Expression("love",      "❤️ 冧",      0.90, 0.0, 1.1, 0.0,-0.3,  10, 2,  0.30, 0.7,  80, False, True, False, 300, 0,  180, 55, 255, 100, 150),
    Expression("wink",      "😉 單眼",     0.85, 0.0, 1.0, 0.0, 0.0,  5, 1,  0.15, 0.4,  10, False, False, False, 200, 600, 180, 50, 255, 200, 100),
    Expression("sweat",     "😅 滴汗",     0.80, 0.0, 0.9, 0.0, 0.1, -5, 0,  0.20, 0.2,  0, False, False, False, 250, 0,  180, 45, 200, 200, 100),
    Expression("crying",    "😭 喊",       0.40, 0.0, 0.7, 0.0, 0.5, -20,-3,  0.40,-0.5,  0, True, False, False, 400, 0,  180, 30, 100, 100, 255),
]


EXPRESSION_MAP = {e.name: e for e in EXPRESSIONS}


def get(name: str) -> Expression:
    """Get expression by name, fallback to idle."""
    return EXPRESSION_MAP.get(name, EXPRESSION_MAP["idle"])


def interpolate(a: Expression, b: Expression, t: float) -> Expression:
    """Linearly interpolate between two expressions (t: 0..1)."""
    def lerp(va, vb):
        return va + (vb - va) * t

    return Expression(
        name=f"tween_{a.name}_to_{b.name}",
        label="",
        eye_open=lerp(a.eye_open, b.eye_open),
        eye_height=lerp(a.eye_height, b.eye_height),
        eye_width=lerp(a.eye_width, b.eye_width),
        pupil_x=lerp(a.pupil_x, b.pupil_x),
        pupil_y=lerp(a.pupil_y, b.pupil_y),
        brow_angle=lerp(a.brow_angle, b.brow_angle),
        brow_height=lerp(a.brow_height, b.brow_height),
        mouth_open=lerp(a.mouth_open, b.mouth_open),
        mouth_curve=lerp(a.mouth_curve, b.mouth_curve),
        blush=int(lerp(float(a.blush), float(b.blush))),
        tear=a.tear or b.tear,
        heart_eyes=a.heart_eyes or b.heart_eyes,
        exclamation=a.exclamation or b.exclamation,
        tween_ms=int(lerp(float(a.tween_ms), float(b.tween_ms))),
        hold_ms=int(lerp(float(a.hold_ms), float(b.hold_ms))),
        servo_pan=lerp(a.servo_pan or 180, b.servo_pan or 180) if (a.servo_pan is not None or b.servo_pan is not None) else None,
        servo_tilt=lerp(a.servo_tilt or 45, b.servo_tilt or 45) if (a.servo_tilt is not None or b.servo_tilt is not None) else None,
        led_r=int(lerp(float(a.led_r), float(b.led_r))),
        led_g=int(lerp(float(a.led_g), float(b.led_g))),
        led_b=int(lerp(float(a.led_b), float(b.led_b))),
    )
