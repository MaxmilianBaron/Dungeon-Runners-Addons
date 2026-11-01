#pragma once
#include <windows.h>
#include "native_meter.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <type_traits>

struct DungeonRecord {
    uint64_t id = 0;
    int64_t started = 0, ended = 0;
    uint32_t flags = 0;
    char title[128]{}, ending[48]{};
    MeterView view{}, taken{};
    static DWORD WriteHistory(const std::filesystem::path&,uint32_t,const std::vector<std::shared_ptr<const DungeonRecord>>&);
};
struct DungeonHistoryView { std::vector<DungeonRecord> records; char status[128]{}; };
struct DungeonHistorySnapshot { std::vector<std::shared_ptr<const DungeonRecord>> records; char status[128]{}; };

inline bool MeterNumber(double value) { return std::isfinite(value) && value >= 0 && value <= 1e15; }
inline bool MeterRange(uint32_t first,uint32_t count,size_t size) { return first <= size && count <= size-first; }
inline bool ValidCriticalHits(const CriticalHits& crit,uint32_t hits) {
    const uint64_t categorized = uint64_t(crit.weaponHits)+crit.magicHits+crit.otherHits+crit.reflectedHits;
    if (categorized && (categorized != crit.known || crit.weapon > crit.weaponHits || crit.magic > crit.magicHits ||
        uint64_t(crit.weapon)+crit.magic > crit.total || crit.total-crit.weapon-crit.magic > crit.otherHits)) return false;
    return crit.known <= hits && crit.total <= crit.known && uint64_t(crit.weapon)+crit.magic <= crit.total;
}
inline bool ValidMeterRow(const PlayerRow& row) {
    return ValidCriticalHits(row.critical,row.hits) && row.characterId && std::memchr(row.name,0,sizeof(row.name)) && MeterNumber(row.damage) && MeterNumber(row.dps) &&
        MeterNumber(row.unrecordedSkillDamage) && MeterNumber(row.unrecordedTargetDamage) && row.skillCount<=MeterSkills && row.targetCount<=MeterTargets;
}
inline bool ValidHitStatistics(const HitStatistics& stats,const CriticalHits& critical,uint32_t hits,double damage) {
    const auto valid = [](const HitValues& value) {
        if (!value.hits) return !value.amount && !value.lowest && !value.highest;
        return value.lowest && value.lowest <= value.highest && value.highest <= INT32_MAX &&
            uint64_t(value.lowest)*value.hits <= value.amount && value.amount <= uint64_t(value.highest)*value.hits;
    };
    const uint64_t count = uint64_t(stats.normal.hits)+stats.critical.hits;
    if (!valid(stats.normal) || !valid(stats.critical) || count > hits) return false;
    if (!count) return true;
    if (count != critical.known || stats.critical.hits != critical.total) return false;
    const double recorded = static_cast<double>(stats.normal.amount+stats.critical.amount)/256.0;
    return recorded <= damage && (count != hits || recorded == damage);
}
inline bool ValidMeterView(const MeterView& view) {
    if (view.playerCount>MeterPlayers || view.petCount>MeterPets || !MeterNumber(view.duration) || view.duration>1e9 ||
        view.skills.size()>MeterSkills || view.targets.size()>MeterTargets || view.details.size()>MeterDetails) return false;
    for (const auto& skill:view.skills) if (!std::memchr(skill.name,0,sizeof(skill.name)) || !std::memchr(skill.damageType,0,sizeof(skill.damageType)) ||
        !std::memchr(skill.component,0,sizeof(skill.component)) || !MeterNumber(skill.damage) || !MeterNumber(skill.dps) || !ValidCriticalHits(skill.critical,skill.hits) ||
        !ValidHitStatistics(skill.values,skill.critical,skill.hits,skill.damage)) return false;
    size_t skills=0,targets=0,details=0;
    const auto validRow=[&](const PlayerRow& row) {
        if (!ValidMeterRow(row) || !MeterRange(row.skillFirst,row.skillCount,view.skills.size()) || !MeterRange(row.targetFirst,row.targetCount,view.targets.size()) ||
            (row.skillCount && row.skillFirst!=skills) || (row.targetCount && row.targetFirst!=targets)) return false;
        for (uint32_t i=0;i<row.targetCount;++i) {
            const auto& target=view.targets[row.targetFirst+i];
            if (!std::memchr(target.counterpart,0,sizeof(target.counterpart)) || !MeterNumber(target.damage) || !MeterNumber(target.dps) || !MeterNumber(target.unrecordedDamage) ||
                !MeterRange(target.detailFirst,target.detailCount,view.details.size()) || (target.detailCount && target.detailFirst!=details)) return false;
            for (uint32_t j=0;j<target.detailCount;++j) {
                const auto& detail=view.details[target.detailFirst+j];
                if (!MeterNumber(detail.damage) || !ValidCriticalHits(detail.critical,detail.hits) || !ValidHitStatistics(detail.values,detail.critical,detail.hits,detail.damage) ||
                    detail.skill<row.skillFirst || detail.skill-row.skillFirst>=row.skillCount) return false;
            }
            details+=target.detailCount;
        }
        skills+=row.skillCount; targets+=row.targetCount;
        return true;
    };
    for (uint32_t i=0;i<view.playerCount;++i) {
        if (!validRow(view.players[i])) return false;
        for (uint32_t j=0;j<i;++j) if (view.players[i].characterId==view.players[j].characterId) return false;
    }
    for (uint32_t i=0;i<view.petCount;++i) {
        const auto& pet=view.pets[i];
        if (!pet.key || !validRow(pet.data)) return false;
        bool owner=false;
        for (uint32_t j=0;j<view.playerCount;++j) if (view.players[j].characterId==pet.data.characterId) owner=true;
        if (!owner) return false;
        for (uint32_t j=0;j<i;++j) if (view.pets[j].key==pet.key && view.pets[j].data.characterId==pet.data.characterId) return false;
    }
    return skills==view.skills.size() && targets==view.targets.size() && details==view.details.size();
}
inline bool ValidDungeonRecord(const DungeonRecord& record) {
    return record.id && record.id!=UINT64_MAX && record.started>0 && record.ended>=record.started && record.ended<=32503680000LL && record.flags<=7 &&
        std::memchr(record.title,0,sizeof(record.title)) && std::memchr(record.ending,0,sizeof(record.ending)) && record.view.playerCount &&
        !record.view.petCount && ValidMeterView(record.view) && ValidMeterView(record.taken);
}

