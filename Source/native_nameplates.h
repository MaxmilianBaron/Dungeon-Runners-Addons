#pragma once
#include "nameplates.h"
#include "native_reader.h"
#include <cstring>

class NativeNameplates {
    const NativeReader& reader;
    uintptr_t image;
    template<size_t N> bool Label(uintptr_t address,const char (&expected)[N]) const {
        const auto text = reader.Pointer(address);
        uint32_t size = 0;
        char value[N]{};
        return reader.Read(text,size) && size == N - 1 && reader.Read(text + 4,value) && std::memcmp(value,expected,N - 1) == 0;
    }
public:
    NativeNameplates(const NativeReader& source,uintptr_t base) : reader(source), image(base) {}
    NameplateKind Classify(uintptr_t unit) const {
        const auto type = reader.Pointer(unit);
        if (type == image + 0x46de00) {
            if (!reader.OwnerId(unit)) return NameplateKind::Other;
            const auto ui = reader.Pointer(image + 0x5314b0);
            const auto zone = ui ? reader.Pointer(ui + 0x1b4) : 0;
            const auto player = zone ? reader.Pointer(zone + 0xf8) : 0;
            return player && reader.Pointer(player) == image + 0x49b468 && reader.Pointer(player + 0x80) &&
                reader.Pointer(player + 0xb0) == unit ? NameplateKind::Self : NameplateKind::Player;
        }
        if (type != image + 0x472940 || !reader.OwnerId(unit)) return NameplateKind::Other;
        const auto description = reader.Pointer(unit + 0x5c);
        if (!description) return NameplateKind::Other;
        if (Label(description + 0x70,"Pope Sweet Geebus")) return NameplateKind::BlingGnome;
        if (Label(description + 0x70,"Flaming Buddy")) return NameplateKind::FlamingBuddy;
        return NameplateKind::Other;
    }
};
