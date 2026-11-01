#pragma once
#include <array>
#include <cstdint>
#include <istream>
#include <ostream>

enum class NameplateKind : uint8_t { Other, Player, BlingGnome, FlamingBuddy, Self };
enum class NameplatePart : uint8_t { Bars, Name, Posse };

struct NameplateSettings {
    bool enabled = true;
    bool playerBars = true;
    bool playerName = true;
    bool playerPosse = true;
    bool blingBars = true;
    bool flamingBars = true;
    bool selfBars = true;
    bool selfName = true;
    bool selfPosse = true;

    unsigned Mask() const {
        return enabled ? 1u | (playerBars ? 2u : 0u) | (playerName ? 4u : 0u) |
            (playerPosse ? 8u : 0u) | (blingBars ? 16u : 0u) | (flamingBars ? 32u : 0u) |
            (selfBars ? 64u : 0u) | (selfName ? 128u : 0u) | (selfPosse ? 256u : 0u) : 0u;
    }
    bool Load(std::istream& input) {
        unsigned version = 0;
        std::array<unsigned,9> values{};
        if (!(input >> version) || (version != 1 && version != 2)) return false;
        for (unsigned i = 0; i < (version == 1 ? 6u : 9u); ++i) if (!(input >> values[i]) || values[i] > 1) return false;
        input >> std::ws;
        if (!input.eof()) return false;
        if (version == 1) { values[6] = values[1]; values[7] = values[2]; values[8] = values[3]; }
        enabled = values[0] != 0; playerBars = values[1] != 0; playerName = values[2] != 0;
        playerPosse = values[3] != 0; blingBars = values[4] != 0; flamingBars = values[5] != 0;
        selfBars = values[6] != 0; selfName = values[7] != 0; selfPosse = values[8] != 0;
        return true;
    }
    void Save(std::ostream& output) const {
        output << 2 << ' ' << enabled << ' ' << playerBars << ' ' << playerName << ' '
            << playerPosse << ' ' << blingBars << ' ' << flamingBars << ' '
            << selfBars << ' ' << selfName << ' ' << selfPosse << '\n';
    }
};

inline bool NameplateVisible(unsigned mask, NameplateKind kind, NameplatePart part) {
    if (!(mask & 1u)) return true;
    if (kind == NameplateKind::Self) return (mask & (part == NameplatePart::Bars ? 64u : part == NameplatePart::Name ? 128u : 256u)) != 0;
    if (kind == NameplateKind::Player) return (mask & (part == NameplatePart::Bars ? 2u : part == NameplatePart::Name ? 4u : 8u)) != 0;
    if (part == NameplatePart::Bars && kind == NameplateKind::BlingGnome) return (mask & 16u) != 0;
    if (part == NameplatePart::Bars && kind == NameplateKind::FlamingBuddy) return (mask & 32u) != 0;
    return true;
}

class NameplateIndex {
    struct Entry { uintptr_t plate = 0; NameplateKind kind = NameplateKind::Other; };
    std::array<Entry,8192> entries{};
    static size_t Slot(uintptr_t plate) {
        uint32_t value = static_cast<uint32_t>(plate);
        value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15;
        return value & 8191u;
    }
public:
    void Remember(uintptr_t plate, NameplateKind kind) {
        if (!plate) return;
        Entry* empty = nullptr;
        for (size_t i = 0, slot = Slot(plate); i < 16; ++i) {
            auto& entry = entries[(slot + i) & 8191u];
            if (entry.plate == plate) { entry = kind == NameplateKind::Other ? Entry{} : Entry{plate,kind}; return; }
            if (!entry.plate && !empty) empty = &entry;
        }
        if (empty && kind != NameplateKind::Other) *empty = {plate,kind};
    }
    NameplateKind Find(uintptr_t plate) const {
        if (plate) for (size_t i = 0, slot = Slot(plate); i < 16; ++i) {
            const auto& entry = entries[(slot + i) & 8191u];
            if (entry.plate == plate) return entry.kind;
        }
        return NameplateKind::Other;
    }
    void Forget(uintptr_t plate) { Remember(plate,NameplateKind::Other); }
};
