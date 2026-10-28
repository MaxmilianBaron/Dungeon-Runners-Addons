#pragma once
#include "mythic_sounds.h"
#include <map>
#include <ostream>
#include <sstream>

inline constexpr MythicSound WellSounds[] = {
    {"Triple Message","ChatWindowMessage",3},
    {"Well Greeting 1","Wishing_Well_Hello_01"},
    {"Well Greeting 2","Wishing_Well_Hello_02"},
    {"Well Greeting 3","Wishing_Well_Hello_03"},
    {"Well Greeting 4","Wishing_Well_Hello_04"},
    {"Well Greeting 5","Wishing_Well_Hello_05"},
    {"Well Greeting 6","Wishing_Well_Hello_06"},
    {"Well Greeting 7","Wishing_Well_Hello_07"},
    {"Well Greeting 8","Wishing_Well_Hello_08"},
    {"Well Greeting 9","Wishing_Well_Hello_09"},
    {"Well Goodbye 1","Wishing_Well_Bye_01"},
    {"Well Goodbye 2","Wishing_Well_Bye_02"},
    {"Well Goodbye 3","Wishing_Well_Bye_03"},
    {"Well Goodbye 4","Wishing_Well_Bye_04"},
    {"Well Goodbye 5","Wishing_Well_Bye_05"},
    {"Well Goodbye 6","Wishing_Well_Bye_06"},
    {"Well Goodbye 7","Wishing_Well_Bye_07"},
    {"Well Goodbye 8","Wishing_Well_Bye_08"},
    {"Well Goodbye 9","Wishing_Well_Bye_09"},
    {"Custom",""}
};
inline constexpr unsigned WellCustomSound = unsigned(std::size(WellSounds)-1);
inline constexpr uint64_t WellCooldownSeconds = 1800;

struct WellAlertSettings {
    bool sound = true, announcements = true;
    unsigned choice = 0, volume = 100;
};

struct WellSettings {
    bool enabled = true;
    bool timerVisible = true;
    bool remindersEnabled = true;
    std::array<unsigned,6> reminderMinutes{{5,0,0,0,0,0}};
    std::array<WellAlertSettings,2> alerts{};
    bool ValidReminders() const {
        uint32_t used = 0;
        for (const auto minutes : reminderMinutes) {
            if (!minutes) continue;
            if (minutes >= WellCooldownSeconds/60 || (used & (1u << minutes))) return false;
            used |= 1u << minutes;
        }
        return true;
    }
    bool Load(std::istream& input) {
        WellSettings candidate;
        std::string format, extra;
        unsigned enabledValue = 0;
        if (!(input >> format >> enabledValue) || (format != "WW1" && format != "WW2" && format != "WW3") || enabledValue > 1) return false;
        candidate.enabled = enabledValue != 0;
        if (format != "WW1") {
            unsigned timer = 0;
            if (!(input >> timer) || timer > 1) return false;
            candidate.timerVisible = timer != 0;
        }
        for (auto& alert : candidate.alerts) {
            unsigned sound = 0, messages = 0;
            if (!(input >> sound >> messages >> alert.choice >> alert.volume) || sound > 1 || messages > 1 ||
                alert.choice >= std::size(WellSounds) || alert.volume > 100) return false;
            alert.sound = sound != 0; alert.announcements = messages != 0;
        }
        if (format == "WW3") {
            unsigned reminders = 0;
            if (!(input >> reminders) || reminders > 1) return false;
            candidate.remindersEnabled = reminders != 0;
            for (auto& minutes : candidate.reminderMinutes) if (!(input >> minutes)) return false;
            if (!candidate.ValidReminders()) return false;
        }
        if (input >> extra) return false;
        *this = candidate;
        return true;
    }
    bool Write(std::ostream& output) const {
        if (!ValidReminders()) return false;
        for (const auto& alert : alerts) if (alert.choice >= std::size(WellSounds) || alert.volume > 100) return false;
        output << "WW3 " << enabled << ' ' << timerVisible << '\n';
        for (const auto& alert : alerts) output << alert.sound << ' ' << alert.announcements << ' ' << alert.choice << ' ' << alert.volume << '\n';
        output << remindersEnabled;
        for (const auto minutes : reminderMinutes) output << ' ' << minutes;
        output << '\n';
        return output.good();
    }
};

enum class WellEvent { None, Login, Reminder, Ready };
inline std::string WellMessage(WellEvent event,unsigned minutes = 5) {
    switch (event) {
        case WellEvent::Login: return "<font color=#ffffff>Don't forget to use the Wishing Well</font><font color=#ff0000>!</font>";
        case WellEvent::Reminder:
            if (!minutes || minutes >= WellCooldownSeconds/60) return {};
            return "<font color=#ffffff>The Wishing Well will be ready in " + std::to_string(minutes) +
                (minutes == 1 ? " minute" : " minutes") + "</font><font color=#ff0000>!</font>";
        case WellEvent::Ready: return "<font color=#ffffff>The Wishing Well is ready</font><font color=#ff0000>!</font>";
        default: return "";
    }
}

inline std::string WellCharacterKey(std::string_view name) {
    if (name.empty() || name.size() > 128) return {};
    std::string key;
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : name) {
        if (c < 32 || c == 127) return {};
        if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        key += digits[c >> 4]; key += digits[c & 15];
    }
    return key;
}

