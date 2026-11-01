#pragma once
#include <cmath>
#include <cstdint>

inline uint32_t CursorRainbowTint(float x,float y) {
    const float angle=std::atan2(y,x)/6.2831853071795864769f;
    const float hue=(angle-std::floor(angle))*6;
    const auto sector=static_cast<unsigned>(hue);
    const float fraction=hue-sector;
    const auto up=static_cast<uint32_t>(32+223*fraction+.5f);
    const auto down=static_cast<uint32_t>(255-223*fraction+.5f);
    uint32_t red=255,green=32,blue=32;
    switch (sector) {
        case 0: green=up; break;
        case 1: red=down; green=255; break;
        case 2: red=32; green=255; blue=up; break;
        case 3: red=32; green=down; blue=255; break;
        case 4: red=up; blue=255; break;
        default: blue=down; break;
    }
    return 0xff000000u | (red<<16) | (green<<8) | blue;
}
