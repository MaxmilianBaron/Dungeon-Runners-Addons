#pragma once
#include "native_effects.h"
#include "native_reader.h"
#include "cooldown_timers.h"

class NativeCooldowns {
    const NativeReader& reader;
    uintptr_t image;

    uint8_t Alignment(uintptr_t modifier, uintptr_t description) const {
        uint8_t alignment = 0;
        if (description && reader.Read(description + 0xb5,alignment)) {
            if (alignment == 1 || alignment == 2) return alignment;
            if (alignment > 2) return 0;
        }
        std::array<uintptr_t,16> seen{};
        unsigned count = 0;
        while (modifier) {
            if (count == seen.size() || std::find(seen.begin(),seen.begin() + count,modifier) != seen.begin() + count) return 0;
            seen[count++] = modifier;
            const uintptr_t definition = reader.Pointer(modifier + 0x58);
            if (definition) {
                auto path = reader.String(definition + 0x18,512);
                std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const int authored = AuthoredEffectAlignment(path);
                if (authored >= 0) return static_cast<uint8_t>(authored);
            }
            modifier = reader.Pointer(modifier + 0x54);
        }
        return 0;
    }

    bool Bounds(uintptr_t node, uintptr_t ui, int32_t width, int32_t height, TimerIcon& icon) const {
        int32_t w = 0, h = 0;
        if (!reader.Read(node + 0xf8,w) || !reader.Read(node + 0xfc,h) || w < 8 || h < 8 || w > 256 || h > 256) return false;
        int64_t x = 0, y = 0;
        float opacity = 1;
        std::array<uintptr_t,16> ancestors{};
        unsigned count = 0;
        while (node) {
            if (count == ancestors.size() || std::find(ancestors.begin(),ancestors.begin() + count,node) != ancestors.begin() + count) return false;
            ancestors[count++] = node;
            uint32_t flags = 0;
            float alpha = 0;
            if (!reader.Read(node + 0xb4,flags) || !(flags & 8) || !reader.Read(node + 0x14c,alpha) || !std::isfinite(alpha) || alpha <= 0 || alpha > 1) return false;
            opacity *= alpha;
            if (node == ui) {
                if (x < 0 || y < 0 || x + w > width || y + h > height || opacity <= 0) return false;
                icon.x = static_cast<float>(x) / width;
                icon.y = static_cast<float>(y) / height;
                icon.width = static_cast<float>(x + w) / width - icon.x;
                icon.height = static_cast<float>(y + h) / height - icon.y;
                icon.opacity = opacity;
                return true;
            }
            int32_t dx = 0, dy = 0, parentWidth = 0, parentHeight = 0;
            if (!reader.Read(node + 0xf0,dx) || !reader.Read(node + 0xf4,dy)) return false;
            x += dx; y += dy;
            node = reader.Pointer(node + 0x14);
            if (!node || !reader.Read(node + 0xf8,parentWidth) || !reader.Read(node + 0xfc,parentHeight) || x < 0 || y < 0 || x + w > parentWidth || y + h > parentHeight) return false;
        }
        return false;
    }

public:
    NativeCooldowns(const NativeReader& source, uintptr_t base) : reader(source), image(base) {}

