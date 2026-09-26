#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <istream>

struct MythicSound { const char* name; const char* resource; unsigned repetitions = 1; };
inline constexpr MythicSound MythicSounds[] = {
    {"Triple Message","ChatWindowMessage",3},
    {"Force Blast","SpellForceWave"},
    {"Dreadnaught Boom","DreadnaughtDeath"},
    {"Banshee Scream","FadeBansheeSpellScream"},
    {"Harbinger Roar","Mutant_Harbinger_DeathVox_01"},
    {"Howler Scream","Mutant_Howler_Death_01"},
    {"Explosion","BoomerExplosion"},
    {"Cannon Blast","Mutant_ChestCannon_Melee_SpecialAttack"},
    {"Custom",""}
};
inline constexpr unsigned MythicCustomSound = unsigned(std::size(MythicSounds)-1);
enum class CustomSoundStatus : unsigned { None, Ready, Browsing, Format, File, Size, Audio, Unavailable };
inline const char* CustomSoundMessage(CustomSoundStatus status) {
    switch (status) {
        case CustomSoundStatus::Ready: return "Custom sound loaded.";
        case CustomSoundStatus::Browsing: return "Select a WAV or MP3 file.";
        case CustomSoundStatus::Format: return "Please select a WAV or MP3 file.";
        case CustomSoundStatus::File: return "The sound file could not be read or saved.";
        case CustomSoundStatus::Size: return "Select a sound smaller than 32 MB.";
        case CustomSoundStatus::Audio: return "This WAV or MP3 file could not be played.";
        case CustomSoundStatus::Unavailable: return "Custom audio is unavailable.";
        default: return "Browse to select a WAV or MP3 file.";
    }
}

struct MythicSettings {
    bool enabled = true;
    bool announcements = true;
    unsigned sound = 0;
    unsigned volume = 100;
    void Load(std::istream& input) {
        unsigned on = 1, choice = 0;
        if (input >> on >> choice && on <= 1) {
            enabled = on != 0;
            announcements = enabled;
            sound = choice < std::size(MythicSounds) ? choice : 0;
            unsigned messages = 0;
            if (input >> messages && messages <= 1) {
                announcements = messages != 0;
                unsigned loudness = 100;
                if (input >> loudness && loudness <= 100) volume = loudness;
            }
        }
    }
};

class MythicQueue {
    struct Identity { uintptr_t object = 0, item = 0; uint64_t until = 0; };
    std::array<Identity,256> ignored{};
    std::array<uint64_t,8> pending{};
    unsigned cursor = 0, first = 0, count = 0;
public:
    void Clear() { ignored = {}; pending = {}; cursor = first = count = 0; }
    void ClearPending() { pending = {}; first = count = 0; }
    void Ignore(uintptr_t object,uintptr_t item,uint64_t now) {
        if (object && item) ignored[cursor++ % ignored.size()] = {object,item,now+10000};
    }
    bool Push(uintptr_t object,uintptr_t item,uint64_t now,bool audio = true) {
        if (!object || !item) return false;
        for (const auto& entry : ignored) if (entry.object == object && entry.item == item && now <= entry.until) return false;
        Ignore(object,item,now);
        if (audio && count < pending.size()) pending[(first+count++) % pending.size()] = now;
        return true;
    }
    bool Pop(uint64_t now) {
        while (count) {
            const auto when = pending[first]; first = (first+1) % pending.size(); --count;
            if (now >= when && now-when <= 15000) return true;
        }
        return false;
    }
};

inline const char* MythicDropMessage(bool own) {
    return own ? "<font effect=rainbow>Mythic Item dropped for you!</font>" :
        "<font effect=rainbow>Mythic Item dropped for party member!</font>";
}

class MythicNotices {
    struct Notice { uint64_t time = 0; bool own = false; };
    std::array<Notice,32> pending{};
    unsigned first = 0, count = 0;
public:
    void Clear() { pending = {}; first = count = 0; }
    void Push(bool own,uint64_t now) {
        if (count < pending.size()) pending[(first+count++) % pending.size()] = {now,own};
    }
    void Pop() { if (count) { first = (first+1) % pending.size(); --count; } }
    bool Peek(uint64_t now,bool& own) {
        while (count) {
            const auto& next = pending[first];
            if (now >= next.time && now-next.time <= 15000) { own = next.own; return true; }
            Pop();
        }
        return false;
    }
};
