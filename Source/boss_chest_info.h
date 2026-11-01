#pragma once
#include <string_view>

enum class BossChestPresence { Unknown, NotDetected, Found };

inline bool BossChestInfoZone(std::string_view key) {
    if (key.substr(0,7)!="dungeon" || key.substr(7,2)=="00") return false;
    const auto at=key.find("_boss");
    return at!=std::string_view::npos && (key.substr(at)=="_boss" || key.substr(at)=="_boss_loot");
}

class BossChestNotices {
    bool announced=false,found=false;
public:
    void Reset() { announced=found=false; }
    const char* Observe(BossChestPresence presence) {
        if (presence==BossChestPresence::Unknown) return nullptr;
        if (presence==BossChestPresence::Found) {
            if (found) return nullptr;
            announced=found=true;
            return "Boss chest found.";
        }
        if (announced) return nullptr;
        announced=true;
        return "No boss chest detected.";
    }
};
