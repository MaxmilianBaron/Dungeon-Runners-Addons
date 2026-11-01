#pragma once
#include <cstdint>
#include <istream>
#include <ostream>

struct LowHpSettings {
    bool enabled = false;
    unsigned threshold = 30;
    unsigned intensity = 45;
    bool Valid() const { return threshold >= 1 && threshold <= 99 && intensity >= 10 && intensity <= 90 && intensity % 5 == 0; }
    bool Load(std::istream& input) {
        unsigned on = 0, limit = 0, opacity = 0;
        if (!(input >> on >> limit >> opacity) || on > 1) return false;
        const LowHpSettings value{on != 0,limit,opacity};
        if (!value.Valid()) return false;
        input >> std::ws;
        if (!input.eof()) return false;
        *this = value;
        return true;
    }
    bool Write(std::ostream& output) const {
        if (!Valid()) return false;
        output << (enabled ? 1 : 0) << ' ' << threshold << ' ' << intensity << '\n';
        return output.good();
    }
};

struct LocalHealthFrame {
    uint32_t player = 0;
    int32_t current = 0, maximum = 0;
    uint64_t sampledAt = 0;
    bool Valid() const { return player && current > 0 && maximum > 0 && maximum <= INT32_MAX / 256 && current <= int64_t(maximum) * 256; }
    bool Warning(const LowHpSettings& settings,uint64_t now) const {
        return settings.enabled && settings.Valid() && Valid() && now >= sampledAt && now - sampledAt <= 500 &&
            int64_t(current) * 100 < int64_t(maximum) * 256 * settings.threshold;
    }
};
