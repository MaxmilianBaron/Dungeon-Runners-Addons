#pragma once
#include <cstdint>
#include <istream>
#include <ostream>

struct CombatCursorSettings {
    bool enabled = true;
    unsigned radius = 18;
    unsigned color = 0;
    bool Valid() const { return radius >= 12 && radius <= 40 && radius % 2 == 0 && color < 8; }
    bool Load(std::istream& input) {
        unsigned on = 0, size = 0, shade = 0;
        if (!(input >> on >> size >> shade) || on > 1) return false;
        CombatCursorSettings value{on != 0,size,shade};
        if (!value.Valid()) return false;
        input >> std::ws;
        if (!input.eof()) return false;
        *this = value;
        return true;
    }
    bool Write(std::ostream& output) const {
        if (!Valid()) return false;
        output << (enabled ? 1 : 0) << ' ' << radius << ' ' << color << '\n';
        return output.good();
    }
};

class CursorCombat {
    uint32_t player = 0;
    uint64_t until = 0;
public:
    void Reset() { player = 0; until = 0; }
    void Player(uint32_t value) {
        if (value != player) { player = value; until = 0; }
    }
    void Hit(uint32_t source, uint32_t target, uint32_t targetPetOwner, int32_t before, int32_t after, uint64_t now) {
        if (!player || before <= 0 || after < 0 || after >= before) return;
        if (target == player && after == 0) { until = 0; return; }
        if (source == player || target == player || targetPetOwner == player) until = now + 10000;
    }
    uint64_t Until() const { return until; }
    bool Active(uint64_t now) const { return player && until && now < until; }
};
