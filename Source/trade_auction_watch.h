#pragma once
#include "trade_auction.h"
#include "trade_variants.generated.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

namespace TradeAuction {
inline constexpr size_t WatchEffectLimit=24;

inline std::string_view CanonicalDefinition(std::string_view definition) {
    const auto end=std::end(TradeItemAliases);
    const auto alias=std::lower_bound(std::begin(TradeItemAliases),end,definition,
        [](const TradeItemAlias& item,std::string_view key){return item.definition<key;});
    return alias!=end && definition==alias->definition ? alias->canonical : definition;
}

inline const TradeArmorRating* FindArmorRating(std::string_view definition) {
    definition=CanonicalDefinition(definition);
    const auto end=std::end(TradeArmorRatings);
    const auto found=std::lower_bound(std::begin(TradeArmorRatings),end,definition,
        [](const TradeArmorRating& item,std::string_view key){return item.definition<key;});
    return found!=end && definition==found->definition ? found : nullptr;
}

inline unsigned DefenseRating(const TradeArmorRating& armor,unsigned tier,unsigned quality) {
    return armor.ratings[(quality==7 ? 3u : 0u)+tier-1];
}

struct LocalRoll {
    std::string definition;
    unsigned tier=0, quality=0;
    std::vector<std::string> effects;
    unsigned defense=0;

    bool operator==(const LocalRoll& other) const {
        return definition==other.definition && tier==other.tier && quality==other.quality &&
            defense==other.defense && effects==other.effects;
    }
    bool Valid() const {
        return Definition(definition) && tier>=1 && tier<=3 && (quality==5 || quality==7) &&
            defense<=65535 && !effects.empty() && effects.size()<=WatchEffectLimit && std::is_sorted(effects.begin(),effects.end()) &&
            std::all_of(effects.begin(),effects.end(),[](const auto& effect){return Hex(effect,64);});
    }
};

struct LocalWatch {
    LocalRoll roll;
    std::vector<std::string> labels;
};
using LocalWatches=std::vector<LocalWatch>;

template<class ReadRoll>
bool MatchesLocalWatch(const LocalWatches& watches,const std::string& definition,unsigned tier,ReadRoll read) {
    const auto canonical=CanonicalDefinition(definition);
    const auto candidate=[&](const LocalWatch& watch) {
        return watch.roll.definition==canonical && watch.roll.tier==tier;
    };
    const auto first=std::find_if(watches.begin(),watches.end(),candidate);
    if (first==watches.end()) return false;
    LocalRoll actual;
    return read(actual) && std::any_of(first,watches.end(),[&](const LocalWatch& watch) {return watch.roll==actual;});
}

inline int WatchLevelDelta(unsigned quality) {
    return quality==7 ? TradeMythicLevelDelta : TradeUniqueLevelDelta;
}

inline const char* WatchModifierEffect(const std::string& definition) {
    const auto end=std::end(TradeModifierEffects);
    const auto found=std::lower_bound(std::begin(TradeModifierEffects),end,definition,
        [](const TradeModifierEffect& item,const std::string& key){return item.definition<key;});
    return found!=end && definition==found->definition ? found->effect : nullptr;
}

class LocalWatchList {
    std::filesystem::path path;
    std::shared_ptr<const LocalWatches> watches;
    bool ready=false;