namespace MeterHistoryIO {
constexpr uint32_t Magic=0x34485244;
constexpr uint64_t MaxBytes=128ull*1024*1024;
#pragma pack(push,1)
struct RecordHeader { uint64_t id; int64_t started,ended; uint32_t flags; char title[128],ending[48]; };
struct ViewHeader { uint32_t players; double duration; uint32_t pets,skills,targets,details; };
struct OldSkillRow { char name[80],damageType[16],component[24]; double damage,dps; uint32_t hits; char counterpart[64]; uint64_t counterpartId; };
struct OldPlayerRow { uint32_t characterId; char name[64]; double damage,dps; uint32_t hits,skillCount; OldSkillRow skills[64]; };
struct OldPetRow { uint64_t key; OldPlayerRow data; };
struct OldView { uint32_t playerCount; double duration; OldPlayerRow players[5]; uint32_t petCount; OldPetRow pets[64]; };
struct PreviousView { uint32_t playerCount; double duration; OldPlayerRow players[5]; };
struct LegacySkillRow { char name[80],damageType[16],component[24]; double damage,dps; uint32_t hits; };
struct LegacyPlayerRow { uint32_t characterId; char name[64]; double damage,dps; uint32_t hits,skillCount; LegacySkillRow skills[64]; };
struct LegacyView { uint32_t playerCount; double duration; LegacyPlayerRow players[5]; };
#pragma pack(pop)
struct OldRecord { uint64_t id; int64_t started,ended; uint32_t flags; char title[128],ending[48]; OldView view,taken; };
struct PreviousRecord { uint64_t id; int64_t started,ended; uint32_t flags; char title[128],ending[48]; PreviousView view,taken; };
struct LegacyRecord { uint64_t id; int64_t started,ended; uint32_t flags; char title[128],ending[48]; LegacyView view; };
static_assert(sizeof(OldRecord)==1886344 && sizeof(PreviousRecord)==136832 && sizeof(LegacyRecord)==45480);
static_assert(sizeof(RecordHeader)==204 && sizeof(ViewHeader)==28);

inline void Hash(uint32_t& sum,const void* data,size_t size) {
    const auto* bytes=static_cast<const unsigned char*>(data);
    for (size_t i=0;i<size;++i) sum=(sum^bytes[i])*16777619u;
}
struct Output {
    std::ofstream& stream;
    uint32_t checksum=2166136261u;
    uint64_t bytes=0;
    bool Put(const void* data,size_t size) {
        if (!size) return true;
        if (size>MaxBytes-bytes) return false;
        stream.write(static_cast<const char*>(data),size);
        if (!stream) return false;
        Hash(checksum,data,size); bytes+=size; return true;
    }
};
struct Input {
    std::ifstream& stream;
    uint64_t remaining;
    uint32_t checksum=2166136261u;
    bool Get(void* data,size_t size) {
        if (!size) return true;
        if (size>remaining) return false;
        stream.read(static_cast<char*>(data),size);
        if (!stream) return false;
        Hash(checksum,data,size); remaining-=size; return true;
    }
};
inline bool WriteView(Output& out,const MeterView& view) {
    const ViewHeader header{view.playerCount,view.duration,view.petCount,static_cast<uint32_t>(view.skills.size()),static_cast<uint32_t>(view.targets.size()),static_cast<uint32_t>(view.details.size())};
    return out.Put(&header,sizeof(header)) && out.Put(view.players,view.playerCount*sizeof(PlayerRow)) && out.Put(view.pets,view.petCount*sizeof(PetRow)) &&
        out.Put(view.skills.data(),view.skills.size()*sizeof(SkillRow)) && out.Put(view.targets.data(),view.targets.size()*sizeof(TargetRow)) && out.Put(view.details.data(),view.details.size()*sizeof(DetailRow));
}
inline bool ReadView(Input& in,MeterView& view,uint32_t version = 5) {
    ViewHeader header{};
    if (!in.Get(&header,sizeof(header)) || header.players>MeterPlayers || header.pets>MeterPets || header.skills>MeterSkills || header.targets>MeterTargets || header.details>MeterDetails) return false;
    if (version < 1 || version > 5) return false;
    const size_t playerBytes = version >= 3 ? sizeof(PlayerRow) : offsetof(PlayerRow,critical)+(version == 2 ? 16 : 0);
    const size_t skillBytes = version >= 5 ? sizeof(SkillRow) : version == 4 ? offsetof(SkillRow,values) : offsetof(SkillRow,critical);
    const size_t detailBytes = version >= 5 ? sizeof(DetailRow) : version == 4 ? offsetof(DetailRow,values) : offsetof(DetailRow,critical);
    const uint64_t size=uint64_t(header.players)*playerBytes+uint64_t(header.pets)*(playerBytes+sizeof(uint64_t))+uint64_t(header.skills)*skillBytes+uint64_t(header.targets)*sizeof(TargetRow)+uint64_t(header.details)*detailBytes;
    if (size>in.remaining) return false;
    view.playerCount=header.players; view.petCount=header.pets; view.duration=header.duration;
    view.skills.resize(header.skills); view.targets.resize(header.targets); view.details.resize(header.details);
    for (uint32_t i=0;i<header.players;++i) { view.players[i]={}; if (!in.Get(&view.players[i],playerBytes)) return false; }
    for (uint32_t i=0;i<header.pets;++i) {
        view.pets[i]={};
        if (!in.Get(&view.pets[i].key,sizeof(uint64_t)) || !in.Get(&view.pets[i].data,playerBytes)) return false;
    }
    for (uint32_t i=0;i<header.skills;++i) {
        auto& skill = view.skills[i]; skill = {};
        if (!in.Get(&skill,skillBytes)) return false;
        if (!skill.key) skill.key = uint64_t(i)+1;
    }
    if (!in.Get(view.targets.data(),view.targets.size()*sizeof(TargetRow))) return false;
    for (auto& detail:view.details) { detail = {}; if (!in.Get(&detail,detailBytes)) return false; }
    return ValidMeterView(view);
}
template<class Old> inline void Metadata(DungeonRecord& target,const Old& source) {
    target.id=source.id; target.started=source.started; target.ended=source.ended; target.flags=source.flags;
    std::memcpy(target.title,source.title,sizeof(target.title)); std::memcpy(target.ending,source.ending,sizeof(target.ending));
}
inline bool ConvertRow(MeterView& view,PlayerRow& row,const OldPlayerRow& old) {
    if (!std::memchr(old.name,0,sizeof(old.name)) || old.skillCount>64) return false;
    row={}; row.characterId=old.characterId; std::memcpy(row.name,old.name,sizeof(row.name)); row.damage=old.damage; row.dps=old.dps; row.hits=old.hits;
    using Key=std::tuple<std::string,std::string,std::string>;
    std::map<Key,SkillRow> skills;
    std::map<std::pair<uint64_t,std::string>,TargetRow> targets;
    for (uint32_t i=0;i<old.skillCount;++i) {
        const auto& source=old.skills[i];
        if (!std::memchr(source.name,0,sizeof(source.name)) || !std::memchr(source.component,0,sizeof(source.component)) || !std::memchr(source.damageType,0,sizeof(source.damageType)) ||
            !std::memchr(source.counterpart,0,sizeof(source.counterpart)) || !MeterNumber(source.damage) || !MeterNumber(source.dps)) return false;
        auto& skill=skills[{source.name,source.component,source.damageType}];
        std::memcpy(skill.name,source.name,sizeof(skill.name)); std::memcpy(skill.component,source.component,sizeof(skill.component)); std::memcpy(skill.damageType,source.damageType,sizeof(skill.damageType));
        skill.damage+=source.damage; skill.dps+=source.dps; skill.hits=static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX,uint64_t(skill.hits)+source.hits));
        auto& target=targets[{source.counterpartId,source.counterpart}];
        target.counterpartId=source.counterpartId; std::memcpy(target.counterpart,source.counterpart,sizeof(target.counterpart));
        target.damage+=source.damage; target.dps+=source.dps; target.hits=static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX,uint64_t(target.hits)+source.hits));
    }
    if (view.skills.size()+skills.size()>MeterSkills || view.targets.size()+targets.size()>MeterTargets || view.details.size()+old.skillCount>MeterDetails) return false;
    std::vector<std::pair<Key,SkillRow>> orderedSkills(skills.begin(),skills.end());
    std::sort(orderedSkills.begin(),orderedSkills.end(),[](const auto& a,const auto& b) { return a.second.damage!=b.second.damage ? a.second.damage>b.second.damage : a.first<b.first; });
    std::map<Key,uint32_t> indexes;
    row.skillFirst=static_cast<uint32_t>(view.skills.size()); row.skillCount=static_cast<uint32_t>(orderedSkills.size());
    for (const auto& item:orderedSkills) { indexes[item.first]=static_cast<uint32_t>(view.skills.size()); auto skill=item.second; skill.key=uint64_t(view.skills.size())+1; view.skills.push_back(skill); }
    std::vector<TargetRow> orderedTargets;
    for (const auto& item:targets) orderedTargets.push_back(item.second);
    std::sort(orderedTargets.begin(),orderedTargets.end(),[](const auto& a,const auto& b) { return a.damage!=b.damage ? a.damage>b.damage : std::strcmp(a.counterpart,b.counterpart)<0; });
    row.targetFirst=static_cast<uint32_t>(view.targets.size()); row.targetCount=static_cast<uint32_t>(orderedTargets.size());
    for (auto& target:orderedTargets) {
        std::map<uint32_t,DetailRow> details;
        for (uint32_t i=0;i<old.skillCount;++i) {
            const auto& source=old.skills[i];
            if (source.counterpartId!=target.counterpartId || std::strcmp(source.counterpart,target.counterpart)) continue;
            const auto index=indexes.at({source.name,source.component,source.damageType});
            auto& detail=details[index]; detail.skill=index; detail.damage+=source.damage;
            detail.hits=static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX,uint64_t(detail.hits)+source.hits));
        }
        std::vector<DetailRow> ordered;
        for (const auto& item:details) ordered.push_back(item.second);
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return a.damage!=b.damage ? a.damage>b.damage : a.skill<b.skill; });
        target.detailFirst=static_cast<uint32_t>(view.details.size()); target.detailCount=static_cast<uint32_t>(ordered.size());
        view.details.insert(view.details.end(),ordered.begin(),ordered.end()); view.targets.push_back(target);
    }
    return ValidMeterRow(row);
}
template<class Old> inline bool ConvertView(MeterView& view,const Old& old) {
    if (old.playerCount>MeterPlayers) return false;
    view.playerCount=old.playerCount; view.duration=old.duration;
    for (uint32_t i=0;i<old.playerCount;++i) if (!ConvertRow(view,view.players[i],old.players[i])) return false;
    if constexpr (std::is_same_v<Old,OldView>) {
        if (old.petCount>MeterPets) return false;
        view.petCount=old.petCount;
        for (uint32_t i=0;i<old.petCount;++i) { view.pets[i].key=old.pets[i].key; if (!ConvertRow(view,view.pets[i].data,old.pets[i].data)) return false; }
    }
    return ValidMeterView(view);
}
inline bool Read(const std::filesystem::path& file,std::vector<DungeonRecord>& records,std::string& error) {
    records.clear(); error.clear();
    std::ifstream stream(file,std::ios::binary|std::ios::ate);
    if (!stream) { std::error_code code; if (std::filesystem::exists(file,code) || code) error="History could not be read; previous file left untouched."; return error.empty(); }
    const auto size=stream.tellg();
    if (size<16 || static_cast<uint64_t>(size)>MaxBytes) { error="History size is invalid; previous file left untouched."; return false; }
    stream.seekg(0); uint32_t header[4]{}; stream.read(reinterpret_cast<char*>(header),sizeof(header));
    const bool current=header[0]==Magic && header[1]>=1 && header[1]<=5;
    const bool old=header[0]==0x33485244 && header[1]==sizeof(OldRecord);
    const bool previous=header[0]==0x32485244 && header[1]==sizeof(PreviousRecord);
    const bool legacy=header[0]==0x31485244 && header[1]==sizeof(LegacyRecord);
    if (!stream || header[2]>20 || (!current && !old && !previous && !legacy) || (!current && uint64_t(size)!=16+uint64_t(header[2])*header[1])) {
        error="History format is invalid; previous file left untouched."; return false;
    }
    Input input{stream,static_cast<uint64_t>(size)-16};
    records.resize(header[2]);
    bool valid=true;
    for (auto& record:records) {
        if (current) {
            RecordHeader metadata{};
            valid=input.Get(&metadata,sizeof(metadata));
            if (valid) { Metadata(record,metadata); valid=ReadView(input,record.view,header[1]) && ReadView(input,record.taken,header[1]); }
        } else if (old) {
            auto source=std::make_unique<OldRecord>();
            valid=input.Get(source.get(),sizeof(*source));
            if (valid) { Metadata(record,*source); valid=ConvertView(record.view,source->view) && ConvertView(record.taken,source->taken); }
        } else if (previous) {
            auto source=std::make_unique<PreviousRecord>();
            valid=input.Get(source.get(),sizeof(*source));
            if (valid) { Metadata(record,*source); valid=ConvertView(record.view,source->view) && ConvertView(record.taken,source->taken); }
        } else {
            auto source=std::make_unique<LegacyRecord>();
            valid=input.Get(source.get(),sizeof(*source));
            if (valid) {
                Metadata(record,*source); record.flags|=4;
                PreviousView converted{}; converted.playerCount=source->view.playerCount; converted.duration=source->view.duration;
                if (converted.playerCount>MeterPlayers) valid=false;
                for (uint32_t i=0;valid && i<converted.playerCount;++i) {
                    const auto& a=source->view.players[i]; auto& b=converted.players[i];
                    std::memcpy(&b,&a,offsetof(LegacyPlayerRow,skills));
                    if (a.skillCount>64) { valid=false; break; }
                    for (uint32_t j=0;j<a.skillCount;++j) { std::memcpy(&b.skills[j],&a.skills[j],sizeof(LegacySkillRow)); std::memcpy(b.skills[j].counterpart,"Not recorded",sizeof("Not recorded")); }
                }
                if (valid) valid=ConvertView(record.view,converted);
            }
        }
        if (!valid || !ValidDungeonRecord(record)) { valid=false; break; }
    }
    if (!valid || input.remaining || input.checksum!=header[3]) { records.clear(); error="Invalid history data or checksum; previous file left untouched."; return false; }
    for (size_t i=0;i<records.size();++i) for (size_t j=0;j<i;++j) if (records[i].id==records[j].id) { records.clear(); error="Duplicate history record."; return false; }
    return true;
}
}

