#pragma once
#include "native_reader.h"
#include "mouse_look.h"

struct MouseLookContext {
    uintptr_t handler = 0, mouse = 0;
    bool right = false, available = false;
    static MouseLookContext Read(const NativeReader& reader,uintptr_t image) {
        MouseLookContext value;
        const auto ui = reader.Pointer(image + 0x5314b0), manager = reader.Pointer(image + 0x533d70);
        if (reader.Pointer(reader.Pointer(manager) + 0x30) != image + 0x2ed200) return value;
        value.mouse = reader.Pointer(manager + 0x1c);
        value.handler = reader.Pointer(ui + 0x1c4);
        uint8_t right = 0;
        if (reader.Pointer(reader.Pointer(value.mouse) + 0x28) != image + 0x2ee930 ||
            reader.Pointer(reader.Pointer(value.handler) + 0x38) != image + 0x27c10 ||
            !reader.Read(value.mouse + 0x8d,right)) return {};
        value.right = right != 0;
        if (!reader.InWorld() || reader.Pointer(ui + 0x180) || reader.Pointer(ui + 0x1c8)) return value;
        uint8_t allowed = 0;
        if (!reader.Read(value.handler + 0x78,allowed) || !allowed) return value;
        const auto root = reader.Pointer(image + 0x533e20), focus = reader.Pointer(root + 0x1b4);
        uint32_t current = 0, normal = 0;
        if (!focus || !reader.Read(focus + 0x10,current) || !reader.Read(focus + 0x14,normal) || (current && current != normal)) return value;
        const auto chat = reader.Pointer(ui + 0x21c), prompt = reader.Pointer(ui + 0x268);
        uint8_t editing = 0, dragging = 0;
        if (chat && (!reader.Read(chat + 0x20c,editing) || editing || !reader.Read(chat + 0x2ec,dragging) || dragging)) return value;
        if (prompt && (!reader.Read(prompt + 0x179,editing) || editing)) return value;
        value.available = true;
        return value;
    }
};

inline bool MouseLookBlocksClick(const NativeReader& reader,const MouseLookGesture& gesture,uintptr_t stack,uintptr_t mouse) {
    uint32_t button = 0;
    return reader.Read(stack + 16,button) && button == 1 && gesture.Owns(reader.Pointer(stack + 4),mouse);
}

inline bool MouseLookMasksRight(const NativeReader& reader,const MouseLookGesture& gesture,uintptr_t image,uintptr_t stack,uintptr_t handler,uintptr_t mouse) {
    const auto caller = reader.Pointer(stack);
    return (caller == image + 0x2a606 || caller == image + 0x2a68c) && gesture.Owns(handler,mouse);
}

inline bool MouseLookProfileMatches(const NativeReader& reader,uintptr_t image) {
    if (image != 0x400000) return false;
    struct Region { uintptr_t rva; unsigned size; uint32_t hash; };
    constexpr Region regions[] = {{0x27c10,346,0x97138145},{0x2a9f0,170,0xb31c5569},{0x2a2b0,778,0x3520cd69},
        {0x2a5c0,343,0xab37e58d},{0x7ce00,264,0x08ca10c6},{0x2ee930,12,0xcb38403d},{0x2ed200,4,0xb134319c}};
    for (const auto& region : regions) {
        uint32_t hash = 2166136261u;
        for (unsigned i = 0; i < region.size; ++i) {
            uint8_t byte = 0;
            if (!reader.Read(image + region.rva + i,byte)) return false;
            hash = (hash ^ byte) * 16777619u;
        }
        if (hash != region.hash) return false;
    }
    return true;
}
