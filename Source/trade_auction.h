#pragma once
#include "leaderboard.h"
#include <map>
#include <optional>

namespace TradeAuction {
using Value=Leaderboard::Json::Value;
struct Item {
    std::string definition,name,signature;
    std::vector<std::string> stats;
    unsigned rollTier=0;
};
struct Choice {
    uint32_t id=0;
    std::string definition,name;
    bool inventory=false;
    unsigned rollTier=0;
};
struct Capture {
    enum class Kind { None, Page, Item, Confirm, Character };
    Kind kind=Kind::None;
    bool inventory=false;
    uint32_t id=0, characterId=0;
};
struct Offer {
    std::string id,character,wanted;
    Item item;
    uint32_t revision=0;
    bool closed=false;
    std::string wantedDefinition,wantedSignature;
    std::vector<std::string> wantedOptions;
    bool WantedOnly() const { return item.definition.empty(); }
};
struct Page {
    std::vector<Offer> offers;
    std::string next;
    std::optional<unsigned> pageCount;
};
inline size_t PageCount(size_t entries,size_t pageSize) { return entries ? 1+(entries-1)/pageSize : 0; }
inline std::string PageLabel(size_t current,size_t total) {
    return std::to_string(current)+"/"+(total ? std::to_string(total) : "?");
}
inline constexpr size_t TrackLimit=12;
inline constexpr unsigned TierFromItemLevel(unsigned level) { return level>=100 && level<=102 ? level-99 : 0; }
inline std::string TierLabel(unsigned tier) { return tier>=1 && tier<=3 ? "Tier: "+std::to_string(tier) : "Tier: —"; }
inline unsigned WantedTier(const Offer& offer) {
    for (const auto& option:offer.wantedOptions)
        if (option.size()==12 && option.compare(0,11,"Roll tier: ")==0 && option.back()>='1' && option.back()<='3')
            return static_cast<unsigned>(option.back()-'0');
    return 0;
}
struct TrackedOffer {
    std::string id,definition,character,offered;
};
struct WantedIndex {
    std::map<std::string,std::set<std::string>> definitions;
    std::map<std::string,std::vector<TrackedOffer>> tracked;
    uint64_t expires=0;
};

inline bool Hex(const std::string& text,size_t length) {
    return text.size()==length && text.find_first_not_of("0123456789abcdef")==std::string::npos;
}
inline bool Definition(const std::string& name) {
    return !name.empty() && name.size()<=192 &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.-")==std::string::npos;
}
inline bool CanTrack(const Offer& offer) {
    return !offer.closed && !offer.WantedOnly() && Hex(offer.id,32) &&
        Definition(offer.wantedDefinition) && Hex(offer.wantedSignature,64);
}
inline const Value& Require(const Value& object,const char* key,Value::Type type) {
    const auto* field=object.Find(key);
    if (!field || field->type!=type) throw std::runtime_error("Invalid Trade Auction response.");
    return *field;
}
inline std::string Text(const Value& field,size_t limit,bool allowEmpty=false) {
    if (field.type!=Value::Type::String || (!allowEmpty && field.text.empty()) || field.text.size()>limit ||
        std::any_of(field.text.begin(),field.text.end(),[](unsigned char c){return c<32 || c==127;}))
        throw std::runtime_error("Invalid Trade Auction response text.");
    return field.text;
}
inline Page ReadPage(const Value& value) {
    Page page;
    if (const auto* count=value.Find("pageCount"))
        page.pageCount=static_cast<unsigned>(Leaderboard::Json::Integer(*count,2147483646));
    const auto& rows=Require(value,"offers",Value::Type::Array).array;
    if (rows.size()>6) throw std::runtime_error("Too many offers.");
    std::set<std::string> seen;
    for (const auto& row:rows) {
        Offer offer;
        offer.id=Text(Require(row,"id",Value::Type::String),32);
        offer.character=Text(Require(row,"character",Value::Type::String),64);
        offer.wanted=Text(Require(row,"wanted",Value::Type::String),160,true);
        offer.wantedDefinition=Text(Require(row,"wantedDefinition",Value::Type::String),192,true);
        offer.wantedSignature=Text(Require(row,"wantedSignature",Value::Type::String),64,true);
        offer.revision=static_cast<uint32_t>(Leaderboard::Json::Integer(Require(row,"revision",Value::Type::Number),2147483646));
        offer.closed=Require(row,"closed",Value::Type::Bool).text=="true";
        if (const auto* options=row.Find("wantedOptions")) {
            if (options->type!=Value::Type::Array || options->array.size()>8)
                throw std::runtime_error("Invalid Trade Auction variant.");
            for (const auto& option:options->array) {
                auto label=Text(option,192);
                if (label.rfind("Tier: ",0)!=0) offer.wantedOptions.push_back(std::move(label));
            }
        }
        const auto* item=row.Find("item");
        if (!Hex(offer.id,32) || !seen.insert(offer.id).second || !offer.revision ||
            !item || (item->type!=Value::Type::Object && item->type!=Value::Type::Null) ||
            (!offer.wantedDefinition.empty() && (!Definition(offer.wantedDefinition) || offer.wanted.empty())) ||
            (!offer.wantedSignature.empty() && (offer.wantedDefinition.empty() || !Hex(offer.wantedSignature,64))))
            throw std::runtime_error("Invalid Trade Auction offer.");
        if (item->type==Value::Type::Object) {
            offer.item.definition=Text(Require(*item,"definition",Value::Type::String),192);
            offer.item.name=Text(Require(*item,"name",Value::Type::String),160);
            offer.item.signature=Text(Require(*item,"signature",Value::Type::String),64);
            if (const auto* tier=item->Find("rollTier"))
                offer.item.rollTier=static_cast<unsigned>(Leaderboard::Json::Integer(*tier,3));
            const auto& stats=Require(*item,"stats",Value::Type::Array).array;
            if (!Definition(offer.item.definition) || !Hex(offer.item.signature,64) || stats.empty() || stats.size()>28)
                throw std::runtime_error("Invalid Trade Auction item.");
            for (const auto& stat:stats) offer.item.stats.push_back(Text(stat,192));
        }
        page.offers.push_back(std::move(offer));
    }
    page.next=Text(Require(value,"next",Value::Type::String),43,true);
    if (!page.next.empty()) {
        const auto separator=page.next.find('.');
        if (!separator || separator>10 || !Hex(page.next.substr(separator+1),32) ||
            page.next.substr(0,separator).find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("Invalid Trade Auction page.");
    }
    return page;
}

inline std::string Quote(const std::string& value) {
    std::string text="\"";
    for (const unsigned char c:value) {
        if (c<32 || c==127) throw std::runtime_error("Invalid control in offer.");
        if (c=='\"' || c=='\\') text+='\\';
        text+=static_cast<char>(c);
    }
    return text+'\"';
}
inline std::string WriteLines(const std::vector<std::string>& lines) {
    std::string text="[";
    for (const auto& line:lines) {
        if (text.size()>1) text+=',';
        text+=Quote(line);
    }
    return text+']';
}
inline std::string Write(const Offer& offer) {
    std::string result="{\"revision\":"+std::to_string(offer.revision);
    if (offer.closed) return result+",\"closed\":true}";
    if (offer.wantedDefinition.empty() && !offer.WantedOnly() && offer.wanted.empty() &&
        offer.wantedSignature.empty() && offer.wantedOptions.empty()) {
        result+=",\"wanted\":\"\",\"match\":null";
    } else {
        if (!Definition(offer.wantedDefinition) || offer.wanted.empty())
            throw std::runtime_error("Select a wanted item.");
        result+=",\"wanted\":"+Quote(offer.wanted);
        result+=",\"match\":{\"definition\":"+Quote(offer.wantedDefinition)+",\"signature\":"+Quote(offer.wantedSignature)+
            ",\"options\":"+WriteLines(offer.wantedOptions)+"}";
    }
    if (!offer.revision) {
        result+=",\"character\":"+Quote(offer.character);
        if (offer.WantedOnly()) return result+",\"item\":null}";
        result+=",\"item\":{\"definition\":"+Quote(offer.item.definition)+",\"name\":"+Quote(offer.item.name)+
            ",\"signature\":"+Quote(offer.item.signature)+",\"stats\":"+WriteLines(offer.item.stats);
        if (offer.item.rollTier>=1 && offer.item.rollTier<=3) result+=",\"rollTier\":"+std::to_string(offer.item.rollTier);
        result+='}';
    }
    return result+'}';
}
}
