#pragma once
#include "native_mythic_sounds.h"
#include "wishing_well.h"
#include <fstream>

class NativeWellSound {
    CustomMythicSound custom;
    int channel = -1;
    unsigned pulses = 0, choice = 0, volume = 100;
    uint64_t nextPulse = 0, expiry = 0, nextPreview = 0;
    bool failed = false, previewing = false;
public:
    void Configure(const std::filesystem::path& directory,const wchar_t* title) { custom.Configure(directory,title); }
    void Browse(HWND owner) { custom.Select(owner); }
    CustomSoundStatus Status() const { return custom.Status(); }
    bool Imported() { return custom.TakeImported(); }
    bool Failed() const { return failed; }
    void Stop(const NativeReader& r,uintptr_t image) {
        pulses = 0; previewing = false; custom.Stop();
        if (channel >= 0 && NativeMythicSounds::AudioReady(r,image)) NativeMythicSounds::Stop(image,channel);
        else channel = -1;
    }
    void Start(const NativeReader& r,uintptr_t image,unsigned sound,unsigned loudness,uint64_t now,bool preview) {
        if (sound >= std::size(WellSounds) || custom.Browsing() || (preview && now < nextPreview)) return;
        if (preview) nextPreview = now+500;
        Stop(r,image);
        previewing = preview;
        if (sound == WellCustomSound) { custom.Play(loudness); return; }
        if (failed || !NativeMythicSounds::AudioReady(r,image)) return;
        choice = sound; volume = loudness;
        channel = NativeMythicSounds::PlayResource(image,WellSounds[choice].resource,volume,failed);
        if (channel >= 0 && !failed) { pulses = WellSounds[choice].repetitions-1; nextPulse = now+200; expiry = now+2000; }
    }
    void Service(const NativeReader& r,uintptr_t image,uint64_t now,bool allowed) {
        if (!allowed && !previewing) { Stop(r,image); return; }
        if (channel >= 0 && NativeMythicSounds::AudioReady(r,image) && !NativeMythicSounds::Playing(image,channel)) channel = -1;
        if (pulses && (now > expiry || failed)) pulses = 0;
        if (!pulses || now < nextPulse || !NativeMythicSounds::AudioReady(r,image)) return;
        NativeMythicSounds::Stop(image,channel);
        channel = NativeMythicSounds::PlayResource(image,WellSounds[choice].resource,volume,failed);
        --pulses; if (channel < 0 || failed) pulses = 0;
        nextPulse = now+200;
    }
};

class NativeWishingWell {
    WellTracker tracker;
    std::array<NativeWellSound,2> sounds;
    std::filesystem::path statePath;
    std::string current, pendingKey;
    uintptr_t pendingManager = 0, pendingQuest = 0;
    uint32_t pendingId = 0;
    uint64_t pendingStart = 0, nextPoll = 0, nextSave = 0, loginAfter = 0;
    bool available = false, saveFailed = false, chatFailed = false;

