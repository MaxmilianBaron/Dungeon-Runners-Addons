#pragma once
#include "overlay_protocol.h"

enum class AccuracyWeapon : uint8_t { Melee, Ranged };
enum class AccuracyOutcome : uint8_t { Hit, Miss, Block };

struct AccuracyRoll {
    uint32_t character = 0;
    AccuracyWeapon weapon = AccuracyWeapon::Melee;
    AccuracyOutcome outcome = AccuracyOutcome::Miss;
};

template<class Reader>
AccuracyRoll ReadAccuracyRoll(const Reader& reader, uintptr_t image, uintptr_t stack, uint32_t eax, uint32_t ecx) {
    const auto caller = reader.Pointer(stack + 0x90);
    AccuracyWeapon weapon;
    if (caller == image + 0x192229) weapon = AccuracyWeapon::Melee;
    else if (caller == image + 0x194690 || caller == image + 0x196161) weapon = AccuracyWeapon::Ranged;
    else return {};
    const auto source = reader.Pointer(stack + 0x20);
    if (!source || source != reader.LocalAvatar()) return {};
    const auto passed = static_cast<uint8_t>(eax);
    const auto blocked = static_cast<uint8_t>(ecx);
    if (passed > 1 || blocked > 1) return {};
    const auto outcome = !passed ? AccuracyOutcome::Miss : blocked ? AccuracyOutcome::Block : AccuracyOutcome::Hit;
    return {reader.PlayerId(source), weapon, outcome};
}

class NativeAccuracy {
    AccuracySnapshot snapshot{};
    uint32_t character = 0;
    uint64_t lastRoll = 0;
    bool active = false;

    static void Count(AccuracyCounts& counts, AccuracyOutcome outcome) {
        uint32_t& count = outcome == AccuracyOutcome::Hit ? counts.hits :
            outcome == AccuracyOutcome::Miss ? counts.misses : counts.blocks;
        if (count != UINT32_MAX) ++count;
    }

public:
    void Close() { active = false; }

    void Reset() {
        snapshot = {};
        Close();
    }

    void Character(uint32_t id) {
        if (!id) Close();
        else if (id != character) {
            Reset();
            character = id;
        }
    }

    void Record(const AccuracyRoll& roll, uint64_t now) {
        if (!roll.character) return;
        Character(roll.character);
        if (!active || now < lastRoll || now - lastRoll >= 10000) snapshot.current = {};
        active = true;
        lastRoll = now;
        const auto index = static_cast<unsigned>(roll.weapon);
        Count(snapshot.current[index], roll.outcome);
        Count(snapshot.overall[index], roll.outcome);
    }

    AccuracySnapshot Snapshot(bool available) const {
        if (!available) return {};
        auto result = snapshot;
        result.available = true;
        return result;
    }
};