    void Sample(CooldownFrame& output) const {
        output = {};
        const uintptr_t ui = reader.Pointer(image + 0x5314b0);
        const uintptr_t list = ui ? reader.Pointer(ui + 0x22c) : 0;
        if (!list || reader.Pointer(list) != image + 0x463c90) return;
        const uintptr_t unit = reader.Pointer(list + 0x180), skills = reader.Pointer(list + 0x184);
        const uintptr_t zone = reader.Pointer(ui + 0x1b4), player = zone ? reader.Pointer(zone + 0xf8) : 0;
        if (!unit || !skills || reader.Pointer(skills + 0x14) != unit || !reader.PlayerId(unit) || reader.Pointer(unit + 0x14) != player) return;
        int32_t width = 0, height = 0;
        if (!reader.Read(ui + 0xf8,width) || !reader.Read(ui + 0xfc,height) || width < 320 || height < 200 || width > 16384 || height > 16384) return;
        uintptr_t manipulators = 0, node = reader.Pointer(unit + 0x18);
        unsigned count = 0;
        std::array<uintptr_t,256> seen{};
        while (node) {
            if (count == seen.size() || reader.Pointer(node + 0x14) != unit || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return;
            seen[count++] = node;
            if (reader.Pointer(node) == image + 0x4710e8) { manipulators = node; break; }
            node = reader.Pointer(node + 0x20);
        }
        if (!manipulators) return;
        std::array<uint16_t,256> timers{};
        std::array<uintptr_t,256> bindings{};
        std::array<bool,256> slots{};
        count = 0;
        for (node = reader.Pointer(manipulators + 0x18); node; node = reader.Pointer(node + 0x20)) {
            if (count == seen.size() || reader.Pointer(node + 0x14) != manipulators || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return;
            seen[count++] = node;
            const auto type = reader.Pointer(node);
            if (type != image + 0x47c430 && type != image + 0x47bfc8) continue;
            uint32_t slot = 0;
            if (reader.Pointer(node + 0x6c) != unit || !reader.Read(node + 0x68,slot) || slot >= timers.size() || slots[slot]) continue;
            slots[slot] = true;
            uint16_t remaining = 0;
            if (reader.Read(node + (type == image + 0x47c430 ? 0x7c : 0x80),remaining)) { timers[slot] = remaining; bindings[slot] = node; }
        }
        CooldownFrame next;
        next.owner = static_cast<uint32_t>(unit);
        next.zone = static_cast<uint32_t>(zone);
        struct Branch { uintptr_t node, parent; };
        std::array<Branch,256> pending{};
        unsigned pendingCount = 1;
        pending[0] = {reader.Pointer(list + 0x18),list};
        count = 0;
        while (pendingCount) {
            const auto branch = pending[--pendingCount];
            node = branch.node;
            if (!node) continue;
            if (count == seen.size() || reader.Pointer(node + 0x14) != branch.parent || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return;
            seen[count++] = node;
            if (pendingCount + 2 > pending.size()) return;
            pending[pendingCount++] = {reader.Pointer(node + 0x20),branch.parent};
            uint32_t flags = 0;
            if (!reader.Read(node + 0xb4,flags) || !(flags & 8)) continue;
            if (reader.Pointer(node) == image + 0x463b28) {
                uint8_t slot = 0;
                TimerIcon icon;
                if (reader.Pointer(node + 0x170) != skills || !reader.Read(node + 0x174,slot) || !bindings[slot] || !Bounds(node,ui,width,height,icon)) continue;
                if (next.count == next.icons.size()) return;
                icon.ticks = timers[slot];
                icon.control = static_cast<uint32_t>(node);
                icon.source = static_cast<uint32_t>(bindings[slot]);
                next.icons[next.count++] = icon;
            } else pending[pendingCount++] = {reader.Pointer(node + 0x18),node};
        }
        if (ValidTimerFrame(next)) output = next;
    }

    void SampleEffects(EffectFrame& output) const {
        output = {};
        const uintptr_t ui = reader.Pointer(image + 0x5314b0);
        const uintptr_t list = ui ? reader.Pointer(ui + 0x26c) : 0;
        if (!list || reader.Pointer(list) != image + 0x44d958) return;
        const uintptr_t unit = reader.Pointer(list + 0x17c);
        const uintptr_t zone = reader.Pointer(ui + 0x1b4), player = zone ? reader.Pointer(zone + 0xf8) : 0;
        if (!unit || !reader.PlayerId(unit) || reader.Pointer(unit + 0x14) != player) return;
        int32_t width = 0, height = 0;
        if (!reader.Read(ui + 0xf8,width) || !reader.Read(ui + 0xfc,height) || width < 320 || height < 200 || width > 16384 || height > 16384) return;
        std::array<uintptr_t,256> seen{};
        unsigned count = 0;
        uintptr_t modifiers = 0, node = reader.Pointer(unit + 0x18);
        while (node) {
            if (count == seen.size() || reader.Pointer(node + 0x14) != unit || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return;
            seen[count++] = node;
            if (reader.Pointer(node) == image + 0x4726b0) { modifiers = node; break; }
            node = reader.Pointer(node + 0x20);
        }
        if (!modifiers) return;
        struct Effect { uintptr_t node; uint32_t ticks; uint8_t alignment; };
        std::array<Effect,256> effects{};
        unsigned effectCount = 0;
        count = 0;
        for (node = reader.Pointer(modifiers + 0x18); node; node = reader.Pointer(node + 0x20)) {
            if (count == seen.size() || reader.Pointer(node + 0x14) != modifiers || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return;
            seen[count++] = node;
            uint32_t ticks = 0;
            if (reader.Pointer(node + 0x74) != unit || !reader.Read(node + 0x7c,ticks)) continue;
            const auto description = reader.Pointer(node + 0x5c);
            const uint8_t alignment = Alignment(node,description);
            if (ticks || alignment) effects[effectCount++] = {node,ticks,alignment};
        }
        if (!effectCount) return;
        EffectFrame next;
        next.owner = static_cast<uint32_t>(unit);
        next.zone = static_cast<uint32_t>(zone);
        struct Branch { uintptr_t node, parent; };
        std::array<Branch,256> pending{};
        unsigned pendingCount = 1;
        pending[0] = {reader.Pointer(list + 0x18),list};
        count = 0;
        while (pendingCount) {
            const auto branch = pending[--pendingCount];
            node = branch.node;
            if (!node) continue;
            if (count == seen.size() || reader.Pointer(node + 0x14) != branch.parent || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return;
            seen[count++] = node;
            if (pendingCount + 2 > pending.size()) return;
            pending[pendingCount++] = {reader.Pointer(node + 0x20),branch.parent};
            uint32_t flags = 0;
            if (!reader.Read(node + 0xb4,flags) || !(flags & 8)) continue;
            const uintptr_t modifier = reader.Pointer(node + 0xec);
            const auto effect = std::find_if(effects.begin(),effects.begin() + effectCount,[modifier](const Effect& value) { return value.node == modifier; });
            if (effect != effects.begin() + effectCount) {
                TimerIcon icon;
                if (!Bounds(node,ui,width,height,icon)) continue;
                if (next.count == next.icons.size()) return;
                icon.ticks = effect->ticks;
                icon.control = static_cast<uint32_t>(node);
                icon.source = static_cast<uint32_t>(modifier);
                icon.alignment = effect->alignment;
                next.icons[next.count++] = icon;
            } else pending[pendingCount++] = {reader.Pointer(node + 0x18),node};
        }
        if (ValidTimerFrame(next)) output = next;
    }
};