inline DWORD DungeonRecord::WriteHistory(const std::filesystem::path& file,uint32_t,const std::vector<std::shared_ptr<const DungeonRecord>>& records) {
    if (records.size()>20) return ERROR_INVALID_DATA;
    for (const auto& record:records) if (!record || !ValidDungeonRecord(*record)) return ERROR_INVALID_DATA;
    auto pending=file; pending+=L".pending";
    std::ofstream stream(pending,std::ios::binary|std::ios::trunc);
    uint32_t header[]={MeterHistoryIO::Magic,5,static_cast<uint32_t>(records.size()),0};
    stream.write(reinterpret_cast<const char*>(header),sizeof(header));
    MeterHistoryIO::Output output{stream};
    for (const auto& record:records) {
        MeterHistoryIO::RecordHeader metadata{record->id,record->started,record->ended,record->flags,{},{}};
        std::memcpy(metadata.title,record->title,sizeof(metadata.title)); std::memcpy(metadata.ending,record->ending,sizeof(metadata.ending));
        if (!output.Put(&metadata,sizeof(metadata)) || !MeterHistoryIO::WriteView(output,record->view) || !MeterHistoryIO::WriteView(output,record->taken)) return ERROR_WRITE_FAULT;
    }
    header[3]=output.checksum; stream.seekp(0); stream.write(reinterpret_cast<const char*>(header),sizeof(header)); stream.close();
    if (!stream.good()) return ERROR_WRITE_FAULT;
    if (!MoveFileExW(pending.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return GetLastError();
    return ERROR_SUCCESS;
}
