#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>

enum class MovePanel : unsigned { Player, Party, Target, Chat, Minimap, Buffs, Count };
constexpr unsigned MovePanelCount = static_cast<unsigned>(MovePanel::Count);

struct MoveRect {
    float x = 0, y = 0, width = 0, height = 0;
    bool Valid() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
            std::abs(x) <= 32768 && std::abs(y) <= 32768 && width > 0 && height > 0 && width <= 16384 && height <= 16384;
    }
};

inline bool MoveResizable(unsigned panel) { return panel < MovePanelCount; }
inline bool MoveFixedAspect(unsigned panel) { return panel < unsigned(MovePanel::Chat) || panel == unsigned(MovePanel::Buffs); }

inline MoveRect FitMoveAspect(unsigned panel,MoveRect rect,const MoveRect& native,float width,float height) {
    if (!MoveFixedAspect(panel) || !rect.Valid() || !native.Valid()) return rect;
    const float maximum = std::min({640.0f/native.width,640.0f/native.height,width/native.width,height/native.height});
    const float minimum = std::min(maximum,std::max(80.0f/native.width,20.0f/native.height));
    const float scale = std::clamp(rect.width/native.width,minimum,maximum);
    rect.width = std::round(native.width*scale); rect.height = std::round(native.height*scale);
    return rect;
}

struct MovePlacement {
    bool custom = false;
    float x = 0, y = 0, width = 0, height = 0;
    bool Valid() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
            x >= 0 && x <= 1 && y >= 0 && y <= 1 && width >= 0 && height >= 0 && width <= 16384 && height <= 16384;
    }
};

struct MoveLayoutSettings {
    bool enabled = true;
    std::array<MovePlacement,MovePanelCount> panels{};
    bool Load(std::istream& input) {
        MoveLayoutSettings value;
        unsigned version = 0, on = 0;
        if (!(input >> version >> on) || (version != 1 && version != 2) || on > 1) return false;
        value.enabled = on != 0;
        const unsigned count = version == 1 ? unsigned(MovePanel::Buffs) : MovePanelCount;
        for (unsigned i = 0; i < count; ++i) {
            auto& panel = value.panels[i];
            unsigned custom = 0;
            if (!(input >> custom >> panel.x >> panel.y >> panel.width >> panel.height) || custom > 1 || !panel.Valid()) return false;
            panel.custom = custom != 0;
        }
        input >> std::ws;
        if (!input.eof()) return false;
        *this = value;
        return true;
    }
    bool Write(std::ostream& output) const {
        for (const auto& panel : panels) if (!panel.Valid()) return false;
        output << "2 " << (enabled ? 1 : 0) << '\n' << std::setprecision(9);
        for (const auto& panel : panels) output << (panel.custom ? 1 : 0) << ' ' << panel.x << ' ' << panel.y << ' ' << panel.width << ' ' << panel.height << '\n';
        return output.good();
    }
};

inline MoveRect ClampMoveRect(unsigned panel,MoveRect rect,float width,float height) {
    if (panel >= MovePanelCount || !rect.Valid() || !std::isfinite(width) || !std::isfinite(height) || width < 320 || height < 200 || width > 16384 || height > 16384) return {};
    if (panel == unsigned(MovePanel::Minimap)) {
        rect.width = rect.height = std::clamp(std::min(rect.width,rect.height),120.0f,std::min({400.0f,width,height}));
    } else if (panel == unsigned(MovePanel::Chat)) {
        rect.width = std::clamp(rect.width,240.0f,width);
        rect.height = std::clamp(rect.height,120.0f,height);
    } else {
        rect.width = std::clamp(rect.width,80.0f,std::min(640.0f,width));
        rect.height = std::clamp(rect.height,20.0f,std::min(640.0f,height));
    }
    rect.x = std::clamp(rect.x,0.0f,std::max(0.0f,width-rect.width));
    rect.y = std::clamp(rect.y,0.0f,std::max(0.0f,height-rect.height));
    return rect;
}

inline MoveRect ResolveMoveRect(unsigned panel,const MovePlacement& placement,MoveRect native,float width,float height) {
    if (!placement.custom || !placement.Valid()) return native;
    const auto original = native;
    if (MoveResizable(panel) && placement.width > 0 && placement.height > 0) { native.width = placement.width; native.height = placement.height; }
    native = FitMoveAspect(panel,native,original,width,height);
    native.x = placement.x * std::max(0.0f,width-native.width);
    native.y = placement.y * std::max(0.0f,height-native.height);
    return ClampMoveRect(panel,native,width,height);
}

inline MovePlacement StoreMoveRect(unsigned panel,MoveRect rect,float width,float height) {
    rect = ClampMoveRect(panel,rect,width,height);
    if (!rect.Valid()) return {};
    return {true,width > rect.width ? rect.x/(width-rect.width) : 0,height > rect.height ? rect.y/(height-rect.height) : 0,MoveResizable(panel) ? rect.width : 0,MoveResizable(panel) ? rect.height : 0};
}

struct MoveLayoutFrame {
    float width = 0, height = 0;
    std::array<MoveRect,MovePanelCount> panels{};
    std::array<MoveRect,MovePanelCount> natural{};
    std::array<bool,MovePanelCount> visible{};
};
