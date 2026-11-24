#pragma once
#include <cstdint>
#include <array>

enum class AddonUiLayer : unsigned { Meter, Hotbar, Effects, Menu, CharacterSheet, Bank, Inventory, QuestLog };
struct QuestLogFrame {
    std::array<float,4> panel{},title{},close{};
};
using AddonInputTest = bool (*)(float, float, bool);
enum class AddonHotkeyState { Available, Conflict, Busy, Unavailable };
using AddonHotkeyTest = AddonHotkeyState (*)(unsigned, unsigned, bool);
