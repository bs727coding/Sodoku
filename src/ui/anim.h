// Easing helpers for UI animations.
#pragma once

#include <cmath>

namespace sudoku::ui {

inline constexpr float kPi = 3.14159265f;

inline float clamp01(double t) { return t <= 0 ? 0.0f : t >= 1 ? 1.0f : float(t); }
inline float easeOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
inline float easeInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
inline float easeOutBack(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f, u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
// 0 -> 1 -> 0 over t in [0, 1].
inline float bump(float t) { return t <= 0 || t >= 1 ? 0.0f : std::sin(t * kPi); }

// Progress of an animation that started at `start` and lasts `dur` seconds; -1 if not running.
inline float progress(double now, double start, double dur) {
    if (start < 0) return -1.0f;
    const double t = (now - start) / dur;
    return (t < 0 || t >= 1) ? -1.0f : float(t);
}

}  // namespace sudoku::ui
