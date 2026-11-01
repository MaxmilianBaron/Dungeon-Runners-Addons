#pragma once
#include "native_reader.h"
#include "enhanced_zoom.h"
#include "native_zoom_profile.h"
#include "AardvarkHook/patch.h"

namespace EnhancedZoomNative {
inline volatile LONG maximumBits=0x432a0000;
inline const LONG normalBits=0x432a0000;
inline uintptr_t wheelContinue=0,dragContinue=0,outContinue=0,inContinue=0,pitchContinue=0;

static __declspec(naked) void Wheel() { __asm fld dword ptr [maximumBits] __asm jmp dword ptr [wheelContinue] }
static __declspec(naked) void Drag() { __asm fld dword ptr [maximumBits] __asm jmp dword ptr [dragContinue] }
static __declspec(naked) void Out() { __asm fld dword ptr [maximumBits] __asm jmp dword ptr [outContinue] }
static __declspec(naked) void In() { __asm fld dword ptr [maximumBits] __asm jmp dword ptr [inContinue] }
static __declspec(naked) void Pitch() {
    __asm pushfd
    __asm push eax
    __asm cmp dword ptr [maximumBits],0x432a0000
    __asm jbe original
    __asm mov eax,dword ptr [ecx+0x9c]
    __asm test eax,0x80000000
    __asm jnz original
    __asm cmp eax,0x7f800000
    __asm jae original
    __asm cmp eax,0x432a0000
    __asm jbe original
    __asm fld dword ptr [normalBits]
    __asm jmp done
original:
    __asm fld dword ptr [ecx+0x9c]
done:
    __asm pop eax
    __asm popfd
    __asm jmp dword ptr [pitchContinue]
}
}

struct EnhancedZoomFrame {
    uintptr_t ui=0,zone=0,world=0,camera=0;
    uint32_t seed=0;
    float distance=0;
    static EnhancedZoomFrame Read(const NativeReader& reader,uintptr_t image) {
        EnhancedZoomFrame result;
        if (!reader.InWorld()) return result;
        result.ui=reader.Pointer(image+0x5314b0);
        result.zone=reader.Pointer(result.ui+0x1b4);
        result.world=reader.Pointer(result.zone+0xf0);
        result.camera=reader.Pointer(result.ui+0x1cc);
        if (!result.world || !result.camera || result.camera%4 || reader.Pointer(result.camera)!=image+0x44fea8 ||
            reader.Pointer(result.camera+0x30)!=result.ui || !reader.Read(result.zone+0xc8,result.seed) ||
            !reader.Read(result.camera+0x9c,result.distance) || !std::isfinite(result.distance)) return {};
        return result;
    }
};

class NativeEnhancedZoom {
    NativePatchSet patches;
    std::array<void*,5> originals{};
    bool available=false,enabled=false,restorePending=false,searched=false;
    uintptr_t activeWorld=0,activeZone=0,map=0,mapOwner=0;
    uint32_t activeSeed=0;
    float maximum=NormalZoomMaximum,ownedMaximum=NormalZoomMaximum,restoreMaximum=NormalZoomMaximum;

