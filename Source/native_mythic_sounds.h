#pragma once
#include "native_reader.h"
#include "mythic_sounds.h"
#include "native_chat.h"
#include "custom_mythic_sound.h"

class NativeMythicSounds {
    MythicQueue queue;
    MythicNotices notices;
    CustomMythicSound custom;
    bool customSelected = false;
    int channel = -1;
    uint64_t nextPoll = 0, nextPreview = 0, nextPulse = 0, pulseExpiry = 0;
    unsigned pulses = 0, pulseSound = 0, pulseVolume = 100;
    bool enabled = false, soundEnabled = false, announcementsEnabled = false, failed = false, chatFailed = false;

    static bool Mythic(const NativeReader& r,uintptr_t image,uintptr_t item) {
        const uintptr_t description = r.Pointer(item+0x5c);
        uint8_t quality = 0;
        if (!description || !r.Read(description+0xcd,quality)) return false;
        if (quality != 1) return quality == 7;
        std::array<uintptr_t,64> seen{};
        unsigned count = 0;
        for (uintptr_t node = r.Pointer(item+0x18); node; node = r.Pointer(node+0x20)) {
            if (count == seen.size() || r.Pointer(node+0x14) != item || std::find(seen.begin(),seen.begin()+count,node) != seen.begin()+count) return false;
            seen[count++] = node;
            const uintptr_t desc = r.Pointer(node+0x5c);
            uint8_t value = 0;
            if (desc && r.KindOf(node,image+0x530b90) && r.Read(desc+0x73,value)) quality = std::max(quality,value);
        }
        return quality == 7;
    }
    static bool Owned(const NativeReader& r,uintptr_t image,uint32_t owner,bool& own) {
        if (!owner) return false;
        const uintptr_t ui = r.Pointer(image+0x5314b0), zone = r.Pointer(ui+0x1b4), self = r.Pointer(zone+0xf8);
        if (!self || r.Pointer(self) != image+0x49b468) return false;
        const uint32_t selfId = static_cast<uint32_t>(r.Pointer(self+0x98));
        if (!selfId) return false;
        if (owner == selfId) { own = true; return true; }
        const uintptr_t group = r.Pointer(image+0x530d3c), begin = r.Pointer(group+0xe0), end = r.Pointer(group+0xe4);
        if (!begin || end < begin || end-begin > 20 || (end-begin)%4) return false;
        bool containsSelf = false, containsOwner = false;
        for (uintptr_t at = begin; at != end; at += 4) {
            const auto id = r.Pointer(r.Pointer(at)+0xc);
            containsSelf |= id == selfId; containsOwner |= id == owner;
        }
        return containsSelf && containsOwner;
    }
    static uintptr_t ChatReady(const NativeReader& r,uintptr_t image) {
        const uintptr_t ui = r.Pointer(image+0x5314b0), control = r.Pointer(ui+0x21c);
        if (!control || r.Pointer(control) != image+0x449b30) return 0;
        const uintptr_t client = r.Pointer(control+0x1b0);
        if (!client || r.Pointer(client) != image+0x4ac3a0) return 0;
        uint32_t begin = 0, end = 0, capacity = 0;
        if (!r.Read(client+0xa4,begin) || !r.Read(client+0xa8,end) || !r.Read(client+0xac,capacity) ||
            end < begin || capacity < end || end-begin > 128*20 || (end-begin)%20 || (capacity-begin)%20) return 0;
        if (begin ? begin < 0x10000 : end || capacity) return 0;
        return client;
    }
    static bool AudioReady(const NativeReader& r,uintptr_t image) {
        uint8_t initialized = 0, exiting = 1;
        return r.Read(image+0x531094,initialized) && initialized && r.Read(image+0x53070e,exiting) && !exiting && r.Pointer(image+0x534d80+0x18);
    }
    static int Play(uintptr_t image,unsigned choice,unsigned volume,bool& fault) {
        if (choice >= MythicCustomSound) return -1;
        struct TextBlock { uint32_t length; char value[64]; } text{};
        text.length = static_cast<uint32_t>(std::strlen(MythicSounds[choice].resource));
        std::memcpy(text.value,MythicSounds[choice].resource,text.length);
        const TextBlock* native = &text;
        const auto argument = &native;
        const float gain = std::min(volume,100u)/100.0f;
        uint32_t gainBits = 0;
        std::memcpy(&gainBits,&gain,sizeof(gain));
        const uintptr_t function = image+0x174c30;
        int result = -1;
        __try {
            __asm push esi
            __asm lea esi, result
            __asm push gainBits
            __asm push argument
            __asm call function
            __asm pop esi
        } __except(EXCEPTION_EXECUTE_HANDLER) { fault = true; return -1; }
        return result;
    }
    static bool Playing(uintptr_t image,int& value) {
        __try { return reinterpret_cast<bool(__thiscall*)(void*,int*)>(image+0x2fecb0)(reinterpret_cast<void*>(image+0x534d80),&value); }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static void Stop(uintptr_t image,int& value) {
        if (value < 0) return;
        __try { reinterpret_cast<void(__thiscall*)(void*,int*)>(image+0x2fec70)(reinterpret_cast<void*>(image+0x534d80),&value); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        value = -1;
    }
    void Start(uintptr_t image,unsigned choice,unsigned volume,uint64_t now) {
        pulses = 0;
        if (choice == MythicCustomSound) { channel = -1; custom.Play(volume); return; }
        custom.Stop();
        if (failed) return;
        channel = Play(image,choice,volume,failed);
        if (channel >= 0 && !failed && choice < std::size(MythicSounds)) {
            pulseSound = choice;
            pulseVolume = volume;
            pulses = MythicSounds[choice].repetitions-1;
            nextPulse = now+200;
            pulseExpiry = now+2000;
        }
    }
public:
    void Configure(const std::filesystem::path& directory) { custom.Configure(directory); }
    void Browse(HWND owner) { custom.Select(owner); }
    CustomSoundStatus CustomStatus() const { return custom.Status(); }
    bool TakeCustomImport() { return custom.TakeImported(); }
    bool Drop(const NativeReader& r,uintptr_t image,uintptr_t object,uint64_t now) {
        if (!enabled || !r.InWorld() || r.Pointer(object) != image+0x4928c8) return false;
        uint8_t state = 255;
        uint32_t owner = 0;
        bool own = false;
        const uintptr_t item = r.Pointer(object+0x114);
        if (!item || !r.Read(object+0x101,state) || state != 0 || !r.Read(object+0x104,owner) || !Owned(r,image,owner,own) || !Mythic(r,image,item) || !queue.Push(object,item,now,soundEnabled && (!failed || customSelected))) return false;
        if (announcementsEnabled && !chatFailed) notices.Push(own,now);
        return true;
    }
    void InventoryDrop(const NativeReader& r,uintptr_t object,uint64_t now) {
        if (enabled) queue.Ignore(object,r.Pointer(object+0x114),now);
    }
    void Reset(const NativeReader& r,uintptr_t image) {
        queue.Clear(); notices.Clear(); enabled = soundEnabled = announcementsEnabled = false; pulses = 0;
        custom.Stop();
        if (channel >= 0 && AudioReady(r,image)) Stop(image,channel); else channel = -1;
    }
    void Service(const NativeReader& r,uintptr_t image,const MythicSettings& settings,int preview,bool world,uint64_t now,unsigned previewVolume = 100) {
        if (!world) { if (enabled || channel >= 0 || custom.Busy()) Reset(r,image); return; }
        customSelected = settings.sound == MythicCustomSound;
        if (!settings.enabled && !settings.announcements && enabled) Reset(r,image);
        if (!settings.enabled && soundEnabled) {
            queue.ClearPending(); pulses = 0;
            custom.Stop();
            if (channel >= 0 && AudioReady(r,image)) Stop(image,channel); else channel = -1;
        }
        if (!settings.announcements && announcementsEnabled) notices.Clear();
        soundEnabled = settings.enabled;
        announcementsEnabled = settings.announcements;
        enabled = soundEnabled || announcementsEnabled;
        const bool due = now >= nextPoll;
        if (preview < 0 && ((!enabled && !pulses) || !due)) return;
        if (due) {
            bool own = false;
            if (!chatFailed && notices.Peek(now,own)) {
                const uintptr_t client = ChatReady(r,image);
                if (client) for (unsigned sent = 0; sent < 8 && notices.Peek(now,own); ++sent) {
                    notices.Pop();
                    if (!AddNativeLocalLine(image+0x200120,client,MythicDropMessage(own),chatFailed)) break;
                }
            }
            nextPoll = now+100;
        } else if (preview < 0) return;
        if (pulses && now > pulseExpiry) pulses = 0;
        if ((!soundEnabled && !pulses && preview < 0) || custom.Browsing()) return;
        if (preview >= 0 && static_cast<unsigned>(preview) < std::size(MythicSounds) && now >= nextPreview) {
            if (static_cast<unsigned>(preview) != MythicCustomSound && (failed || !AudioReady(r,image))) return;
            nextPreview = now+500;
            if (channel >= 0 && AudioReady(r,image)) Stop(image,channel); else channel = -1;
            Start(image,static_cast<unsigned>(preview),previewVolume,now);
            nextPoll = now+100;
            return;
        }
        if (!due) return;
        if (pulses) {
            if (failed || !AudioReady(r,image)) return;
            if (now >= nextPulse) {
                Stop(image,channel);
                channel = Play(image,pulseSound,pulseVolume,failed);
                --pulses;
                if (failed || channel < 0) pulses = 0;
                nextPulse = now+200;
            }
            return;
        }
        if (!soundEnabled) return;
        if (custom.Busy()) return;
        if (!customSelected && (failed || !AudioReady(r,image))) return;
        if (channel >= 0 && Playing(image,channel)) return;
        channel = -1;
        if (queue.Pop(now)) {
            Start(image,settings.sound,settings.volume,now);
        }
    }
    bool Failed() const { return failed; }
};
