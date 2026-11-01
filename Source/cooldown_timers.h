#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

struct TimerIcon {
    float x = 0, y = 0, width = 0, height = 0, opacity = 1;
    uint32_t ticks = 0, control = 0, source = 0;
    uint8_t alignment = 0;
};

template<size_t Capacity> struct TimerFrame {
    std::array<TimerIcon,Capacity> icons{};
    unsigned count = 0;
    uint32_t owner = 0, zone = 0;
};

using CooldownFrame = TimerFrame<16>;
using EffectFrame = TimerFrame<64>;

struct TimerTextStyle {
    uint8_t red, green, blue;
    float scale;
};

inline TimerTextStyle TimerStyle(uint32_t ticks) {
    if (ticks && ticks < 90) return {255,70,65,0.59f};
    if (ticks && ticks < 300) return {251,187,6,0.56f};
    return {239,230,207,0.54f};
}

template<size_t Capacity> inline bool ValidTimerFrame(const TimerFrame<Capacity>& frame) {
    if (frame.count > frame.icons.size() || (frame.count && (!frame.owner || !frame.zone))) return false;
    for (unsigned i = 0; i < frame.count; ++i) {
        const auto& icon = frame.icons[i];
        if (!std::isfinite(icon.x) || !std::isfinite(icon.y) || !std::isfinite(icon.width) || !std::isfinite(icon.height) || !std::isfinite(icon.opacity) ||
            icon.x < 0 || icon.y < 0 || icon.width <= 0 || icon.height <= 0 || icon.x + icon.width > 1 || icon.y + icon.height > 1 || icon.opacity <= 0 || icon.opacity > 1 || !icon.control || !icon.source || icon.alignment > 2) return false;
        for (unsigned j = 0; j < i; ++j) if (frame.icons[j].control == icon.control) return false;
    }
    return true;
}

class CooldownHighlights {
    struct Flash { uint64_t start = 0; bool active = false; };
    CooldownFrame previous;
    std::array<Flash,16> flashes{};
public:
    void Reset() { previous = {}; flashes = {}; }
    void Update(const CooldownFrame& frame, uint64_t now, bool enabled) {
        if (!ValidTimerFrame(frame)) { Reset(); return; }
        std::array<Flash,16> next{};
        if (enabled && frame.owner == previous.owner && frame.zone == previous.zone) {
            for (unsigned i = 0; i < frame.count; ++i) {
                const auto& icon = frame.icons[i];
                if (icon.ticks) continue;
                for (unsigned j = 0; j < previous.count; ++j) {
                    const auto& old = previous.icons[j];
                    if (icon.control == old.control && icon.source == old.source) {
                        next[i] = old.ticks ? Flash{now,true} : flashes[j];
                        break;
                    }
                }
            }
        }
        previous = frame;
        flashes = next;
    }
    float Strength(unsigned index, uint64_t now) const {
        if (index >= previous.count || !flashes[index].active || now < flashes[index].start) return 0;
        const uint64_t elapsed = now - flashes[index].start;
        if (elapsed >= 1350) return 0;
        const unsigned phase = static_cast<unsigned>(elapsed % 450);
        if (phase >= 350) return 0;
        return phase < 100 ? 0.55f + 0.45f * phase / 100 : static_cast<float>(350 - phase) / 250;
    }
};

inline std::string FormatCooldown(uint32_t ticks, bool tenths) {
    if (!ticks) return {};
    char text[16]{};
    const unsigned seconds = static_cast<unsigned>((static_cast<uint64_t>(ticks) + 29) / 30);
    if (tenths && ticks < 300) {
        const unsigned value = (static_cast<unsigned>(ticks) + 2) / 3;
        std::snprintf(text,sizeof(text),"%u.%u",value / 10,value % 10);
    } else if (seconds >= 100) std::snprintf(text,sizeof(text),"%um",(seconds + 59) / 60);
    else std::snprintf(text,sizeof(text),"%u",seconds);
    return text;
}
