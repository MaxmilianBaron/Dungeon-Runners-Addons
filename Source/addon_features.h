#pragma once
#include "overlay_protocol.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

inline std::string FormatDamage(double value, unsigned decimalPlaces = 2) {
    if (!std::isfinite(value) || value < 0) return "-";
    if (value == 0) return "0";
    const char* suffix[] = {"", "K", "M", "B", "T"};
    const double precisionScale[] = {1, 10, 100};
    decimalPlaces = std::min(decimalPlaces,2u);
    unsigned unit = 0;
    while (value >= 1000 && unit < 4) { value /= 1000; ++unit; }
    const double precision = unit ? precisionScale[decimalPlaces] : 1;
    value = std::round(value * precision) / precision;
    if (value >= 1000 && unit < 4) { value /= 1000; ++unit; }
    char number[320];
    std::snprintf(number,sizeof(number),"%.*f",unit ? static_cast<int>(decimalPlaces) : 0,value);
    return std::string(number) + suffix[unit];
}

struct ReportContext {
    std::array<uintptr_t,12> identity{};
    bool operator==(const ReportContext& other) const { return identity == other.identity; }
};

struct MeterReport {
    uint32_t count = 0;
    char lines[6][192]{};
    ReportContext recipient;
};

inline MeterReport BuildPartyReport(const MeterView& view, bool overall, bool dungeon = false, bool taken = false, const char* dungeonTitle = nullptr) {
    MeterReport result;
    if (!view.playerCount || view.playerCount > MeterPlayers || !std::isfinite(view.duration) || view.duration <= 0 || view.duration > 1e9) return result;
    double total = 0;
    for (unsigned i = 0; i < view.playerCount; ++i) {
        const auto& player = view.players[i];
        if (!std::isfinite(player.damage) || !std::isfinite(player.dps) || player.damage < 0 || player.dps < 0 || player.damage > 1e15 || player.dps > 1e15) return {};
        total += player.damage;
    }
    if (total <= 0) return result;
    std::string title;
    if (dungeon && dungeonTitle) for (size_t i=0;i<128 && dungeonTitle[i] && title.size()<64;++i) {
        const auto c=static_cast<unsigned char>(dungeonTitle[i]);
        if (c>=32 && c<127 && c!='<' && c!='>') title+=static_cast<char>(c);
    }
    const char* period=dungeon ? (title.empty() ? "Dungeon run" : title.c_str()) : overall ? "Overall" : "Current fight";
    std::snprintf(result.lines[result.count++],192,"[DPS] %s | %s | %.1f s | observed by this client",taken ? "Damage Taken" : "Damage Done",period,view.duration);
    std::array<unsigned,MeterPlayers> order{};
    for (unsigned i = 0; i < view.playerCount; ++i) order[i] = i;
    std::stable_sort(order.begin(),order.begin()+view.playerCount,[&](unsigned a,unsigned b) { return view.players[a].damage > view.players[b].damage; });
    for (unsigned i = 0; i < view.playerCount; ++i) {
        const auto& player = view.players[order[i]];
        std::string name;
        for (unsigned n = 0; n < sizeof(player.name) && player.name[n]; ++n) {
            const unsigned char c = static_cast<unsigned char>(player.name[n]);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '\'') name += static_cast<char>(c);
        }
        if (name.empty()) name = "Player " + std::to_string(player.characterId);
        std::snprintf(result.lines[result.count++],192,"%u. %s: %s damage | %.0f DPS | %.1f%%",i+1,name.c_str(),FormatDamage(player.damage).c_str(),player.dps,100*player.damage/total);
    }
    return result;
}

class PartyReportQueue {
    MeterReport report;
    ReportContext owner;
    uint32_t index = 0;
    uint64_t nextSend = 0, nextReport = 0;
public:
    bool Pending() const { return index < report.count; }
    bool CoolingDown(uint64_t now) const { return now < nextReport; }
    void Cancel() { report = {}; index = 0; }
    bool Start(const MeterReport& value,const ReportContext& context,uint64_t now) {
        if (Pending() || CoolingDown(now) || !value.count || value.count > 6) return false;
        for (unsigned i = 0; i < value.count; ++i) {
            size_t n = 0;
            for (; n < sizeof(value.lines[i]) && value.lines[i][n]; ++n) {
                const unsigned char c = static_cast<unsigned char>(value.lines[i][n]);
                if (c < 32 || c >= 127 || c == '<' || c == '>') return false;
            }
            if (!n || n == sizeof(value.lines[i])) return false;
        }
        report = value;
        owner = context;
        index = 0;
        nextSend = now;
        nextReport = now + 15000;
        return true;
    }
    const char* Next(const ReportContext& context,uint64_t now) {
        if (!(context == owner)) { Cancel(); return nullptr; }
        return Pending() && now >= nextSend ? report.lines[index] : nullptr;
    }
    void Sent(bool success,uint64_t now) {
        if (!success) { Cancel(); return; }
        if (Pending()) ++index;
        nextSend = now + 1500;
    }
};
