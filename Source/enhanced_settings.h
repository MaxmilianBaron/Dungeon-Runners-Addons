#pragma once
#include "low_hp_warning.h"
#include "unbind_left_click.h"
#include <filesystem>
#include <fstream>

struct EnhancedSettings {
    LowHpSettings health{};
    bool unbindLeft = false;
    bool mouseLook = false;
    bool enhancedZoom = false;
    bool chatTimestamps = false;
    bool tradeAuction = true;
    bool sallySam = true;
    bool Load(std::istream& input) {
        unsigned version = 0, warning = 0, threshold = 0, intensity = 0, unbind = 0, look = 0, zoom = 0, timestamps = 0, trade = 1, vendors = 1;
        if (!(input >> version >> warning >> threshold >> intensity >> unbind >> look) ||
            (version < 1 || version > 5) || warning > 1 || unbind > 1 || look > 1 ||
            (version >= 2 && (!(input >> zoom) || zoom > 1)) ||
            (version >= 3 && (!(input >> timestamps) || timestamps > 1)) ||
            (version >= 4 && (!(input >> trade) || trade > 1)) ||
            (version >= 5 && (!(input >> vendors) || vendors > 1))) return false;
        const EnhancedSettings value{{warning != 0,threshold,intensity},unbind != 0,look != 0,zoom != 0,timestamps != 0,trade != 0,vendors != 0};
        if (!value.health.Valid()) return false;
        input >> std::ws;
        if (!input.eof()) return false;
        *this = value;
        return true;
    }
    bool Write(std::ostream& output) const {
        if (!health.Valid()) return false;
        output << 5 << ' ' << (health.enabled ? 1 : 0) << ' ' << health.threshold << ' ' << health.intensity
            << ' ' << (unbindLeft ? 1 : 0) << ' ' << (mouseLook ? 1 : 0) << ' ' << (enhancedZoom ? 1 : 0)
            << ' ' << (chatTimestamps ? 1 : 0) << ' ' << (tradeAuction ? 1 : 0) << ' ' << (sallySam ? 1 : 0) << '\n';
        return output.good();
    }
    static EnhancedSettings Read(const std::filesystem::path& addons) {
        EnhancedSettings value;
        std::ifstream combined(addons / L"EnhancedSettings" / L"settings.ini");
        if (combined.is_open()) { value.Load(combined); return value; }
        std::ifstream health(addons / L"LowHPWarning" / L"settings.ini");
        value.health.Load(health);
        UnbindLeftClickSettings unbind;
        std::ifstream controls(addons / L"UnbindLeftClick" / L"settings.ini");
        if (unbind.Load(controls)) value.unbindLeft = unbind.enabled;
        return value;
    }
};