class WellTracker {
    struct Record { uint64_t ready = 0; uint32_t reminders = 0; bool notified = false; };
    static constexpr uint32_t AllReminders = (1u << 29)-1;
    std::map<std::string,Record> records;
    std::string current;
    bool login = false, dirty = false;
    static bool ValidKey(const std::string& key) {
        return !key.empty() && key.size() <= 256 && !(key.size()%2) &&
            std::all_of(key.begin(),key.end(),[](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }
public:
    void Logout() { current.clear(); login = false; }
    bool Select(const std::string& key) {
        if (!ValidKey(key)) return false;
        if (key != current) { current = key; login = true; }
        return true;
    }
    bool Used(const std::string& key,uint64_t start) {
        if (!ValidKey(key) || start < 946684800 || start > 4102444800ull) return false;
        auto found = records.find(key);
        if (found != records.end() && found->second.ready == start+WellCooldownSeconds) return false;
        if (found == records.end() && records.size() >= 128) {
            const auto oldest = std::min_element(records.begin(),records.end(),[](const auto& a,const auto& b) { return a.second.ready < b.second.ready; });
            records.erase(oldest);
        }
        records[key] = {start+WellCooldownSeconds,0,false};
        if (current == key) login = false;
        dirty = true;
        return true;
    }
    bool Known() const { return records.find(current) != records.end(); }
    uint64_t Remaining(uint64_t now) const {
        const auto found = records.find(current);
        return found != records.end() && found->second.ready > now ? std::min(WellCooldownSeconds,found->second.ready-now) : 0;
    }
    WellEvent Poll(uint64_t now,const WellSettings& settings,unsigned* reminderMinutes = nullptr) {
        if (reminderMinutes) *reminderMinutes = 0;
        if (current.empty()) return WellEvent::None;
        const bool entering = std::exchange(login,false);
        auto found = records.find(current);
        if (found != records.end()) {
            auto& record = found->second;
            if (now >= record.ready) {
                if (!record.notified) {
                    record.reminders = AllReminders; record.notified = true; dirty = true;
                    return settings.enabled ? WellEvent::Ready : WellEvent::None;
                }
            } else {
                const auto remaining = record.ready-now;
                unsigned notification = 0;
                for (unsigned minutes = 1; minutes < WellCooldownSeconds/60; ++minutes) {
                    const uint32_t bit = 1u << (minutes-1);
                    if (remaining > minutes*60 || (record.reminders & bit)) continue;
                    record.reminders |= bit; dirty = true;
                    if (remaining+5 >= minutes*60 && settings.enabled && settings.remindersEnabled &&
                        std::find(settings.reminderMinutes.begin(),settings.reminderMinutes.end(),minutes) != settings.reminderMinutes.end())
                        notification = minutes;
                }
                if (reminderMinutes) *reminderMinutes = notification;
                return notification ? WellEvent::Reminder : WellEvent::None;
            }
        }
        return settings.enabled && entering ? WellEvent::Login : WellEvent::None;
    }
    bool Load(std::istream& input) {
        std::string header, key, extra;
        size_t count = 0;
        if (!(input >> header >> count) || (header != "WWT1" && header != "WWT2") || count > 128) return false;
        std::map<std::string,Record> loaded;
        for (size_t i = 0; i < count; ++i) {
            Record record; uint32_t reminders = 0; unsigned notified = 0;
            if (!(input >> key >> record.ready >> reminders >> notified) || !ValidKey(key) ||
                record.ready < 946684800+WellCooldownSeconds || record.ready > 4102444800ull+WellCooldownSeconds ||
                reminders > (header == "WWT1" ? 1u : AllReminders) || notified > 1 || loaded.count(key)) return false;
            record.reminders = header == "WWT1" ? (reminders ? 1u << 4 : 0u) : reminders;
            record.notified = notified != 0;
            loaded.emplace(key,record);
        }
        if (input >> extra) return false;
        records = std::move(loaded); dirty = false;
        return true;
    }
    bool Write(std::ostream& output) const {
        output << "WWT2 " << records.size() << '\n';
        for (const auto& entry : records) output << entry.first << ' ' << entry.second.ready << ' ' << entry.second.reminders << ' ' << entry.second.notified << '\n';
        return output.good();
    }
    bool Dirty() const { return dirty; }
    void Saved() { dirty = false; }
};

struct WellUiState {
    std::array<CustomSoundStatus,2> custom{};
    std::array<bool,2> imported{}, failed{};
    uint64_t remaining = 0;
    bool saveFailed = false, known = false;
};
inline std::string WellTimerText(const WellUiState& state,bool compact = false) {
    if (!state.remaining) return state.known ? "Ready!" : compact ? "--:--" : "No cooldown tracked";
    const auto remaining = std::min(state.remaining,WellCooldownSeconds);
    return std::string(compact ? "" : "Ready in ") + std::to_string(remaining/60) + ":" + (remaining%60 < 10 ? "0" : "") + std::to_string(remaining%60);
}
struct WellUiAction {
    int preview = -1, browse = -1;
    unsigned choice = 0, volume = 100;
    uintptr_t owner = 0;
};