    static std::string Character(const NativeReader& r,uintptr_t image,uintptr_t* player = nullptr) {
        const uintptr_t zone = r.Pointer(r.Pointer(image+0x5314b0)+0x1b4), self = r.Pointer(zone+0xf8);
        if (!self || r.Pointer(self) != image+0x49b468) return {};
        if (player) *player = self;
        return WellCharacterKey(r.String(self+0x10,128));
    }
    static bool RepeatQuest(const NativeReader& r,uintptr_t image,uintptr_t manager,uintptr_t quest,std::string& key) {
        uintptr_t self = 0;
        key = Character(r,image,&self);
        if (key.empty() || !manager || r.Pointer(manager+0x14) != self || r.Pointer(quest) != image+0x49cbe0 ||
            r.Pointer(quest+0x14) != manager) return false;
        for (unsigned depth = 0; quest && depth < 8; ++depth) {
            const auto definition = r.Pointer(quest+0x58);
            auto path = r.String(definition+0x18,256);
            std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!path.empty()) return path == "world.town.quest.well.base_r";
            const auto base = r.Pointer(quest+0x54);
            if (base == quest) return false;
            quest = base;
        }
        return false;
    }
    void Save(uint64_t tick) {
        if (!tracker.Dirty() || statePath.empty() || tick < nextSave) return;
        nextSave = tick+5000;
        std::error_code error;
        std::filesystem::create_directories(statePath.parent_path(),error);
        saveFailed = true;
        if (error) return;
        auto pending = statePath; pending += L".pending";
        std::ofstream output(pending,std::ios::trunc);
        const bool written = tracker.Write(output);
        output.close();
        if (written && output.good() && MoveFileExW(pending.c_str(),statePath.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            tracker.Saved(); saveFailed = false;
        }
    }
public:
    static uint64_t UtcNow() {
        FILETIME time{}; GetSystemTimeAsFileTime(&time);
        const uint64_t ticks = (uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;
        return ticks/10000000ull-11644473600ull;
    }
    void Configure(const std::filesystem::path& directory) {
        statePath = directory/L"cooldowns.dat";
        std::error_code error;
        const auto size = std::filesystem::file_size(statePath,error);
        if (!error && size <= 65536) { std::ifstream input(statePath); saveFailed = !tracker.Load(input); }
        sounds[0].Configure(directory/L"Cooldown",L"Wishing Well - Cooldown sound");
        sounds[1].Configure(directory/L"Login",L"Wishing Well - Login sound");
    }
    void Logout() {
        tracker.Logout(); current.clear(); pendingKey.clear(); pendingManager = pendingQuest = 0;
        pendingStart = 0; pendingId = 0; loginAfter = 0;
    }
    bool Accepted(const NativeReader& r,uintptr_t image,uintptr_t manager,uintptr_t quest,uint64_t utc) {
        std::string key;
        uint32_t id = 0;
        if (!available || !RepeatQuest(r,image,manager,quest,key) || !r.Read(quest+0x68,id)) return false;
        if (pendingManager == manager && pendingQuest == quest && pendingId == id && pendingKey == key) return true;
        pendingManager = manager; pendingQuest = quest; pendingId = id; pendingStart = utc; pendingKey = std::move(key);
        return true;
    }
    bool Finalized(const NativeReader& r,uintptr_t image,uintptr_t manager,uintptr_t quest,uint64_t utc) {
        std::string key;
        uint32_t id = 0;
        uint8_t complete = 0;
        if (!available || !RepeatQuest(r,image,manager,quest,key) || !r.Read(quest+0x68,id) || !r.Read(quest+0x6c,complete) || !complete) return false;
        uint64_t start = utc;
        if (pendingManager == manager && pendingQuest == quest && pendingId == id && pendingKey == key && pendingStart <= utc && utc-pendingStart < 86400) start = pendingStart;
        pendingManager = pendingQuest = 0; pendingStart = 0; pendingKey.clear();
        const bool recorded = tracker.Used(key,start);
        if (recorded) nextSave = 0;
        return recorded;
    }
    WellUiState State(uint64_t utc) {
        WellUiState state;
        for (unsigned i = 0; i < sounds.size(); ++i) {
            state.custom[i] = sounds[i].Status(); state.imported[i] = sounds[i].Imported(); state.failed[i] = sounds[i].Failed();
        }
        state.remaining = tracker.Remaining(utc); state.saveFailed = saveFailed; state.known = tracker.Known();
        return state;
    }
    void Service(const NativeReader& r,uintptr_t image,const WellSettings& settings,const WellUiAction& action,bool installed,bool world,uint64_t tick,uint64_t utc) {
        available = installed;
        Save(tick);
        if (!world || !available) {
            for (auto& sound : sounds) sound.Stop(r,image);
            if (!available) Logout();
            return;
        }
        const auto key = Character(r,image);
        if (key.empty()) return;
        if (key != current) {
            for (auto& sound : sounds) sound.Stop(r,image);
            current = key; tracker.Select(key); loginAfter = tick+3000;
        }
        if (action.browse >= 0 && action.browse < 2 && action.owner) sounds[action.browse].Browse(reinterpret_cast<HWND>(action.owner));
        if (action.preview >= 0 && action.preview < 2) {
            sounds[1-action.preview].Stop(r,image);
            sounds[action.preview].Start(r,image,action.choice,action.volume,tick,true);
        }
        for (unsigned i = 0; i < sounds.size(); ++i) sounds[i].Service(r,image,tick,settings.enabled && settings.alerts[i].sound);
        if (tick < nextPoll || tick < loginAfter) return;
        nextPoll = tick+250;
        const uintptr_t chat = NativeMythicSounds::ChatReady(r,image);
        if (!chat) return;
        unsigned reminderMinutes = 0;
        const auto event = tracker.Poll(utc,settings,&reminderMinutes);
        if (event == WellEvent::None) return;
        const unsigned category = event == WellEvent::Login ? 1 : 0;
        const auto& alert = settings.alerts[category];
        if (alert.announcements && !chatFailed) AddNativeLocalLine(image+0x200120,chat,WellMessage(event,reminderMinutes).c_str(),chatFailed);
        if (alert.sound) sounds[category].Start(r,image,alert.choice,alert.volume,tick,false);
        Save(tick);
    }
};