    bool Save(const LocalWatches& next) const {
        auto pending=path;
        pending+=L".pending";
        std::ofstream output(pending,std::ios::binary|std::ios::trunc);
        output<<"{\"version\":1,\"watches\":[";
        for (size_t i=0;i<next.size();++i) {
            const auto& watch=next[i];
            if (i) output<<',';
            output<<"{\"definition\":"<<Quote(watch.roll.definition)<<",\"tier\":"<<watch.roll.tier
                  <<",\"quality\":"<<watch.roll.quality<<",\"defense\":"<<watch.roll.defense<<",\"effects\":"<<WriteLines(watch.roll.effects)
                  <<",\"labels\":"<<WriteLines(watch.labels)<<'}';
        }
        output<<"]}\n";
        output.close();
        return output.good() && MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }

public:
    void Open(const std::filesystem::path& directory) {
        watches.reset();
        ready=false;
        if (directory.empty()) return;
        path=directory/L"watchlist.json";
        std::error_code error;
        const bool exists=std::filesystem::exists(path,error);
        if (error) return;
        if (!exists) { ready=true; return; }
        const auto size=std::filesystem::file_size(path,error);
        if (error || !size || size>65536) return;
        std::ifstream input(path,std::ios::binary);
        std::string data(static_cast<size_t>(size),'\0');
        if (!input.read(data.data(),static_cast<std::streamsize>(data.size()))) return;
        try {
            const auto root=Leaderboard::Json::Reader(data).Parse();
            if (Leaderboard::Json::Integer(Require(root,"version",Value::Type::Number),1)!=1) return;
            const auto& rows=Require(root,"watches",Value::Type::Array).array;
            if (rows.size()>TrackLimit) return;
            LocalWatches parsed;
            for (const auto& row:rows) {
                LocalWatch watch;
                watch.roll.definition=Text(Require(row,"definition",Value::Type::String),192);
                watch.roll.definition=std::string(CanonicalDefinition(watch.roll.definition));
                watch.roll.tier=static_cast<unsigned>(Leaderboard::Json::Integer(Require(row,"tier",Value::Type::Number),3));
                watch.roll.quality=static_cast<unsigned>(Leaderboard::Json::Integer(Require(row,"quality",Value::Type::Number),7));
                if (!watch.roll.tier) return;
                if (const auto* defense=row.Find("defense"))
                    watch.roll.defense=static_cast<unsigned>(Leaderboard::Json::Integer(*defense,65535));
                else if (const auto* armor=FindArmorRating(watch.roll.definition))
                    watch.roll.defense=DefenseRating(*armor,watch.roll.tier,watch.roll.quality);
                const auto& effects=Require(row,"effects",Value::Type::Array).array;
                const auto& labels=Require(row,"labels",Value::Type::Array).array;
                if (effects.size()>WatchEffectLimit || labels.size()>8) return;
                for (const auto& effect:effects) watch.roll.effects.push_back(Text(effect,64));
                for (const auto& label:labels) watch.labels.push_back(Text(label,192));
                if (!watch.roll.Valid()) return;
                if (std::none_of(parsed.begin(),parsed.end(),[&](const LocalWatch& entry){return entry.roll==watch.roll;}))
                    parsed.push_back(std::move(watch));
            }
            if (!parsed.empty()) watches=std::make_shared<const LocalWatches>(std::move(parsed));
            ready=true;
        } catch (const std::runtime_error&) {}
    }

    bool Ready() const { return ready; }
    std::shared_ptr<const LocalWatches> Snapshot() const { return watches; }
    bool Contains(const LocalRoll& roll) const {
        return watches && std::any_of(watches->begin(),watches->end(),[&](const LocalWatch& entry){return entry.roll==roll;});
    }
    const char* Toggle(const LocalWatch& watch) {
        if (!ready) return "The private watch list could not be loaded.";
        if (!watch.roll.Valid()) return "Choose an exact tier and every roll.";
        LocalWatches next=watches ? *watches : LocalWatches{};
        const auto found=std::find_if(next.begin(),next.end(),[&](const LocalWatch& entry){return entry.roll==watch.roll;});
        const bool remove=found!=next.end();
        if (remove) next.erase(found);
        else {
            if (next.size()>=TrackLimit) return "Remove a private watch first.";
            next.push_back(watch);
        }
        auto updated=next.empty() ? nullptr : std::make_shared<const LocalWatches>(next);
        if (!Save(next)) return "The private watch list could not be saved.";
        watches=std::move(updated);
        return remove ? "Private watch removed." : "Tracking this roll privately.";
    }
};
}
