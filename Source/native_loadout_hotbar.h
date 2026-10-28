#pragma once
#include "native_reader.h"
#include "loadouts.h"

struct NativeLoadoutHotbar {
    uintptr_t collection=0;
    std::vector<LoadoutBinding> bindings;
    std::vector<LoadoutSkill> skills;
    std::vector<uintptr_t> objects;
    static std::string Key(const NativeReader& reader,uintptr_t node) {
        std::set<uintptr_t> seen;
        while (node && seen.size()<24 && seen.insert(node).second) {
            const auto definition=reader.Pointer(node+0x58);
            if (definition) return reader.String(definition+0x18,256);
            node=reader.Pointer(node+0x54);
        }
        return {};
    }
    bool Read(const NativeReader& reader,uintptr_t image,uintptr_t unit) {
        const auto ui=reader.Pointer(image+0x5314b0),bar=reader.Pointer(ui+0x22c);
        if (reader.Pointer(bar)!=image+0x463c90 || reader.Pointer(bar+0x180)!=unit) return false;
        collection=reader.Pointer(bar+0x184);
        if (!collection || reader.Pointer(collection+0x14)!=unit || !reader.KindOf(collection,image+0x5309fc)) return false;
        const auto description=reader.Pointer(collection+0x5c);
        if (!description) return false;
        std::set<uintptr_t> seen;
        std::set<unsigned> slots;
        for (auto node=reader.Pointer(description+0x18);node;node=reader.Pointer(node+0x20)) {
            if (seen.size()>=128 || !seen.insert(node).second || reader.Pointer(node+0x14)!=description) return false;
            if (!reader.KindOf(node,image+0x530a04)) continue;
            uint8_t slot=0;
            if (!reader.Read(node+0x65,slot) || !slot || !slots.insert(slot).second || slots.size()>32) return false;
            bindings.push_back({slot,{},{}});
        }
        if (bindings.empty()) return false;
        seen.clear();
        std::set<std::string> keys;
        std::set<unsigned> occupied;
        for (auto node=reader.Pointer(collection+0x18);node;node=reader.Pointer(node+0x20)) {
            if (seen.size()>=512 || !seen.insert(node).second || reader.Pointer(node+0x14)!=collection) return false;
            if (!reader.KindOf(node,image+0x5309ec)) continue;
            uint8_t rank=0; unsigned slot=0;
            const auto key=Key(reader,node);
            if (key.empty() || !keys.insert(key).second || !reader.Read(node+0x75,rank) || !reader.Read(node+0x68,slot)) return false;
            const auto label=reader.Skill(node).second;
            skills.push_back({key,label,slot,rank}); objects.push_back(node);
            if (slot) {
                auto binding=std::find_if(bindings.begin(),bindings.end(),[&](const auto& value) { return value.slot==slot; });
                if (!rank || binding==bindings.end() || !occupied.insert(slot).second) return false;
                binding->key=key; binding->name=label;
            }
        }
        std::sort(bindings.begin(),bindings.end(),[](const auto& a,const auto& b) { return a.slot<b.slot; });
        return true;
    }
    uintptr_t Find(const std::string& key) const {
        for (size_t i=0;i<skills.size();++i) if (skills[i].key==key && skills[i].rank) return objects[i];
        return 0;
    }
    static bool Allowed(uintptr_t image,uintptr_t collection,unsigned slot,uintptr_t skill) {
        const uintptr_t function=image+0x140ee0;
        bool result=false;
        __try {
            __asm {
                mov edi, collection
                mov esi, skill
                mov eax, slot
                call function
                mov result, al
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        return result;
    }
    std::string Validate(uintptr_t image,const Loadout& goal) const {
        const auto plan=PlanLoadoutHotbar(goal,bindings,skills);
        if (!plan.error.empty()) return plan.error;
        for (const auto& binding:goal.hotbar) if (!binding.key.empty() && !Allowed(image,collection,binding.slot,Find(binding.key)))
            return "The game rejected a saved skill slot: "+binding.name;
        return {};
    }
    bool Send(uintptr_t image,const LoadoutBinding& binding) const {
        const auto object=Find(binding.key);
        if (!binding.key.empty() && (!object || !Allowed(image,collection,binding.slot,object))) return false;
        const auto skillCollection=collection;
        const auto slot=binding.slot;
        const uintptr_t clear=image+0x141050;
        __try {
            if (object) reinterpret_cast<void(__thiscall*)(void*,unsigned,void*)>(image+0x140f20)(reinterpret_cast<void*>(skillCollection),slot,reinterpret_cast<void*>(object));
            else {
                __asm {
                    mov edi, skillCollection
                    push slot
                    call clear
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }
};
