#pragma once
#include <cmath>

inline constexpr float NormalZoomMaximum=170.f;
inline constexpr float NormalZoomMinimum=40.f;

inline float MaximumMapZoom(float farClip) {
    return std::isfinite(farClip) && farClip>NormalZoomMaximum ? std::nextafter(farClip,0.f) : NormalZoomMaximum;
}
