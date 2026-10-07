#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

inline constexpr int32_t CharacterSheetExtraHeight = 44;

struct SheetRect { float x = 0, y = 0, w = 0, h = 0; };
struct SheetQuad { int32_t left = 0, top = 0, right = 0, bottom = 0; };
struct SheetVisualPart { SheetQuad source, destination; };
struct SheetResistance {
    SheetRect bounds;
    bool valid = false;
    int32_t weapon = 0, magic = 0;
};

inline bool SheetVisualParts(const SheetQuad& area,std::array<SheetVisualPart,3>& parts) {
    if (area.left != 0 || area.top != 0 || area.right != 400 || area.bottom != 610 + CharacterSheetExtraHeight) return false;
    parts = {{{{0,0,400,460},{0,0,400,460}},{{0,460,400,461},{0,460,400,460 + CharacterSheetExtraHeight}},
        {{0,460,400,610},{0,460 + CharacterSheetExtraHeight,400,610 + CharacterSheetExtraHeight}}}};
    return true;
}

struct CharacterSheetFrame {
    bool visible = false, criticalValid = false, magicCriticalValid = false, movementValid = false, stunResistValid = false, pvp = false;
    std::array<SheetRect,8> cells{};
    SheetRect section;
    float opacity = 1;
    int32_t critical = 0, magicCritical = 0;
    double movement = 0, stunResist = 0;
    std::array<char,1536> magicSkills{};
    std::array<SheetResistance,5> resistances{};
};

inline bool SheetCritical(int32_t base,int32_t melee,int32_t ranged,int32_t oneHand,int32_t twoHand,uint8_t type,int32_t& result) {
    for (const auto value : {base,melee,ranged,oneHand,twoHand}) if (value < -100000 || value > 100000) return false;
    int64_t bonus = 0;
    switch (type) {
        case 1: bonus = melee; break;
        case 3: case 9: case 13: bonus = ranged; break;
        case 5: bonus = int64_t(melee) + oneHand; break;
        case 6: case 8: bonus = int64_t(melee) + twoHand; break;
    }
    const int64_t fixedBase = int64_t(base) * 256;
    const int64_t product = fixedBase * (bonus * 256 / 100);
    const int64_t increment = product >= 0 ? product / 256 : -((-product + 255) / 256);
    result = static_cast<int32_t>(std::clamp(fixedBase + increment,int64_t(0),int64_t(25600)));
    return true;
}

inline double SheetCriticalProbability(int32_t threshold) {
    constexpr uint64_t outcomes = uint64_t(1) << 32, range = 25700;
    const auto count = static_cast<uint64_t>(std::clamp(threshold,0,23040));
    return static_cast<double>((outcomes / range) * count + std::min(outcomes % range,count)) * 100.0 / outcomes;
}

inline double SheetCriticalPercent(int32_t critical,bool pvp = false) {
    return SheetCriticalProbability(pvp ? critical * 3 / 4 : critical);
}

inline bool SheetMagicCritical(int32_t base,int32_t bonus,bool pvp,int32_t& result,int32_t multiplier = 256) {
    if (base < -100000 || base > 100000 || bonus < -100000 || bonus > 100000 || multiplier < 0 || multiplier > 25600) return false;
    const int64_t fixedBase = int64_t(base) * 256;
    const int64_t product = fixedBase * (int64_t(bonus) * 256 / 100);
    const int64_t increment = product >= 0 ? product / 256 : -((-product + 255) / 256);
    int64_t threshold = fixedBase + increment;
    if (threshold < std::numeric_limits<int32_t>::min() || threshold > std::numeric_limits<int32_t>::max()) return false;
    const int64_t scaledSkill = threshold * multiplier;
    threshold = scaledSkill >= 0 ? scaledSkill / 256 : -((-scaledSkill + 255) / 256);
    if (threshold < std::numeric_limits<int32_t>::min() || threshold > std::numeric_limits<int32_t>::max()) return false;
    if (pvp) {
        const int64_t scaled = threshold * 192;
        threshold = scaled >= 0 ? scaled / 256 : -((-scaled + 255) / 256);
    }
    result = static_cast<int32_t>(std::clamp(threshold,int64_t(0),int64_t(23040)));
    return true;
}

inline bool SheetStunResist(int32_t aggregate,bool pvp,double& percent) {
    if (aggregate < -100000 || aggregate > 100000) return false;
    const int32_t threshold = pvp ? std::clamp(aggregate * 128,1280,25600) : std::clamp(aggregate * 256,1280,23040);
    percent = threshold / 256.0;
    return true;
}

inline bool SheetDamageResist(int32_t elemental,int32_t general,int32_t magic,uint16_t level,bool pvp,bool spell,bool spellFloor,bool immune,int32_t& result) {
    if (!level) return false;
    for (const auto value : {elemental,general,magic}) if (value < -100000 || value > 100000) return false;
    if (immune) { result = 25600; return true; }
    const int64_t aggregate = int64_t(elemental) + general + (spell ? magic : 0);
    int64_t threshold = (aggregate * 256 / (int64_t(level) * 13)) * (pvp ? 35 : 75);
    if (!pvp && spell && spellFloor && threshold > 0 && threshold < 1280) threshold = 1280;
    result = static_cast<int32_t>(std::clamp(threshold,int64_t(0),int64_t(23040)));
    return true;
}

inline bool ValidCharacterSheet(const CharacterSheetFrame& frame) {
    if (!frame.visible || frame.magicSkills.back()) return false;
    const auto valid = [](const SheetRect& r) {
        return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) &&
            r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0 && r.x + r.w <= 1.001f && r.y + r.h <= 1.001f;
    };
    for (const auto& cell : frame.cells) if (!valid(cell)) return false;
    for (const auto& resistance : frame.resistances)
        if (resistance.valid && (!valid(resistance.bounds) || resistance.weapon < 0 || resistance.weapon > 25600 || resistance.magic < 0 || resistance.magic > 25600)) return false;
    return valid(frame.section) && std::isfinite(frame.opacity) && frame.opacity > 0 && frame.opacity <= 1 &&
        frame.critical >= 0 && frame.critical <= 25600 && frame.magicCritical >= 0 && frame.magicCritical <= 23040 &&
        std::isfinite(frame.stunResist) && frame.stunResist >= 0 && frame.stunResist <= 100 && std::isfinite(frame.movement) && frame.movement >= 0 && frame.movement <= 10000;
}
