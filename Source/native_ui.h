#pragma once
#include <cstdint>

enum class AddonUiLayer : unsigned { Meter, Hotbar, Effects, Menu, CharacterSheet };
using AddonInputTest = bool (*)(float, float, bool);
enum class AddonHotkeyState { Available, Conflict, Busy, Unavailable };
using AddonHotkeyTest = AddonHotkeyState (*)(unsigned, unsigned, bool);
