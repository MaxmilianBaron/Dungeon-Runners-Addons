#pragma once
#include "native_bank_sort.h"
#include "trade_auction.h"
#include "trade_auction_watch.h"
#include "loadout_name.h"
#include "windows_hash.h"

namespace TradeAuction {
template<size_t N> bool Match(const NativeReader& reader,uintptr_t address,const unsigned char(&expected)[N]) {
    std::array<unsigned char,N> actual{};
    return reader.Read(address,actual) && std::equal(actual.begin(),actual.end(),expected);
}

inline bool SupportsNativeItemView(const NativeReader& reader,uintptr_t image) {
    return
        Match(reader,image+0x94ba7,{0x64,0xa1,0x00,0x00,0x00,0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00,0x00}) &&
        Match(reader,image+0x95525,{0x83,0xec,0x14,0x53,0x33,0xc0,0x56,0x8b,0x74,0x24,0x30}) &&
        Match(reader,image+0x95642,{0xc2,0x14,0x00}) &&
        Match(reader,image+0x8b170,{0x8b,0x08,0x85,0xc9,0x74,0x18,0x8d,0x51,0x04}) &&
        Match(reader,image+0x94c10,{0x51,0xc7,0x04,0x24,0,0,0,0,0x85,0xc0,0x74,0x0c}) &&
        Match(reader,image+0x13bce0,{0x56,0x8b,0xf1,0x8b,0x06,0x50,0xe8}) &&
        Match(reader,image+0x180da0,{0x56,0x8b,0xf0,0xf6,0x86,0x83,0,0,0,1}) &&
        Match(reader,image+0x183c70,{0x0f,0xb6,0x41,0x7f,0x03,0x41,0x78,0x83,0xf8,0x01}) &&
        Match(reader,image+0x27fb90,{0x8d,0x81,0xbc,0,0,0,0xc3}) &&
        Match(reader,image+0x2d0ee0,{0x83,0xec,0x3c,0x53,0x89,0x44,0x24,0x04,0x8b,0x07});
}

inline bool SupportsLocalRolls(const NativeReader& reader,uintptr_t image) {
    return Match(reader,image+0x184160,{0x83,0xec,0x08,0x53,0x55,0x56,0x8b,0xf0,0x0f,0xb6,0x46,0x7f,0x03,0x46,0x78}) &&
        Match(reader,image+0x18ea13,{0x8b,0x54,0x24,0x10,0xc1,0xfa,0x08,0x42,0x66,0x89,0x96,0x86,0x00,0x00,0x00});
}

inline bool SupportsNativeHover(const NativeReader& reader,uintptr_t image) {
    return Match(reader,image+0x283380,{0x8b,0x44,0x24,0x1c,0x8b,0x35,0x18,0x3e,0x93,0x00,0x3b,0xc6,0x75,0x0d,0x85,0xc0,0x74,0x76}) &&
        Match(reader,image+0x283790,{0x8b,0x8e,0xac,0x00,0x00,0x00,0x85,0xc9,0x74,0x53,0x8b,0x01,0x8b,0x50,0x58,0xff,0xd2});
}

inline void ClearNativeHover(uintptr_t image) {
    const auto clear=image+0x283360;
    __try {
        __asm {
            xor ebx, ebx
            push ebx
            call clear
            add esp, 4
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

inline bool Bound(uintptr_t image,uintptr_t item) {
    const auto function=image+0x180da0;
    int result=1;
    __try {
        __asm {
            mov eax, item
            call function
            movzx eax, al
            mov result, eax
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return result!=0;
}

inline bool MakePopup(uintptr_t image,uintptr_t unit,uintptr_t item,uintptr_t* popup) {
    const auto instance=image+0x94ba0;
    const auto create=image+0x95510;
    const auto text=image+0x532fec;
    __try {
        __asm {
            push text
            push item
            push unit
            push popup
            call instance
            push eax
            call create
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return *popup!=0;
}

inline bool MakeName(uintptr_t image,uintptr_t item,uintptr_t* result) {
    const auto format=image+0x94c10;
    __try {
        __asm {
            xor ecx, ecx
            mov esi, result
            mov eax, item
            call format
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return *result!=0;
}

inline void ReleasePopup(uintptr_t image,uintptr_t* popup) {
    const auto release=image+0x8b170;
    __try {
        __asm {
            mov eax, popup
            call release
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
inline void ReleaseName(uintptr_t image,uintptr_t* name) {
    const auto release=image+0x13bce0;
    __try { reinterpret_cast<void(__thiscall*)(void*)>(release)(name); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

inline bool Inflate(uintptr_t image,const unsigned char* source,unsigned stored,unsigned char* target,unsigned size) {
    const auto inflate=image+0x2d0ee0;
    unsigned decoded=size;
    int result=-1;
    __try {
        __asm {
            lea edi, decoded
            mov eax, source
            mov ecx, stored
            mov edx, target
            call inflate
            mov result, eax
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return result==0 && decoded==size;
}

inline std::string GameUtf8(const std::string& text) {
    if (text.empty()) return {};
    const int size=MultiByteToWideChar(1252,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    if (size<=0) return {};
    std::wstring result(static_cast<size_t>(size),L'\0');
    if (MultiByteToWideChar(1252,0,text.data(),static_cast<int>(text.size()),result.data(),size)!=size) return {};
    return LoadoutNameInput::Utf8(result);
}

inline std::string ChatPhrase(const std::string& utf8,const char* fallback) {
    if (utf8.empty() || utf8.size()>192) return fallback;
    wchar_t name[193]{};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),name,193);
    if (!count) return fallback;
    char game[193]{};
    const int size=WideCharToMultiByte(1252,0,name,count,game,193,nullptr,nullptr);
    if (!size) return fallback;
    for (int i=0;i<size;++i) {
        const auto c=static_cast<unsigned char>(game[i]);
        if (c<32 || c==127 || c=='<' || c=='>' || c=='&') game[i]=' ';
    }
    return std::string(game,static_cast<size_t>(size));
}

inline std::string DropLabel(std::string_view link,const std::string& name) {
    constexpr std::string_view prefix="< a href = item:";
    const bool linked=link.size()<1400 && link.substr(0,prefix.size())==prefix &&
        link.size()>24 && link.substr(link.size()-4)=="</a>";
    return linked ? std::string(link) : ChatPhrase(name,"An item");
}

inline std::string TrackedNotice(std::string_view link,const std::string& name,const TrackedOffer& offer) {
    return "Trade Auction: "+DropLabel(link,name)+" dropped. "+
        ChatPhrase(offer.character,"A player")+" wants it for "+ChatPhrase(offer.offered,"the offered item")+".";
}

inline std::string LocalWatchNotice(std::string_view link,const std::string& name,unsigned tier) {
    return "Trade Auction: "+DropLabel(link,name)+" matches your private Tier "+std::to_string(tier)+" watch.";
}

inline std::vector<std::string> TooltipLines(const std::string& raw) {
    std::vector<std::string> lines;
    std::string line;
    auto finish=[&] {
        const auto first=line.find_first_not_of(' '),last=line.find_last_not_of(' ');
        if (first!=std::string::npos) {
            auto text=GameUtf8(line.substr(first,last-first+1));
            if (!text.empty()) lines.push_back(std::move(text));
        }
        line.clear();
    };
    for (size_t i=0;i<raw.size();++i) {
        const unsigned char c=raw[i];
        const auto next=i+1<raw.size() ? raw[i+1] : '\0';
        if (c=='<' && ((next>='a' && next<='z') || (next>='A' && next<='Z') || next=='/')) {
            const auto end=raw.find('>',i+1);
            if (end!=std::string::npos && end-i<=256) {
                const auto tag=BankLower(raw.substr(i+1,end-i-1));
                if (tag=="br" || tag=="br/" || tag=="br /" || tag=="/p" || tag=="/li" || tag=="/div") finish();
                i=end;
                continue;
            }
        }
        bool entity=false;
        if (c=='&') {
            for (const auto& pair:std::array<std::pair<const char*,char>,6>{{{"&amp;",'&'},{"&lt;",'<'},{"&gt;",'>'},{"&quot;",'"'},{"&apos;",'\''},{"&nbsp;",' '}}}) {
                const size_t length=std::strlen(pair.first);
                if (raw.compare(i,length,pair.first)==0) { line+=pair.second; i+=length-1; entity=true; break; }
            }
        }
        if (entity) continue;
        if (c=='\n' || c=='\r') finish();
        else if (c=='\t') line+=' ';
        else if (c>=32 && c!=127) line+=static_cast<char>(c);
    }
    finish();
    return lines;
}

inline std::string NativeName(const NativeReader& reader,uintptr_t image,uintptr_t item) {
    uintptr_t handle=0;
    const bool good=MakeName(image,item,&handle);
    std::string name;
    if (good) name=reader.String(reinterpret_cast<uintptr_t>(&handle),512);
    if (handle) ReleaseName(image,&handle);
    auto lines=TooltipLines(name);
    return lines.size()==1 ? std::move(lines.front()) : std::string{};
}

inline bool NativeStats(const NativeReader& reader,uintptr_t image,uintptr_t unit,uintptr_t item,Item& result) {
    uintptr_t popup=0;
    if (!MakePopup(image,unit,item,&popup)) {
        if (popup) ReleasePopup(image,&popup);
        return false;
    }
    struct Guard {
        uintptr_t image;
        uintptr_t* popup;
        ~Guard() { ReleasePopup(image,popup); }
    } guard{image,&popup};
    std::vector<uintptr_t> nodes{popup};
    std::set<uintptr_t> seen{popup};
    for (size_t i=0;i<nodes.size();++i) {
        const auto node=nodes[i];
        uint32_t flags=0;
        if (node!=popup && (!reader.Read(node+0xb4,flags) || !(flags&8))) continue;
        for (auto child=reader.Pointer(node+0x18);child;child=reader.Pointer(child+0x20)) {
            if (nodes.size()==192 || !seen.insert(child).second) return false;
            nodes.push_back(child);
        }
        for (auto& line:TooltipLines(reader.String(node+0xbc,8192))) {
            if (line==result.name || std::find(result.stats.begin(),result.stats.end(),line)!=result.stats.end()) continue;
            if (line.size()>192 || result.stats.size()>=28) return false;
            result.stats.push_back(std::move(line));
        }
    }
    return !result.stats.empty();
}

inline bool Tradable(const NativeReader& reader,uintptr_t image,uintptr_t item) {
    const auto description=reader.Pointer(item+0x5c);
    uint8_t flags=0,flags2=0;
    return !reader.IsGold(item) && reader.KindOf(item,image+0x530b80) && description && reader.Read(description+0xce,flags) &&
        reader.Read(description+0xcf,flags2) && !(flags&0x40) && !(flags2&8) && !Bound(image,item);
}

inline unsigned ItemRollTier(const NativeReader& reader,uintptr_t item) {
    uint8_t level=0;
    return reader.Read(item+0x7f,level) ? TierFromItemLevel(level) : 0;
}

inline bool ReadLocalRoll(const NativeReader& reader,uintptr_t image,uintptr_t item,
    const std::string& definition,unsigned tier,LocalRoll& roll) {
    const auto desc=reader.Pointer(item+0x5c);
    uint8_t baseQuality=0;
    int32_t delta=0;
    if (!tier || !desc || !reader.KindOf(item,image+0x530b80) ||
        !reader.Read(desc+0xcd,baseQuality) || !reader.Read(item+0x78,delta)) return false;
    unsigned best=1;
    std::set<uintptr_t> seen;
    LocalRoll sample{std::string(CanonicalDefinition(definition)),tier,0,{}};
    for (auto child=reader.Pointer(item+0x18);child;child=reader.Pointer(child+0x20)) {
        if (seen.size()>=64 || !seen.insert(child).second || reader.Pointer(child+0x14)!=item ||
            !reader.KindOf(child,image+0x530b90)) return false;
        const auto modifier=reader.Pointer(child+0x5c);
        uint8_t quality=0;
        if (!modifier || !reader.Read(modifier+0x73,quality) || quality>7) return false;
        best=std::max(best,static_cast<unsigned>(quality));
        const auto effect=WatchModifierEffect(NativeBankSort::TradeDefinition(reader,child));
        if (!effect) return false;
        if (*effect) {
            if (sample.effects.size()>=WatchEffectLimit) return false;
            sample.effects.emplace_back(effect);
        }
    }
    sample.quality=baseQuality==1 ? best : baseQuality;
    if ((sample.quality!=5 && sample.quality!=7) || delta!=WatchLevelDelta(sample.quality)) return false;
    if (reader.KindOf(item,image+0x530ba0)) {
        uint16_t defense=0;
        if (!reader.Read(item+0x86,defense)) return false;
        sample.defense=defense;
    }
    std::sort(sample.effects.begin(),sample.effects.end());
    if (!sample.Valid()) return false;
    roll=std::move(sample);
    return true;
}

inline bool RolledStats(const NativeReader& reader,uintptr_t image,uintptr_t item,std::string& key,unsigned depth=0) {
    const auto definition=NativeBankSort::TradeDefinition(reader,item);
    const auto description=reader.Pointer(item+0x5c);
    uint8_t level=0,quality=0;
    int32_t levelBonus=0;
    if (depth>3 || !Definition(definition) || !description || !reader.Read(description+0xcd,quality) ||
        !reader.Read(item+0x7f,level) || !reader.Read(item+0x78,levelBonus)) return false;
    key=definition+":"+std::to_string(quality)+":"+std::to_string(level)+":"+std::to_string(levelBonus);
    std::set<uintptr_t> seen;
    std::vector<std::string> modifiers;
    std::vector<std::string> sockets;
    for (auto child=reader.Pointer(item+0x18);child;child=reader.Pointer(child+0x20)) {
        if (seen.size()>=64 || !seen.insert(child).second || reader.Pointer(child+0x14)!=item) return false;
        if (reader.KindOf(child,image+0x530b90)) {
            const auto kind=NativeBankSort::TradeDefinition(reader,child);
            const auto modifier=reader.Pointer(child+0x5c);
            uint8_t rank=0,quality=0;
            uint32_t value=0;
            if (!Definition(kind) || !modifier || !reader.Read(modifier+0x73,quality) ||
                !reader.Read(child+0x74,rank) || !reader.Read(child+0x78,value)) return false;
            modifiers.push_back(kind+":"+std::to_string(quality)+":"+std::to_string(rank)+":"+std::to_string(value));
        } else if (reader.KindOf(child,image+0x530b80)) {
            std::string nested;
            if (!RolledStats(reader,image,child,nested,depth+1)) return false;
            sockets.push_back(std::move(nested));
        } else return false;
    }
    std::sort(modifiers.begin(),modifiers.end());
    auto append=[&](const auto& parts,char type) {
        for (const auto& part:parts) {
            key+=type;
            key+=std::to_string(part.size())+":"+part;
            if (key.size()>8192) return false;
        }
        return true;
    };
    return append(modifiers,'m') && append(sockets,'s');
}

inline std::string Signature(const NativeReader& reader,uintptr_t image,uintptr_t item) {
    std::string key;
    if (!RolledStats(reader,image,item,key)) return {};
    key="dr-trade-item-v1:"+key;
    try { return WindowsHash::Sha256(reinterpret_cast<const unsigned char*>(key.data()),static_cast<DWORD>(key.size())); }
    catch (const std::runtime_error&) { return {}; }
}

inline bool CaptureItems(const NativeReader& reader,uintptr_t image,const Capture& request,bool inventory,uintptr_t avatar,
    std::vector<Choice>& choices,Item& item) {
    std::vector<uintptr_t> objects;
    if (!NativeBankSort::TradePage(reader,image,inventory,avatar,objects)) return false;
    for (const auto object:objects) {
        uint32_t id=0;
        if (!reader.Read(object+0x68,id) || !id || (request.kind!=Capture::Kind::Page && request.id!=id) ||
            !Tradable(reader,image,object)) continue;
        auto key=NativeBankSort::TradeDefinition(reader,object);
        if (!Definition(key)) continue;
        const auto* definition=FindBankDefinition(key.c_str());
        if (!definition) continue;
        if (request.kind==Capture::Kind::Page) {
            auto name=NativeName(reader,image,object);
            if (name.empty()) name=*definition->label ? definition->label : key;
            choices.push_back({id,std::move(key),std::move(name),inventory,ItemRollTier(reader,object)});
            continue;
        }
        item.definition=std::move(key);
        item.signature=Signature(reader,image,object);
        item.rollTier=ItemRollTier(reader,object);
        if (request.kind==Capture::Kind::Confirm) return !item.signature.empty();
        item.name=NativeName(reader,image,object);
        return !item.signature.empty() && !item.name.empty() && item.name.size()<=160 && NativeStats(reader,image,avatar,object,item);
    }
    return request.kind==Capture::Kind::Page;
}

inline bool CapturePage(const NativeReader& reader,uintptr_t image,const Capture& request,std::string& character,
    uint32_t& characterId,std::vector<Choice>& choices,Item& item) {
    const auto avatar=reader.LocalAvatar();
    characterId=reader.PlayerId(avatar);
    if (!characterId || ((request.kind==Capture::Kind::Item || request.kind==Capture::Kind::Confirm) &&
        request.characterId!=characterId)) return false;
    character=GameUtf8(reader.String(reader.Pointer(avatar+0x14)+0x10,128));
    if (character.empty() || character.size()>64) return false;
    if (request.kind==Capture::Kind::Character) return true;
    if (request.kind!=Capture::Kind::Page)
        return CaptureItems(reader,image,request,request.inventory,avatar,choices,item);
    const bool inventory=CaptureItems(reader,image,request,true,avatar,choices,item);
    const bool bank=CaptureItems(reader,image,request,false,avatar,choices,item);
    return inventory || bank;
}
}