    void Limit(float value) {
        if (value==maximum) return;
        maximum=value;
        LONG bits=0;
        std::memcpy(&bits,&value,sizeof(bits));
        InterlockedExchange(&EnhancedZoomNative::maximumBits,bits);
    }
    static uintptr_t FindMap(const NativeReader& reader,uintptr_t image,uintptr_t world) {
        if (!reader.Pointer(image+0x530860) || reader.Pointer(reader.Pointer(world)+0x38)!=image+0x2272c0) return 0;
        std::array<uintptr_t,64> seen{};
        unsigned count=0;
        for (auto node=reader.Pointer(world+0x18);node;node=reader.Pointer(node+0x20)) {
            if (count==seen.size() || std::find(seen.begin(),seen.begin()+count,node)!=seen.begin()+count) return 0;
            seen[count++]=node;
            if (reader.KindOf(node,image+0x530860)) return node;
        }
        return 0;
    }
    static bool Restore(const NativeReader& reader,uintptr_t image,const EnhancedZoomFrame& frame,float value) {
        if (reader.Pointer(frame.ui+0x1cc)!=frame.camera || reader.Pointer(frame.camera)!=image+0x44fea8 ||
            reader.Pointer(frame.camera+0x30)!=frame.ui) return false;
        LONG expected=0,replacement=0;
        std::memcpy(&expected,&frame.distance,sizeof(expected));
        std::memcpy(&replacement,&value,sizeof(replacement));
        const auto handler=reader.Pointer(frame.ui+0x1c4);
        uint8_t manual=1;
        const bool pitch=handler && reader.Pointer(reader.Pointer(handler)+0x38)==image+0x27c10 &&
            reader.Read(handler+0x74,manual) && !manual;
        return RestoreCamera(frame.camera,image,expected,replacement,pitch);
    }
    static bool RestoreCamera(uintptr_t camera,uintptr_t image,LONG expected,LONG replacement,bool pitch) {
        __try {
            if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(camera+0x9c),replacement,expected)!=expected) return false;
            if (pitch) reinterpret_cast<void(__thiscall*)(void*)>(image+0x28d00)(reinterpret_cast<void*>(camera));
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
public:
    bool Install(const NativeReader& reader,uintptr_t image,bool compatible) {
        uint32_t normal=0;
        if (available || !compatible || image!=0x400000 || !reader.Read(image+0x4cec54,normal) || normal!=0x432a0000) return false;
        EnhancedZoomNative::wheelContinue=image+0x27d11;
        EnhancedZoomNative::dragContinue=image+0x28afe;
        EnhancedZoomNative::outContinue=image+0x28c92;
        EnhancedZoomNative::inContinue=image+0x28cd2;
        EnhancedZoomNative::pitchContinue=image+0x28d09;
        const NativePatchSpec specs[] = {
            {reinterpret_cast<void*>(image+0x27d0b),reinterpret_cast<void*>(EnhancedZoomNative::Wheel),&originals[0],"d90554ec8c00d8d1dfe0ddd9f6c4057a",6},
            {reinterpret_cast<void*>(image+0x28af8),reinterpret_cast<void*>(EnhancedZoomNative::Drag),&originals[1],"d90554ec8c00d8d1dfe0f6c4057a0add",6},
            {reinterpret_cast<void*>(image+0x28c8c),reinterpret_cast<void*>(EnhancedZoomNative::Out),&originals[2],"d90554ec8c00d8d1dfe0ddd9f6c4057a",6},
            {reinterpret_cast<void*>(image+0x28ccc),reinterpret_cast<void*>(EnhancedZoomNative::In),&originals[3],"d90554ec8c00d8d1dfe0ddd9f6c4057a",6},
            {reinterpret_cast<void*>(image+0x28d03),reinterpret_cast<void*>(EnhancedZoomNative::Pitch),&originals[4],"d9819c0000008b8190000000d825d8eb",6}
        };
        available=patches.Install(specs,std::size(specs));
        return available;
    }
    bool Available() const { return available; }
    void Service(const NativeReader& reader,uintptr_t image,bool requested) {
        const bool next=available && requested;
        if (enabled!=next) {
            if (!next) { restorePending=enabled; restoreMaximum=ownedMaximum; }
            else restorePending=false;
            enabled=next; activeWorld=activeZone=map=mapOwner=0; searched=false;
        }
        if (!enabled) Limit(NormalZoomMaximum);
        if (!enabled && !restorePending) return;
        const auto frame=EnhancedZoomFrame::Read(reader,image);
        if (!frame.camera) { if (enabled) Limit(NormalZoomMaximum); return; }
        if (!enabled) {
            if (frame.distance>NormalZoomMaximum && frame.distance<=restoreMaximum && !Restore(reader,image,frame,NormalZoomMaximum)) return;
            restorePending=false;
            ownedMaximum=NormalZoomMaximum;
            return;
        }
        if (activeWorld!=frame.world || activeZone!=frame.zone || activeSeed!=frame.seed) {
            activeWorld=frame.world; activeZone=frame.zone; activeSeed=frame.seed; map=mapOwner=0; searched=false;
        }
        if (!searched) { map=FindMap(reader,image,frame.world); mapOwner=reader.Pointer(map+0x14); searched=true; }
        float farClip=0;
        if (!map || reader.Pointer(map+0x14)!=mapOwner || !reader.KindOf(map,image+0x530860) || !reader.Read(map+0x84,farClip)) {
            Limit(NormalZoomMaximum);
            if (map) { map=mapOwner=0; searched=false; }
            return;
        }
        Limit(MaximumMapZoom(farClip));
        if (frame.distance>maximum && frame.distance<=ownedMaximum && !Restore(reader,image,frame,maximum)) return;
        ownedMaximum=maximum;
    }
};
