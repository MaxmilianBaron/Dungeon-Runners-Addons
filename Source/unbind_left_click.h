#pragma once
#include "native_reader.h"
#include <istream>
#include <ostream>

struct UnbindLeftClickSettings {
    bool enabled = false;
    bool Load(std::istream& input) {
        unsigned value = 0;
        if (!(input >> value) || value > 1) return false;
        input >> std::ws;
        if (!input.eof()) return false;
        enabled = value != 0;
        return true;
    }
    bool Write(std::ostream& output) const { output << (enabled ? 1 : 0) << '\n'; return output.good(); }
};

inline uintptr_t LeftClickFallbackTarget(const NativeReader& reader,uintptr_t image,uintptr_t handler,bool enabled,uintptr_t original) {
    uint32_t button = UINT32_MAX;
    if (enabled && handler >= 0x10000 && handler <= UINT32_MAX - 0x38 &&
        reader.Read(handler + 0x34,button) && button == 0 && reader.InWorld()) return image + 0x2aae3;
    return original;
}
